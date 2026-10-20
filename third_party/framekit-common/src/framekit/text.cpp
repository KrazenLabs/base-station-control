#include "framekit/text.hpp"

#include <cctype>
#include <cstdint>
#include <cstdio>

namespace framekit {

namespace {

bool isSpace(char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

char lowerChar(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

constexpr std::string_view kReplacementCharacter = "\xEF\xBF\xBD";

}

std::string trim(std::string_view text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && isSpace(text[begin])) {
        ++begin;
    }
    while (end > begin && isSpace(text[end - 1])) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

bool startsWith(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool contains(std::string_view text, std::string_view fragment) {
    return text.find(fragment) != std::string_view::npos;
}

std::vector<std::string> splitWhitespace(std::string_view text) {
    std::vector<std::string> words;
    size_t position = 0;
    while (position < text.size()) {
        while (position < text.size() && isSpace(text[position])) {
            ++position;
        }
        const size_t start = position;
        while (position < text.size() && !isSpace(text[position])) {
            ++position;
        }
        if (position > start) {
            words.emplace_back(text.substr(start, position - start));
        }
    }
    return words;
}

std::vector<std::string> splitLines(std::string_view text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (end < text.size() || !line.empty()) {
            lines.emplace_back(line);
        }
        start = end + 1;
    }
    return lines;
}

std::string toLower(std::string_view text) {
    std::string lower(text);
    for (char& c : lower) {
        c = lowerChar(c);
    }
    return lower;
}

std::string capitalizeFirst(std::string_view text) {
    std::string result(text);
    if (result.empty() || result[0] < 'a' || result[0] > 'z') {
        return result;
    }
    std::string_view firstWord = text.substr(0, text.find(' '));
    while (!firstWord.empty() && (firstWord.back() == '.' || firstWord.back() == ',' || firstWord.back() == ';' ||
                                  firstWord.back() == ':')) {
        firstWord.remove_suffix(1);
    }
    if (firstWord.find('.') != std::string_view::npos || firstWord.find('/') != std::string_view::npos) {
        return result;
    }
    result[0] = static_cast<char>(result[0] - 'a' + 'A');
    return result;
}

std::string fnv1aHex(std::string_view data) {
    uint64_t hash = 14695981039346656037ULL;
    for (const char c : data) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 1099511628211ULL;
    }
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(hash));
    return buffer;
}

std::string joinWords(const std::vector<std::string>& words, std::string_view separator) {
    std::string joined;
    for (size_t i = 0; i < words.size(); ++i) {
        if (i > 0) {
            joined += separator;
        }
        joined += words[i];
    }
    return joined;
}

std::string repairUtf8(std::string_view text) {
    std::string repaired;
    repaired.reserve(text.size());
    size_t position = 0;
    while (position < text.size()) {
        const auto lead = static_cast<unsigned char>(text[position]);
        if (lead < 0x80) {
            repaired += static_cast<char>(lead);
            ++position;
            continue;
        }
        size_t continuations = 0;
        unsigned char secondLow = 0x80;
        unsigned char secondHigh = 0xBF;
        if (lead >= 0xC2 && lead <= 0xDF) {
            continuations = 1;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            continuations = 2;
            secondLow = lead == 0xE0 ? 0xA0 : 0x80;
            secondHigh = lead == 0xED ? 0x9F : 0xBF;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            continuations = 3;
            secondLow = lead == 0xF0 ? 0x90 : 0x80;
            secondHigh = lead == 0xF4 ? 0x8F : 0xBF;
        }
        size_t length = 1;
        while (length <= continuations && position + length < text.size()) {
            const auto next = static_cast<unsigned char>(text[position + length]);
            const unsigned char low = length == 1 ? secondLow : 0x80;
            const unsigned char high = length == 1 ? secondHigh : 0xBF;
            if (next < low || next > high) {
                break;
            }
            ++length;
        }
        if (continuations > 0 && length == continuations + 1) {
            repaired.append(text.substr(position, length));
        } else {
            repaired += kReplacementCharacter;
        }
        position += length;
    }
    return repaired;
}

}
