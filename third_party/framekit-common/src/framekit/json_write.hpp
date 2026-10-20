#pragma once

#include <filesystem>
#include <string>

#include <json/json.h>

#include "framekit/files.hpp"

namespace framekit {

enum class JsonLayout { Compact, Indented };
enum class JsonCharacters { Utf8, AsciiEscapes };

std::string writeJson(const Json::Value& value, JsonLayout layout = JsonLayout::Compact,
                      JsonCharacters characters = JsonCharacters::Utf8);
bool writeJsonFile(const std::filesystem::path& path, const Json::Value& value, std::string* error = nullptr,
                   const WriteOptions& options = {});

}
