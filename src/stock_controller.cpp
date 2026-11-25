#include "stock_controller.hpp"

#include "util.hpp"

namespace inventory {

std::string StockController::usage() const {
    return "stock receive <sku> <quantity> | stock low | stock value | stock adjust <sku> <change> <reason> | "
           "stock reorder";
}

Result StockController::handle(const std::vector<std::string>& args) {
    if (args.empty()) {
        return {false, "usage: " + usage()};
    }
    const std::string& action = args[0];

    if (action == "receive") {
        if (args.size() != 3) {
            return {false, "usage: stock receive <sku> <quantity>"};
        }
        int quantity = 0;
        if (!parse_int(args[2], quantity)) {
            return {false, "quantity must be a whole number"};
        }
        return service_.receive_stock(args[1], quantity);
    }

    if (action == "low") {
        const std::vector<Product> low = service_.low_stock();
        if (low.empty()) {
            return {true, "nothing is low on stock"};
        }
        std::string text;
        for (const Product& product : low) {
            text += product.sku + "  qty " + std::to_string(product.quantity) + " (reorder at " +
                    std::to_string(product.reorder_level) + ")\n";
        }
        return {true, text};
    }

    if (action == "value") {
        return {true, "stock value: " + format_money(service_.stock_value_cents())};
    }

    if (action == "adjust") {
        int delta = 0;
        if (args.size() != 4 || !parse_int(args[2], delta)) {
            return {false, "usage: stock adjust <sku> <change> <reason>"};
        }
        return service_.adjust_stock(args[1], delta, args[3]);
    }

    if (action == "reorder") {
        const std::vector<ReorderSuggestion> suggestions = service_.reorder_suggestions();
        if (suggestions.empty()) {
            return {true, "nothing needs reordering"};
        }
        std::string text;
        for (const ReorderSuggestion& suggestion : suggestions) {
            text += format_reorder_line(suggestion.sku, suggestion.quantity) + "\n";
        }
        return {true, text};
    }

    return {false, "unknown stock command '" + action + "'. usage: " + usage()};
}

}  // namespace inventory
