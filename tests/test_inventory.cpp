#include <cstdint>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>

#include "csv_io.hpp"
#include "inventory_service.hpp"
#include "order_controller.hpp"
#include "product_controller.hpp"
#include "repository.hpp"
#include "router.hpp"
#include "stock_controller.hpp"
#include "util.hpp"

using namespace inventory;

static int failures = 0;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            std::cerr << "FAILED " << __FILE__ << ":" << __LINE__ << ": " #cond "\n"; \
            failures++;                                                               \
        }                                                                             \
    } while (0)

// One fresh shop per test: repositories, service and a router with the three controllers.
struct Shop {
    ProductRepository products;
    OrderRepository orders;
    InventoryService service{products, orders};
    Router router;

    Shop() {
        router.add(std::make_unique<ProductController>(service));
        router.add(std::make_unique<StockController>(service));
        router.add(std::make_unique<OrderController>(service));
    }
};

static void test_util() {
    CHECK(tokenize("product add A-1 \"blue widget\" 5").size() == 5);
    CHECK(tokenize("product add A-1 \"blue widget\" 5")[3] == "blue widget");
    CHECK(tokenize("   ").empty());

    int n = 0;
    CHECK(parse_int("42", n) && n == 42);
    CHECK(!parse_int("42x", n));
    CHECK(!parse_int("", n));
    std::int64_t big = 0;
    CHECK(parse_i64("9000000000", big) && big == 9000000000LL);

    CHECK(format_money(1999) == "19.99");
    CHECK(format_money(5) == "0.05");
    CHECK(format_money(-250) == "-2.50");
}

static void test_products() {
    Shop shop;
    CHECK(shop.service.add_product("A-1", "Widget", 10, 3, 1999).ok);
    CHECK(!shop.service.add_product("A-1", "Again", 1, 1, 100).ok);  // same sku
    CHECK(!shop.service.add_product("", "No sku", 1, 1, 100).ok);
    CHECK(!shop.service.add_product("B-1", "Negative", -1, 0, 100).ok);

    auto product = shop.products.find_by_sku("A-1");
    CHECK(product.has_value());
    CHECK(product->name == "Widget");
    CHECK(product->price_cents == 1999);
}

static void test_stock() {
    Shop shop;
    shop.service.add_product("A-1", "Widget", 2, 3, 1000);
    shop.service.add_product("B-1", "Gadget", 50, 10, 250);

    CHECK(shop.service.low_stock().size() == 1);  // Widget: 2 <= 3
    CHECK(shop.service.receive_stock("A-1", 10).ok);
    CHECK(shop.service.low_stock().empty());
    CHECK(!shop.service.receive_stock("A-1", 0).ok);
    CHECK(!shop.service.receive_stock("nope", 5).ok);

    // 12 * 10.00 + 50 * 2.50 = 245.00
    CHECK(shop.service.stock_value_cents() == 24500);
}

static void test_orders() {
    Shop shop;
    shop.service.add_product("A-1", "Widget", 5, 1, 1000);
    shop.service.add_product("B-1", "Gadget", 1, 1, 500);

    CHECK(!shop.service.create_order({}).ok);
    CHECK(!shop.service.create_order({{"zzz", 1}}).ok);
    CHECK(!shop.service.create_order({{"A-1", 0}}).ok);

    CHECK(shop.service.create_order({{"A-1", 2}, {"B-1", 1}}).ok);
    CHECK(shop.service.fulfill_order(1).ok);
    CHECK(shop.products.find_by_sku("A-1")->quantity == 3);
    CHECK(shop.products.find_by_sku("B-1")->quantity == 0);
    CHECK(!shop.service.fulfill_order(1).ok);  // already fulfilled
    CHECK(!shop.service.cancel_order(1).ok);

    // An order with one short line changes nothing.
    CHECK(shop.service.create_order({{"A-1", 1}, {"B-1", 1}}).ok);
    CHECK(!shop.service.fulfill_order(2).ok);
    CHECK(shop.products.find_by_sku("A-1")->quantity == 3);

    CHECK(shop.service.cancel_order(2).ok);
    CHECK(shop.orders.find(2)->status == OrderStatus::Cancelled);
    CHECK(!shop.service.fulfill_order(99).ok);
}

static void test_controllers() {
    Shop shop;
    CHECK(shop.router.dispatch("product add A-1 \"Blue Widget\" 10 3 1999").ok);
    CHECK(!shop.router.dispatch("product add A-1 Widget ten 3 1999").ok);  // not a number
    CHECK(!shop.router.dispatch("product add A-1").ok);                    // too few words

    Result shown = shop.router.dispatch("product show A-1");
    CHECK(shown.ok);
    CHECK(shown.message.find("Blue Widget") != std::string::npos);
    CHECK(shown.message.find("19.99") != std::string::npos);

    CHECK(shop.router.dispatch("stock receive A-1 5").ok);
    CHECK(shop.router.dispatch("stock value").message.find("299.85") != std::string::npos);  // 15 * 19.99
    CHECK(shop.router.dispatch("stock low").message.find("nothing") != std::string::npos);

    CHECK(shop.router.dispatch("order create A-1:4").ok);
    CHECK(!shop.router.dispatch("order create A-1").ok);  // missing :quantity
    CHECK(shop.router.dispatch("order fulfill 1").ok);
    CHECK(shop.router.dispatch("order list").message.find("fulfilled") != std::string::npos);
    CHECK(!shop.router.dispatch("order fulfill abc").ok);

    CHECK(!shop.router.dispatch("nonsense").ok);
    CHECK(shop.router.dispatch("help").ok);
    CHECK(shop.router.dispatch("").ok);
}

// ---------------------------------------------------------------------------------------------------------------
// Catalog maintenance and CSV exchange

static void test_adjust_stock() {
    Shop shop;
    shop.service.add_product("A-1", "Widget", 10, 3, 1000);

    CHECK(shop.service.adjust_stock("A-1", 5, "found in the back").ok);
    CHECK(shop.products.find_by_sku("A-1")->quantity == 15);
    CHECK(shop.service.adjust_stock("A-1", -4, "damaged").ok);
    CHECK(shop.products.find_by_sku("A-1")->quantity == 11);
    CHECK(!shop.service.adjust_stock("nope", 5, "found").ok);
}

static void test_pricing() {
    Shop shop;
    shop.service.add_product("A-1", "Widget", 1, 0, 2000);

    CHECK(shop.service.set_price("A-1", 2500).ok);
    CHECK(shop.products.find_by_sku("A-1")->price_cents == 2500);

    CHECK(shop.service.apply_discount("A-1", 20).ok);
    CHECK(shop.products.find_by_sku("A-1")->price_cents == 2000);
}

static void test_remove_product() {
    Shop shop;
    shop.service.add_product("A-1", "Widget", 5, 1, 1000);

    CHECK(shop.service.remove_product("A-1").ok);
    CHECK(!shop.products.find_by_sku("A-1").has_value());
}

static void test_reorder_suggestions() {
    Shop shop;
    shop.service.add_product("A-1", "Widget", 2, 3, 1000);
    shop.service.add_product("C-1", "Gizmo", 50, 10, 500);

    const std::vector<ReorderSuggestion> suggestions = shop.service.reorder_suggestions();
    CHECK(suggestions.size() == 1);
    CHECK(suggestions[0].sku == "A-1" && suggestions[0].quantity == 5);
}

static void test_csv() {
    Shop shop;
    shop.service.add_product("A-1", "Widget", 10, 3, 1999);
    shop.service.add_product("B-1", "Gadget", 4, 1, 250);

    Result exported = export_products_csv(shop.service, "test_export.csv");
    CHECK(exported.ok);

    Shop other;
    Result imported = import_products_csv(other.service, "test_export.csv");
    CHECK(imported.ok);
    CHECK(other.products.find_by_sku("A-1")->price_cents == 1999);
    CHECK(other.products.find_by_sku("B-1")->quantity == 4);
    std::remove("test_export.csv");
}

static void test_catalog_controllers() {
    Shop shop;
    CHECK(shop.router.dispatch("product add A-1 Widget 2 3 1999").ok);
    CHECK(shop.router.dispatch("product price A-1 2500").ok);
    CHECK(shop.router.dispatch("product discount A-1 20").ok);
    CHECK(shop.router.dispatch("product show A-1").message.find("20.00") != std::string::npos);
    CHECK(shop.router.dispatch("stock adjust A-1 4 \"found in the back\"").ok);
    CHECK(shop.router.dispatch("stock reorder").ok);
}

int main() {
    test_util();
    test_products();
    test_stock();
    test_orders();
    test_controllers();
    test_adjust_stock();
    test_pricing();
    test_remove_product();
    test_reorder_suggestions();
    test_csv();
    test_catalog_controllers();
    if (failures == 0) {
        std::cout << "all tests passed\n";
        return 0;
    }
    std::cerr << failures << " check(s) failed\n";
    return 1;
}
