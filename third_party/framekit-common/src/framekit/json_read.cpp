#include "framekit/json_read.hpp"

#include <cmath>
#include <exception>
#include <memory>

#include "framekit/files.hpp"
#include "framekit/text.hpp"

namespace framekit {

std::optional<Json::Value> parseJson(const std::string& text, std::string* error) {
    std::string errors;
    try {
        Json::Value root;
        Json::CharReaderBuilder builder;
        std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
        if (reader->parse(text.data(), text.data() + text.size(), &root, &errors)) {
            return root;
        }
    } catch (const std::exception& exception) {
        errors = exception.what();
    }
    if (error) {
        *error = trim(errors);
        if (error->empty()) {
            *error = "not JSON";
        }
    }
    return std::nullopt;
}

const Json::Value& member(const Json::Value& object, const char* key) {
    static const Json::Value missing;
    return object.isObject() ? object[key] : missing;
}

std::string textMember(const Json::Value& object, const char* key) {
    const Json::Value& value = member(object, key);
    return value.isString() ? value.asString() : std::string();
}

bool boolMember(const Json::Value& object, const char* key, bool fallback) {
    const Json::Value& value = member(object, key);
    return value.isBool() ? value.asBool() : fallback;
}

std::optional<double> numberMember(const Json::Value& object, const char* key) {
    const Json::Value& value = member(object, key);
    if (!value.isNumeric()) {
        return std::nullopt;
    }
    const double number = value.asDouble();
    if (!std::isfinite(number)) {
        return std::nullopt;
    }
    return number;
}

std::optional<long long> integerMember(const Json::Value& object, const char* key) {
    const Json::Value& value = member(object, key);
    if (!value.isInt64()) {
        return std::nullopt;
    }
    return static_cast<long long>(value.asInt64());
}

JsonFile readJsonFile(const std::filesystem::path& path) {
    JsonFile result;
    FileRead read = readFileChecked(path);
    if (read.status == ReadStatus::Missing) {
        result.status = JsonFileStatus::Missing;
        result.error = std::move(read.error);
        return result;
    }
    if (read.status == ReadStatus::Unreadable) {
        result.status = JsonFileStatus::Unreadable;
        result.error = std::move(read.error);
        return result;
    }
    std::optional<Json::Value> value = parseJson(read.content, &result.error);
    if (!value) {
        result.status = JsonFileStatus::Invalid;
        return result;
    }
    result.status = JsonFileStatus::Ok;
    result.error.clear();
    result.value = std::move(*value);
    return result;
}

}
