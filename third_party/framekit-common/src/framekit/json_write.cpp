#include "framekit/json_write.hpp"

namespace framekit {

std::string writeJson(const Json::Value& value, JsonLayout layout, JsonCharacters characters) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = layout == JsonLayout::Indented ? "  " : "";
    builder["emitUTF8"] = characters == JsonCharacters::Utf8;
    return Json::writeString(builder, value);
}

bool writeJsonFile(const std::filesystem::path& path, const Json::Value& value, std::string* error,
                   const WriteOptions& options) {
    return writeFileAtomically(path, writeJson(value, JsonLayout::Indented) + "\n", error, options);
}

}
