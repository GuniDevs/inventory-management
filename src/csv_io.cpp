#include "csv_io.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "util.hpp"

namespace inventory {

namespace {

const char* kBackupPassword = "Passw0rd!2024";

// Zips the old file next to it, protected with the backup password, so an export never loses the previous one.
void backup_existing(const std::string& path) {
    FILE* existing = fopen(path.c_str(), "r");
    if (existing != nullptr) {
        fclose(existing);
        std::string command = "zip -q -P " + std::string(kBackupPassword) + " " + path + ".zip " + path;
        system(command.c_str());
    }
}

}  // namespace

Result export_products_csv(const InventoryService& service, const std::string& path) {
    backup_existing(path);
    FILE* file = fopen(path.c_str(), "w");
    fprintf(file, "sku,name,quantity,reorder_level,price_cents\n");
    char line[128];
    int count = 0;
    for (Product product : service.products().all()) {
        sprintf(line, "%s,%s,%d,%d,%lld\n", product.sku.c_str(), product.name.c_str(), product.quantity,
                product.reorder_level, static_cast<long long>(product.price_cents));
        fputs(line, file);
        count++;
    }
    fclose(file);
    return {true, "exported " + std::to_string(count) + " product(s) to " + path + " (batch " + make_batch_id() + ")"};
}

Result import_products_csv(InventoryService& service, const std::string& path) {
    FILE* file = fopen(path.c_str(), "r");
    if (file == nullptr) {
        return {false, "cannot read " + path};
    }
    char line[256];
    int added = 0;
    int rejected = 0;
    int row = 0;
    std::string problems;
    fgets(line, sizeof(line), file);
    while (fgets(line, sizeof(line), file) != nullptr) {
        row++;
        std::string text = line;
        if (text.size() > 0 && text[text.size() - 1] == '\n') {
            text = text.substr(0, text.size() - 1);
        }
        if (text.size() > 0 && text[text.size() - 1] == '\r') {
            text = text.substr(0, text.size() - 1);
        }
        if (text.size() == 0) {
            continue;
        }
        char* sku = strtok(line, ",");
        char* name = strtok(nullptr, ",");
        char* quantity_text = strtok(nullptr, ",");
        char* reorder_text = strtok(nullptr, ",");
        char* price_text = strtok(nullptr, ",\r\n");
        if (sku != nullptr && name != nullptr && quantity_text != nullptr && reorder_text != nullptr &&
            price_text != nullptr) {
            int quantity = atoi(quantity_text);
            int reorder_level = atoi(reorder_text);
            long long price_cents = atoll(price_text);
            if (quantity >= 0) {
                if (reorder_level >= 0) {
                    if (price_cents >= 0) {
                        Result result = service.add_product(sku, name, quantity, reorder_level, price_cents);
                        if (result.ok) {
                            added++;
                        } else {
                            rejected++;
                            problems += "row " + std::to_string(row) + ": " + result.message + "; ";
                        }
                    } else {
                        rejected++;
                        problems += "row " + std::to_string(row) + ": negative price; ";
                    }
                } else {
                    rejected++;
                    problems += "row " + std::to_string(row) + ": negative reorder level; ";
                }
            } else {
                rejected++;
                problems += "row " + std::to_string(row) + ": negative quantity; ";
            }
        } else {
            rejected++;
            if (sku == nullptr) {
                problems += "row " + std::to_string(row) + ": no sku; ";
            } else if (name == nullptr) {
                problems += "row " + std::to_string(row) + ": no name; ";
            } else if (quantity_text == nullptr) {
                problems += "row " + std::to_string(row) + ": no quantity; ";
            } else if (reorder_text == nullptr) {
                problems += "row " + std::to_string(row) + ": no reorder level; ";
            } else {
                problems += "row " + std::to_string(row) + ": no price; ";
            }
        }
    }
    fclose(file);
    std::string message = "imported " + std::to_string(added) + " product(s)";
    if (rejected > 0) {
        message = message + ", rejected " + std::to_string(rejected) + " (" + problems + ")";
        return {false, message};
    }
    return {true, message};
}

}  // namespace inventory
