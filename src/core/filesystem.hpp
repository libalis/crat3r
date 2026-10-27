#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

//--------------------------------------------------------------------------------------------------

struct Fs final {
    static std::vector<char> readFile(const std::string& filename) {
        std::ifstream file(findPath(filename), std::ios::ate | std::ios::binary);

        if (!file.is_open()) {
            fprintf(stderr, "File could not be found: %s\n", filename.c_str());
            exit(1);
        }

        size_t fileSize = (size_t)file.tellg();
        std::vector<char> buffer(fileSize);

        file.seekg(0);
        file.read(buffer.data(), fileSize);

        file.close();

        return buffer;
    }

    static std::filesystem::path findPath(const std::string& filename) {
        const auto absPath = std::filesystem::path(filename);
        if (absPath.is_absolute()) {
            return absPath;
        }

        const auto cwdPath = std::filesystem::current_path() / filename;
        if (std::filesystem::exists(cwdPath)) {
            return cwdPath;
        }

        const auto projPath = std::filesystem::path(ROOT_DIR) / filename;
        if (std::filesystem::exists(projPath)) {
            return projPath;
        }

        return absPath; // probably does not exist
    }
};
