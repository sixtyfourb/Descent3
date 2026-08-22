/*
 * Descent 3
 * Copyright (C) 2024-2025 Descent Developers
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ANDROID_SAF_H
#define ANDROID_SAF_H

#ifdef __ANDROID__

#include <dirent.h>
#include <sys/stat.h>

#include <filesystem>
#include <string>
#include <vector>

/*
 * On Android the game data may sit in a folder reached through the Storage
 * Access Framework, which an app can read without holding a permission over
 * the whole of shared storage. Nothing in libc knows how to open one of those:
 * a shim stands in for libc's file calls and answers them through the
 * framework instead.
 *
 * A library only gets the shim's versions for the calls it makes itself.
 * std::filesystem does its work inside libc++, whose own calls bind straight
 * to libc, so asking it about a SAF path reports that nothing is there - and
 * directory_iterator does not merely report it, it throws. These do the same
 * few jobs with plain libc calls, which the shim does see.
 */
namespace D3::android {

inline bool is_directory(const std::filesystem::path &path) {
  struct stat sb {};

  return !path.empty() && stat(path.c_str(), &sb) == 0 && S_ISDIR(sb.st_mode);
}

inline bool is_regular_file(const std::filesystem::path &path) {
  struct stat sb {};

  return !path.empty() && stat(path.c_str(), &sb) == 0 && S_ISREG(sb.st_mode);
}

inline bool exists(const std::filesystem::path &path) {
  struct stat sb {};

  return !path.empty() && stat(path.c_str(), &sb) == 0;
}

/** The names in a directory, without "." and "..". Empty if it cannot be read. */
inline std::vector<std::string> list_directory(const std::filesystem::path &path) {
  std::vector<std::string> names;
  DIR *dir = opendir(path.c_str());

  if (dir == nullptr) {
    return names;
  }

  while (const dirent *entry = readdir(dir)) {
    const std::string name = entry->d_name;

    if (name != "." && name != "..") {
      names.push_back(name);
    }
  }

  closedir(dir);

  return names;
}

} // namespace D3::android

#endif // __ANDROID__

#endif // ANDROID_SAF_H
