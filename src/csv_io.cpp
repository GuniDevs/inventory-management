#include "csv_io.hpp"

#include <cstdint>
#include <fstream>
#include <vector>

#include "util.hpp"

namespace inventory {

namespace {

constexpr std::size_t kMaxRows = 10000;
constexpr std::size_t kMaxLineLength = 1024;
const std::string kHeader = "sku,name,quantity,reorder_level,price_cents";
const std::string kBadPath = "the file must be a plain relative name ending in .csv";

void drop_carriage_return(std::string& line) {
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
}

// Undoes the ' that escape_csv puts in front of text starting with = + - or @.
std::string unescape_formula(const std::string& field) {
    if (field.size() > 1 && field[0] == '\'' &&
        (field[1] == '=' || field[1] == '+' || field[1] == '-' || field[1] == '@')) {
        return field.substr(1);
    }
    return field;
}

// Adds the product on one data row. Returns "" when it worked, otherwise the reason it did not.
std::string import_row(InventoryService& service, const std::string& line) {
    std::vector<std::string> fields;
    if (!split_csv_line(line, fields) || fields.size() != 5) {
        return "expected 5 fields";
    }
    int quantity = 0;
    int reorder_level = 0;
    std::int64_t price_cents = 0;
    if (!parse_int(fields[2], quantity) || !parse_int(fields[3], reorder_level) || !parse_i64(fields[4], price_cents)) {
        return "quantity, reorder level and price must be whole numbers";
    }
    const Result added =
        service.add_product(unescape_formula(fields[0]), unescape_formula(fields[1]), quantity, reorder_level, price_cents);
    return added.ok ? "" : added.message;
}

}  // namespace

Result export_products_csv(const InventoryService& service, const std::string& path) {
    if (!is_safe_csv_path(path)) {
        return {false, kBadPath};
    }
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        return {false, "cannot write " + path};
    }
    out << kHeader << "\n";
    int count = 0;
    for (const Product& product : service.products().all()) {
        out << escape_csv(product.sku) << ',' << escape_csv(product.name) << ',' << product.quantity << ','
            << product.reorder_level << ',' << product.price_cents << "\n";
        count++;
    }
    out.close();
    return out.fail() ? Result{false, "could not finish writing " + path} : Result{true, "exported " + std::to_string(count) + " product(s) to " + path};
}

Result import_products_csv(InventoryService& service, const std::string& path) {
    if (!is_safe_csv_path(path)) {
        return {false, kBadPath};
    }
    std::ifstream in(path);
    if (!in) {
        return {false, "cannot read " + path};
    }
    std::string line;
    std::getline(in, line);
    drop_carriage_return(line);
    if (line != kHeader) {
        return {false, "the first line must be: " + kHeader};
    }

    int added = 0;
    int rejected = 0;
    std::string first_problem;
    std::size_t rows = 0;
    while (std::getline(in, line)) {
        drop_carriage_return(line);
        if (line.empty()) {
            continue;
        }
        if (++rows > kMaxRows) {
            return {false, "too many rows: stopped after " + std::to_string(kMaxRows)};
        }
        const std::string problem = line.size() > kMaxLineLength ? "line is too long" : import_row(service, line);
        if (problem.empty()) {
            added++;
        } else {
            rejected++;
            if (first_problem.empty()) {
                first_problem = "row " + std::to_string(rows) + ": " + problem;
            }
        }
    }
    std::string message = "imported " + std::to_string(added) + " product(s)";
    if (rejected > 0) {
        message += ", rejected " + std::to_string(rejected) + " (" + first_problem + ")";
    }
    return {rejected == 0, message};
}

}  // namespace inventory
