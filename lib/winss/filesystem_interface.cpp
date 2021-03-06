/*
 * Copyright 2016-2017 Morgan Stanley
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "filesystem_interface.hpp"
#include <windows.h>
#include <filesystem>
#include <string>
#include <fstream>
#include <vector>
#include <memory>
#include "easylogging/easylogging++.hpp"

namespace fs = std::experimental::filesystem;

std::shared_ptr<winss::FilesystemInterface>
winss::FilesystemInterface::instance =
    std::make_shared<winss::FilesystemInterface>();

std::string winss::FilesystemInterface::Read(const fs::path& path) const {
    try {
        VLOG(5) << "Reading file " << path;

        std::ifstream infile{ path };
        std::string value{
            std::istreambuf_iterator<char>(infile),
            std::istreambuf_iterator<char>()
        };
        infile.close();

        return value;
    } catch (const std::exception& e) {
        VLOG(1) << "Failed to read file " << path << ": " << e.what();
    }

    return "";
}

bool winss::FilesystemInterface::Write(const fs::path& path,
    const std::string& content) const {
    fs::path temp_path = path;
    temp_path += ".new";

    try {
        VLOG(5) << "Writting file " << temp_path;

        std::ofstream outfile(temp_path, std::ios::out | std::ios::trunc);
        outfile << content << std::endl;
        outfile.close();
    } catch (const std::exception& e) {
        VLOG(1) << "Failed to write file " << temp_path << ": " << e.what();
        return false;
    }

    return Rename(temp_path, path);
}

bool winss::FilesystemInterface::ChangeDirectory(const fs::path& dir) const {
    try {
        fs::current_path(dir);
    } catch (const fs::filesystem_error& e) {
        VLOG(1) << "Could not change directory " << dir << ": " << e.what();
        return false;
    }

    return true;
}

bool winss::FilesystemInterface::DirectoryExists(const fs::path& dir) const {
    try {
        fs::file_status dir_status = fs::status(dir);
        return fs::exists(dir_status) && fs::is_directory(dir_status);
    } catch (const fs::filesystem_error& e) {
        VLOG(1) << "Could not check exists " << dir << ": " << e.what();
        return false;
    }
}

bool winss::FilesystemInterface::CreateDirectory(const fs::path& dir) const {
    try {
        if (!DirectoryExists(dir)) {
            if (!fs::create_directory(dir)) {
                VLOG(6) << "Could not create directory " << dir;
                return false;
            }
        }

        return true;
    } catch (const fs::filesystem_error& e) {
        VLOG(1) << "Could not create directory " << dir << ": " << e.what();
        return false;
    }
}

bool winss::FilesystemInterface::Rename(const fs::path& from,
    const fs::path& to) const {
    try {
        VLOG(5) << "Renaming file " << from << " to " << to;

        fs::rename(from, to);
        return true;
    } catch (const fs::filesystem_error& e) {
        VLOG(1)
            << "Could not rename "
            << from
            << " to "
            << to
            << ": "
            << e.what();
        return false;
    }
}

bool winss::FilesystemInterface::Remove(const fs::path& path) const {
    try {
        VLOG(4) << "Removing path " << path;
        fs::remove(path);
        return true;
    } catch (const fs::filesystem_error& e) {
        VLOG(1) << "Could not remove path " << path << ": " << e.what();
        return false;
    }
}

bool winss::FilesystemInterface::FileExists(const fs::path& path) const {
    try {
        fs::file_status dir_status = fs::status(path);
        return fs::exists(dir_status) && fs::is_regular_file(dir_status);
    } catch (const fs::filesystem_error& e) {
        // Incase there are permission problems
        // in which case the file does not exist
        VLOG(1) << "Could check path exists " << path << ": " << e.what();
        return false;
    }
}

fs::path winss::FilesystemInterface::Absolute(const fs::path& path) const {
    try {
        return fs::canonical(path);
    } catch (const std::exception& e) {
        VLOG(1) << "Could not get canonical path " << path << ": " << e.what();
        return path;
    }
}

fs::path winss::FilesystemInterface::CanonicalUncPath(
    const fs::path& path) const {
    auto path_str = path.string();
    HANDLE file = CreateFile(
        path_str.data(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        NULL);

    if (file == INVALID_HANDLE_VALUE) {
        CloseHandle(file);
        return Absolute(path);
    }

    std::vector<char> pathbuf;
    DWORD bufsize = static_cast<DWORD>(path_str.size() * 2);

    while (true) {
        pathbuf.resize(bufsize + 1);
        DWORD len = GetFinalPathNameByHandle(file, pathbuf.data(), bufsize,
            VOLUME_NAME_DOS);

        if (len == 0) {
            CloseHandle(file);
            return Absolute(path);
        }

        if (len <= bufsize)
            break;

        bufsize = len;
    }

    CloseHandle(file);
    fs::path unc_path(pathbuf.begin(), pathbuf.end());
    return unc_path;
}

std::vector<fs::path> winss::FilesystemInterface::GetDirectories(
    const fs::path& path) const {
    std::vector<fs::path> directories;

    try {
        for (auto entry : fs::directory_iterator(path)) {
            if (fs::is_directory(entry.status())) {
                directories.push_back(entry.path());
            } else {
                VLOG(6) << "Skipping non-directory " << entry.path();
            }
        }
    } catch (const fs::filesystem_error& e) {
        VLOG(1)
            << "Could not iterate directories in "
            << path
            << ": "
            << e.what();
    }

    return directories;
}

std::vector<fs::path> winss::FilesystemInterface::GetFiles(
    const fs::path& path) const {
    std::vector<fs::path> files;

    try {
        for (auto entry : fs::directory_iterator(path)) {
            if (!fs::is_directory(entry.status())) {
                files.push_back(entry.path());
            } else {
                VLOG(6) << "Skipping directory " << entry.path();
            }
        }
    } catch (const fs::filesystem_error& e) {
        VLOG(1)
            << "Could not iterate files in "
            << path
            << ": "
            << e.what();
    }

    return files;
}

const winss::FilesystemInterface& winss::FilesystemInterface::GetInstance() {
    if (!winss::FilesystemInterface::instance) {
        winss::FilesystemInterface::instance =
            std::make_shared<FilesystemInterface>();
    }

    return *winss::FilesystemInterface::instance;
}
