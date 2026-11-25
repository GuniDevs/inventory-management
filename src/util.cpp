#include "util.hpp"

#include <charconv>
#include <system_error>

namespace inventory {

std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> words;
    std::string current;
    bool in_quotes = false;
    bool has_word = false;
    for (char c : line) {
        if (c == '"') {
            in_quotes = !in_quotes;
            has_word = true;
        } else if ((c == ' ' || c == '\t') && !in_quotes) {
            if (has_word) {
                words.push_back(current);
                current.clear();
                has_word = false;
            }
        } else {
            current.push_back(c);
            has_word = true;
        }
    }
    if (has_word) {
        words.push_back(current);
    }
    return words;
}

template <typename T>
static bool parse_number(const std::string& text, T& out) {
    if (text.empty()) {
        return false;
    }
    T value{};
    const char* begin = text.data();
    const char* end = begin + text.size();
    auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc() || result.ptr != end) {
        return false;
    }
    out = value;
    return true;
}

bool parse_int(const std::string& text, int& out) {
    return parse_number(text, out);
}

bool parse_i64(const std::string& text, std::int64_t& out) {
    return parse_number(text, out);
}

std::string format_money(std::int64_t cents) {
    const bool negative = cents < 0;
    const std::int64_t abs_cents = negative ? -cents : cents;
    const std::int64_t whole = abs_cents / 100;
    const std::int64_t part = abs_cents % 100;
    std::string text = std::string(negative ? "-" : "") + std::to_string(whole) + ".";
    if (part < 10) {
        text += "0";
    }
    text += std::to_string(part);
    return text;
}

std::string escape_csv(const std::string& field) {
    std::string text = field;
    if (!text.empty() && (text[0] == '=' || text[0] == '+' || text[0] == '-' || text[0] == '@')) {
        text.insert(text.begin(), '\'');
    }
    if (text.find_first_of(",\"\r\n") == std::string::npos) {
        return text;
    }
    std::string quoted = "\"";
    for (char c : text) {
        if (c == '"') {
            quoted.push_back('"');
        }
        quoted.push_back(c);
    }
    quoted.push_back('"');
    return quoted;
}

bool split_csv_line(const std::string& line, std::vector<std::string>& fields) {
    fields.clear();
    std::string current;
    bool in_quotes = false;
    for (std::size_t i = 0; i < line.size(); i++) {
        const char c = line[i];
        if (in_quotes) {
            if (c != '"') {
                current.push_back(c);
            } else if (i + 1 < line.size() && line[i + 1] == '"') {
                current.push_back('"');
                i++;
            } else {
                in_quotes = false;
            }
        } else if (c == '"') {
            in_quotes = true;
        } else if (c == ',') {
            fields.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    fields.push_back(current);
    return !in_quotes;
}

bool is_safe_csv_path(const std::string& path) {
    constexpr std::size_t kMaxLength = 255;
    const std::string suffix = ".csv";
    if (path.size() <= suffix.size() || path.size() > kMaxLength) {
        return false;
    }
    if (path.compare(path.size() - suffix.size(), suffix.size(), suffix) != 0) {
        return false;
    }
    if (path[0] == '/' || path[0] == '\\' || path.find(':') != std::string::npos) {
        return false;
    }
    if (path.find("..") != std::string::npos) {
        return false;
    }
    for (char c : path) {
        if (static_cast<unsigned char>(c) < 0x20) {
            return false;
        }
    }
    return true;
}

}  // namespace inventory
