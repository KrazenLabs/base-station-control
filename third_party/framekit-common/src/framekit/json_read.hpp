#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <json/json.h>

namespace framekit {

std::optional<Json::Value> parseJson(const std::string& text, std::string* error = nullptr);
const Json::Value& member(const Json::Value& object, const char* key);
std::string textMember(const Json::Value& object, const char* key);
bool boolMember(const Json::Value& object, const char* key, bool fallback = false);
std::optional<double> numberMember(const Json::Value& object, const char* key);
std::optional<long long> integerMember(const Json::Value& object, const char* key);

enum class JsonFileStatus { Ok, Missing, Unreadable, Invalid };

struct JsonFile {
    JsonFileStatus status = JsonFileStatus::Missing;
    Json::Value value;
    std::string error;
};

JsonFile readJsonFile(const std::filesystem::path& path);

}
