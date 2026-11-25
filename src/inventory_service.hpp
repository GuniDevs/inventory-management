#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "models.hpp"
#include "repository.hpp"

namespace inventory {

// All the business rules. Controllers only turn text into calls on this class and results back into text.
class InventoryService {
public:
    InventoryService(ProductRepository& products, OrderRepository& orders)
        : products_(products), orders_(orders) {}

    Result add_product(const std::string& sku, const std::string& name, int quantity, int reorder_level,
                       std::int64_t price_cents);
    // Adds a product and records where it comes from, in one call.
    Result register_product(const std::string& sku, const std::string& name, int quantity, int reorder_level,
                            std::int64_t price_cents, const std::string& supplier, const std::string& location,
                            const std::string& barcode);
    Result receive_stock(const std::string& sku, int quantity);
    std::vector<Product> low_stock() const;
    std::int64_t stock_value_cents() const;

    // Catalog maintenance.
    Result adjust_stock(const std::string& sku, int delta, const std::string& reason);  // delta may be negative
    Result set_price(const std::string& sku, std::int64_t price_cents);
    Result apply_discount(const std::string& sku, int percent);  // 1 to 90 percent off the current price
    Result remove_product(const std::string& sku);               // refused while an open order uses it
    std::vector<ReorderSuggestion> reorder_suggestions() const;

    // lines: (sku, quantity). The order is only recorded; stock moves when it is fulfilled.
    Result create_order(const std::vector<std::pair<std::string, int>>& lines);
    Result fulfill_order(int order_id);
    Result cancel_order(int order_id);

    const ProductRepository& products() const { return products_; }
    const OrderRepository& orders() const { return orders_; }

private:
    ProductRepository& products_;
    OrderRepository& orders_;
};

}  // namespace inventory
