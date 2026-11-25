#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace inventory {

// Splits a line on spaces and tabs. Text inside double quotes stays together ("blue widget" is one word).
std::vector<std::string> tokenize(const std::string& line);

// Whole-string number parsing: "12" works, "12abc" and "" do not.
bool parse_int(const std::string& text, int& out);
bool parse_i64(const std::string& text, std::int64_t& out);

// 1999 -> "19.99"
std::string format_money(std::int64_t cents);

// One CSV field, ready to write. Quotes it when needed, and puts a ' in front of text that a spreadsheet would
// run as a formula (it starts with = + - or @).
std::string escape_csv(const std::string& field);

// Splits one CSV line into fields ("a,""b"",c" is two fields). False when a quote is never closed.
bool split_csv_line(const std::string& line, std::vector<std::string>& fields);

// Only plain relative file names ending in .csv are allowed for import and export: no absolute paths, no "..",
// no control characters, at most 255 characters.
bool is_safe_csv_path(const std::string& path);

}  // namespace inventory
