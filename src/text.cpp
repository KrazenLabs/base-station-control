#include "text.hpp"

#include <cctype>
#include <cstdio>

namespace bsc {

namespace {

bool isSpace(char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

char lowerChar(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool isContinuationByte(char c) {
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

}

std::optional<long long> parseInteger(std::string_view text) {
    const std::string trimmed = trim(text);
    size_t position = 0;
    bool negative = false;
    if (position < trimmed.size() && (trimmed[position] == '-' || trimmed[position] == '+')) {
        negative = trimmed[position] == '-';
        ++position;
    }
    if (position >= trimmed.size() || trimmed.size() - position > 18) {
        return std::nullopt;
    }
    long long value = 0;
    for (; position < trimmed.size(); ++position) {
        if (!isDigit(trimmed[position])) {
            return std::nullopt;
        }
        value = value * 10 + (trimmed[position] - '0');
    }
    return negative ? -value : value;
}

std::string formatNumber(double value, int decimals) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
    return buffer;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (lowerChar(a[i]) != lowerChar(b[i])) {
            return false;
        }
    }
    return true;
}

std::string hexByte(uint8_t value) {
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "0x%02x", static_cast<unsigned>(value));
    return buffer;
}

size_t countCharacters(std::string_view utf8) {
    size_t count = 0;
    for (const char c : utf8) {
        if (!isContinuationByte(c)) {
            ++count;
        }
    }
    return count;
}

bool isValidNickname(std::string_view name) {
    if (name.empty() || isSpace(name.front()) || isSpace(name.back()) || repairUtf8(name) != name ||
        countCharacters(name) > kNicknameMaxCharacters) {
        return false;
    }
    for (const char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7F || c == '"' || c == '\\') {
            return false;
        }
    }
    return true;
}

}
