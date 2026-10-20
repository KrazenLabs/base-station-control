#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace framekit {

struct WriteOptions {
    bool durable = false;
    bool ownerOnly = false;
};

enum class ReadStatus { Ok, Missing, Unreadable };

struct FileRead {
    ReadStatus status = ReadStatus::Missing;
    std::string content;
    std::string error;
};

FileRead readFileChecked(const std::filesystem::path& path);
std::optional<std::string> readFile(const std::filesystem::path& path);
bool writeFileAtomically(const std::filesystem::path& path, const std::string& content, std::string* error = nullptr,
                         const WriteOptions& options = {});
std::vector<std::string> listDirectory(const std::filesystem::path& directory);

}
