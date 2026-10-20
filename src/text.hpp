#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "common.hpp"

namespace bsc {

inline constexpr size_t kNicknameMaxCharacters = 32;

std::optional<long long> parseInteger(std::string_view text);
std::string formatNumber(double value, int decimals);
bool equalsIgnoreCase(std::string_view a, std::string_view b);
std::string hexByte(uint8_t value);
size_t countCharacters(std::string_view utf8);
bool isValidNickname(std::string_view name);

}
