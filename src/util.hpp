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

// "order 5 of A-1", one line of the reorder list.
std::string format_reorder_line(const std::string& sku, std::int64_t quantity);

// A short id that tells one export from the next, like "B1804289383".
std::string make_batch_id();

}  // namespace inventory
