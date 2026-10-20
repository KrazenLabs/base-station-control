#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace framekit {

std::string trim(std::string_view text);
bool startsWith(std::string_view text, std::string_view prefix);
bool endsWith(std::string_view text, std::string_view suffix);
bool contains(std::string_view text, std::string_view fragment);
std::vector<std::string> splitWhitespace(std::string_view text);
std::vector<std::string> splitLines(std::string_view text);
std::string toLower(std::string_view text);
std::string capitalizeFirst(std::string_view text);
std::string fnv1aHex(std::string_view data);
std::string joinWords(const std::vector<std::string>& words, std::string_view separator);
std::string repairUtf8(std::string_view text);

}
