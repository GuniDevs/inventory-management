#include "product_controller.hpp"

#include <sstream>

#include "csv_io.hpp"
#include "util.hpp"

namespace inventory {

namespace {

std::string describe(const Product& product) {
    std::ostringstream out;
    out << product.sku << "  " << product.name << "  qty " << product.quantity << "  reorder at "
        << product.reorder_level << "  price " << format_money(product.price_cents);
    return out.str();
}

}  // namespace

std::string ProductController::usage() const {
    return "product add <sku> <name> <quantity> <reorder level> <price cents> | product list | product show <sku> | "
           "product price <sku> <price cents> | product discount <sku> <percent> | product remove <sku> | "
           "product export <file.csv> | product import <file.csv>";
}

Result ProductController::handle(const std::vector<std::string>& args) {
    if (args.empty()) {
        return {false, "usage: " + usage()};
    }
    const std::string& action = args[0];

    if (action == "add") {
        if (args.size() != 6) {
            return {false, "usage: product add <sku> <name> <quantity> <reorder level> <price cents>"};
        }
        int quantity = 0;
        int reorder_level = 0;
        std::int64_t price_cents = 0;
        if (!parse_int(args[3], quantity) || !parse_int(args[4], reorder_level) || !parse_i64(args[5], price_cents)) {
            return {false, "quantity, reorder level and price must be whole numbers"};
        }
        return service_.add_product(args[1], args[2], quantity, reorder_level, price_cents);
    } else if (action == "list") {
        const std::vector<Product> products = service_.products().all();
        if (products.empty()) {
            return {true, "no products yet"};
        } else {
            std::string text;
            for (const Product& product : products) {
                text += describe(product) + "\n";
            }
            return {true, text};
        }
    } else if (action == "show") {
        if (args.size() != 2) {
            return {false, "usage: product show <sku>"};
        }
        auto product = service_.products().find_by_sku(args[1]);
        if (!product) {
            return {false, "unknown sku " + args[1]};
        }
        return {true, describe(*product)};
    } else if (action == "price") {
        std::int64_t price_cents = 0;
        if (args.size() != 3 || !parse_i64(args[2], price_cents)) {
            return {false, "usage: product price <sku> <price cents>"};
        }
        return service_.set_price(args[1], price_cents);
    } else if (action == "discount") {
        int percent = 0;
        if (args.size() != 3 || !parse_int(args[2], percent)) {
            return {false, "usage: product discount <sku> <percent>"};
        }
        return service_.apply_discount(args[1], percent);
    } else if (action == "remove") {
        if (args.size() != 2) {
            return {false, "usage: product remove <sku>"};
        }
        return service_.remove_product(args[1]);
    } else if (action == "export") {
        if (args.size() != 2) {
            return {false, "usage: product export <file.csv>"};
        }
        return export_products_csv(service_, args[1]);
    } else if (action == "import") {
        if (args.size() != 2) {
            return {false, "usage: product import <file.csv>"};
        }
        return import_products_csv(service_, args[1]);
    } else {
        return {false, "unknown product command '" + action + "'. usage: " + usage()};
    }
}

}  // namespace inventory
