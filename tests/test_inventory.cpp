#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
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

static void write_file(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

static std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

static void test_csv_helpers() {
    CHECK(escape_csv("Widget") == "Widget");
    CHECK(escape_csv("a,b") == "\"a,b\"");
    CHECK(escape_csv("say \"hi\"") == "\"say \"\"hi\"\"\"");
    CHECK(escape_csv("line\nbreak") == "\"line\nbreak\"");
    CHECK(escape_csv("") == "");
    CHECK(escape_csv("=SUM(A1)") == "'=SUM(A1)");
    CHECK(escape_csv("+1") == "'+1");
    CHECK(escape_csv("-1") == "'-1");
    CHECK(escape_csv("@cmd") == "'@cmd");

    std::vector<std::string> fields;
    CHECK(split_csv_line("a,b,c", fields) && fields.size() == 3 && fields[2] == "c");
    CHECK(split_csv_line("a,\"b,1\",c", fields) && fields.size() == 3 && fields[1] == "b,1");
    CHECK(split_csv_line("\"say \"\"hi\"\"\",x", fields) && fields[0] == "say \"hi\"");
    CHECK(split_csv_line("a,,c", fields) && fields.size() == 3 && fields[1].empty());
    CHECK(split_csv_line("", fields) && fields.size() == 1);
    CHECK(!split_csv_line("a,\"never closed", fields));

    CHECK(is_safe_csv_path("stock.csv"));
    CHECK(is_safe_csv_path("exports/stock.csv"));
    CHECK(!is_safe_csv_path(""));
    CHECK(!is_safe_csv_path(".csv"));
    CHECK(!is_safe_csv_path("stock.txt"));
    CHECK(!is_safe_csv_path("/etc/stock.csv"));
    CHECK(!is_safe_csv_path("\\share\\stock.csv"));
    CHECK(!is_safe_csv_path("C:stock.csv"));
    CHECK(!is_safe_csv_path("../stock.csv"));
    CHECK(!is_safe_csv_path("a/../../stock.csv"));
    CHECK(!is_safe_csv_path(std::string("bad\x01name.csv")));
    CHECK(!is_safe_csv_path(std::string(252, 'a') + ".csv"));  // 256 characters
    CHECK(is_safe_csv_path(std::string(251, 'a') + ".csv"));   // 255 characters
}

static void test_repository_remove() {
    ProductRepository products;
    Product product;
    product.sku = "A-1";
    const int id = products.add(product);
    CHECK(!products.remove(id + 1));
    CHECK(products.remove(id));
    CHECK(!products.find_by_id(id).has_value());
}

static void test_adjust_stock() {
    Shop shop;
    shop.service.add_product("A-1", "Widget", 10, 3, 1000);
    shop.service.add_product("MAX", "Full shelf", 2147483647, 0, 1);

    CHECK(!shop.service.adjust_stock("A-1", 0, "nothing").ok);
    CHECK(!shop.service.adjust_stock("A-1", 5, "").ok);
    CHECK(!shop.service.adjust_stock("nope", 5, "found").ok);
    CHECK(!shop.service.adjust_stock("A-1", -11, "lost").ok);  // would go below zero
    CHECK(shop.products.find_by_sku("A-1")->quantity == 10);
    CHECK(!shop.service.adjust_stock("MAX", 1, "found").ok);   // would not fit in an int

    Result found = shop.service.adjust_stock("A-1", 5, "found in the back");
    CHECK(found.ok);
    CHECK(found.message.find("found in the back") != std::string::npos);
    CHECK(shop.products.find_by_sku("A-1")->quantity == 15);
    CHECK(shop.service.adjust_stock("A-1", -15, "damaged").ok);  // all the way to zero is allowed
    CHECK(shop.products.find_by_sku("A-1")->quantity == 0);
}

static void test_pricing() {
    Shop shop;
    shop.service.add_product("A-1", "Widget", 1, 0, 1999);
    shop.service.add_product("BIG", "Gold bar", 1, 0, 9000000000000000000LL);

    CHECK(!shop.service.set_price("A-1", -1).ok);
    CHECK(!shop.service.set_price("nope", 100).ok);
    CHECK(shop.service.set_price("A-1", 2000).ok);
    CHECK(shop.products.find_by_sku("A-1")->price_cents == 2000);
    CHECK(shop.service.set_price("A-1", 0).ok);  // free is allowed

    CHECK(!shop.service.apply_discount("A-1", 0).ok);
    CHECK(!shop.service.apply_discount("A-1", 91).ok);
    CHECK(!shop.service.apply_discount("nope", 10).ok);
    shop.service.set_price("A-1", 1999);
    CHECK(shop.service.apply_discount("A-1", 10).ok);
    CHECK(shop.products.find_by_sku("A-1")->price_cents == 1799);  // 19.99 less 10 percent, rounded down
    CHECK(shop.service.apply_discount("A-1", 90).ok);
    CHECK(shop.products.find_by_sku("A-1")->price_cents == 179);
    CHECK(shop.service.apply_discount("BIG", 50).ok);  // no overflow on a huge price
    CHECK(shop.products.find_by_sku("BIG")->price_cents == 4500000000000000000LL);
}

static void test_remove_product() {
    Shop shop;
    shop.service.add_product("A-1", "Widget", 5, 1, 1000);
    shop.service.add_product("B-1", "Gadget", 5, 1, 500);
    shop.service.add_product("C-1", "Gizmo", 5, 1, 500);

    CHECK(!shop.service.remove_product("nope").ok);

    shop.service.create_order({{"A-1", 1}});  // order 1, open
    Result blocked = shop.service.remove_product("A-1");
    CHECK(!blocked.ok);
    CHECK(blocked.message.find("open order 1") != std::string::npos);

    CHECK(shop.service.create_order({{"B-1", 1}}).ok);  // order 2
    CHECK(shop.service.fulfill_order(2).ok);
    CHECK(shop.service.remove_product("B-1").ok);  // a fulfilled order does not block
    CHECK(!shop.products.find_by_sku("B-1").has_value());

    CHECK(shop.service.create_order({{"C-1", 1}}).ok);  // order 3
    CHECK(shop.service.cancel_order(3).ok);
    CHECK(shop.service.remove_product("C-1").ok);  // a cancelled order does not block

    CHECK(shop.service.cancel_order(1).ok);
    CHECK(shop.service.remove_product("A-1").ok);
    CHECK(shop.products.all().empty());
}

static void test_reorder_suggestions() {
    Shop shop;
    CHECK(shop.service.reorder_suggestions().empty());
    shop.service.add_product("A-1", "Widget", 2, 3, 1000);   // low: target 7, order 5
    shop.service.add_product("B-1", "Gadget", 0, 0, 500);    // low: target 1, order 1
    shop.service.add_product("C-1", "Gizmo", 50, 10, 500);   // fine

    const std::vector<ReorderSuggestion> suggestions = shop.service.reorder_suggestions();
    CHECK(suggestions.size() == 2);
    CHECK(suggestions[0].sku == "A-1" && suggestions[0].quantity == 5);
    CHECK(suggestions[1].sku == "B-1" && suggestions[1].quantity == 1);
}

static void test_csv_export() {
    Shop shop;
    CHECK(!export_products_csv(shop.service, "../out.csv").ok);
    CHECK(!export_products_csv(shop.service, "no_such_folder/out.csv").ok);  // cannot be opened

    shop.service.add_product("A-1", "Blue, \"big\" Widget", 10, 3, 1999);
    shop.service.add_product("B-1", "=HYPERLINK(1)", 0, 0, 5);
    Result exported = export_products_csv(shop.service, "test_export.csv");
    CHECK(exported.ok);
    CHECK(exported.message.find("2 product(s)") != std::string::npos);
    CHECK(read_file("test_export.csv") ==
          "sku,name,quantity,reorder_level,price_cents\n"
          "A-1,\"Blue, \"\"big\"\" Widget\",10,3,1999\n"
          "B-1,'=HYPERLINK(1),0,0,5\n");
    std::remove("test_export.csv");
}

static void test_csv_import() {
    Shop shop;
    CHECK(!import_products_csv(shop.service, "/etc/passwd.csv").ok);
    CHECK(!import_products_csv(shop.service, "missing_file.csv").ok);

    write_file("test_import.csv", "wrong header\n");
    CHECK(!import_products_csv(shop.service, "test_import.csv").ok);
    write_file("test_import.csv", "");  // an empty file has no header either
    CHECK(!import_products_csv(shop.service, "test_import.csv").ok);

    // Good rows, a blank line, Windows line endings and a name that starts with a ' for a formula.
    write_file("test_import.csv",
               "sku,name,quantity,reorder_level,price_cents\r\n"
               "A-1,\"Blue, Widget\",10,3,1999\r\n"
               "\r\n"
               "B-1,'=HYPERLINK(1),0,0,5\r\n");
    Result good = import_products_csv(shop.service, "test_import.csv");
    CHECK(good.ok);
    CHECK(good.message == "imported 2 product(s)");
    CHECK(shop.products.find_by_sku("A-1")->name == "Blue, Widget");
    CHECK(shop.products.find_by_sku("B-1")->name == "=HYPERLINK(1)");

    // Bad rows are skipped and counted; the first one is named.
    write_file("test_import.csv",
               "sku,name,quantity,reorder_level,price_cents\n"
               "C-1,Only,3\n"                  // wrong field count
               "D-1,Name,ten,1,100\n"          // not a number
               "A-1,Again,1,1,100\n"           // duplicate sku
               "E-1,\"never closed,1,1,100\n"  // open quote
               "F-1,Fine,1,1,100\n");
    Result mixed = import_products_csv(shop.service, "test_import.csv");
    CHECK(!mixed.ok);
    CHECK(mixed.message.find("imported 1 product(s), rejected 4") != std::string::npos);
    CHECK(mixed.message.find("row 1: expected 5 fields") != std::string::npos);
    CHECK(shop.products.find_by_sku("F-1").has_value());

    // A row longer than the limit is rejected without being parsed.
    write_file("test_import.csv",
               "sku,name,quantity,reorder_level,price_cents\nG-1," + std::string(2000, 'x') + ",1,1,100\n");
    Result longer = import_products_csv(shop.service, "test_import.csv");
    CHECK(!longer.ok && longer.message.find("line is too long") != std::string::npos);

    // More rows than the limit stops the import.
    std::string many = "sku,name,quantity,reorder_level,price_cents\n";
    for (int i = 0; i < 10001; i++) {
        many += "S" + std::to_string(i) + ",N,1,1,1\n";
    }
    write_file("test_import.csv", many);
    Result too_many = import_products_csv(shop.service, "test_import.csv");
    CHECK(!too_many.ok && too_many.message.find("too many rows") != std::string::npos);
    std::remove("test_import.csv");
}

static void test_catalog_controllers() {
    Shop shop;
    CHECK(shop.router.dispatch("product add A-1 Widget 2 3 1999").ok);

    CHECK(shop.router.dispatch("product price A-1 2500").ok);
    CHECK(shop.router.dispatch("product show A-1").message.find("25.00") != std::string::npos);
    CHECK(!shop.router.dispatch("product price A-1").ok);
    CHECK(!shop.router.dispatch("product price A-1 cheap").ok);

    CHECK(shop.router.dispatch("product discount A-1 20").ok);
    CHECK(shop.router.dispatch("product show A-1").message.find("20.00") != std::string::npos);
    CHECK(!shop.router.dispatch("product discount A-1").ok);
    CHECK(!shop.router.dispatch("product discount A-1 lots").ok);

    // Quantity 2 with reorder level 3: order enough to reach 7.
    CHECK(shop.router.dispatch("stock reorder").message.find("order 5 of A-1") != std::string::npos);
    CHECK(shop.router.dispatch("stock adjust A-1 4 \"found in the back\"").ok);
    CHECK(!shop.router.dispatch("stock adjust A-1 4").ok);
    CHECK(!shop.router.dispatch("stock adjust A-1 many \"why\"").ok);
    CHECK(shop.router.dispatch("stock reorder").message.find("nothing") != std::string::npos);  // 6 is above 3

    CHECK(shop.router.dispatch("product export controller_test.csv").ok);
    CHECK(!shop.router.dispatch("product export").ok);
    CHECK(shop.router.dispatch("product remove A-1").ok);
    CHECK(!shop.router.dispatch("product remove").ok);
    CHECK(shop.router.dispatch("product import controller_test.csv").ok);
    CHECK(!shop.router.dispatch("product import").ok);
    std::remove("controller_test.csv");

    CHECK(!shop.router.dispatch("product").ok);
    CHECK(!shop.router.dispatch("product frobnicate").ok);
    CHECK(!shop.router.dispatch("stock").ok);
    CHECK(!shop.router.dispatch("stock frobnicate").ok);
    CHECK(!shop.router.dispatch("order").ok);
    CHECK(!shop.router.dispatch("order frobnicate").ok);
    CHECK(shop.router.dispatch("order list").message.find("no orders") != std::string::npos);
    CHECK(shop.router.dispatch("product list").message.find("A-1") != std::string::npos);
    CHECK(!shop.router.dispatch("product show").ok);
    CHECK(!shop.router.dispatch("product show nope").ok);
    CHECK(!shop.router.dispatch("order fulfill").ok);
    CHECK(shop.router.dispatch("help").message.find("stock adjust") != std::string::npos);
    CHECK(to_string(static_cast<OrderStatus>(99)) == "unknown");
}

// Paths of the original code that had no test yet.
static void test_original_gaps() {
    Shop shop;
    CHECK(shop.router.dispatch("product list").message == "no products yet");
    CHECK(shop.router.dispatch("order list").message == "no orders yet");
    CHECK(!shop.service.cancel_order(7).ok);  // unknown order

    Product ghost;
    ghost.id = 42;
    CHECK(!shop.products.update(ghost));  // unknown product
    Order phantom;
    phantom.id = 42;
    CHECK(!shop.orders.update(phantom));  // unknown order

    CHECK(shop.router.dispatch("product add A-1 Widget 1 5 100").ok);
    CHECK(!shop.router.dispatch("stock receive A-1").ok);
    CHECK(!shop.router.dispatch("stock receive A-1 many").ok);
    CHECK(shop.router.dispatch("stock low").message.find("A-1  qty 1 (reorder at 5)") != std::string::npos);

    CHECK(shop.router.dispatch("order create A-1:1").ok);
    CHECK(shop.router.dispatch("order create A-1:1").ok);
    CHECK(shop.router.dispatch("order cancel 2").ok);
    const std::string orders = shop.router.dispatch("order list").message;
    CHECK(orders.find("order 1  open") != std::string::npos);
    CHECK(orders.find("order 2  cancelled") != std::string::npos);
    CHECK(!shop.router.dispatch("order create").ok);  // no lines: the service refuses
}

int main() {
    test_util();
    test_products();
    test_stock();
    test_orders();
    test_controllers();
    test_csv_helpers();
    test_repository_remove();
    test_adjust_stock();
    test_pricing();
    test_remove_product();
    test_reorder_suggestions();
    test_csv_export();
    test_csv_import();
    test_catalog_controllers();
    test_original_gaps();
    if (failures == 0) {
        std::cout << "all tests passed\n";
        return 0;
    }
    std::cerr << failures << " check(s) failed\n";
    return 1;
}
