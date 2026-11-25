#pragma once

#include "controller.hpp"
#include "inventory_service.hpp"

namespace inventory {

// product add <sku> <name> <quantity> <reorder level> <price in cents>
// product list
// product show <sku>
// product price <sku> <price in cents>
// product discount <sku> <percent>
// product remove <sku>
// product export <file.csv>
// product import <file.csv>
class ProductController : public Controller {
public:
    explicit ProductController(InventoryService& service) : service_(service) {}

    std::string name() const override { return "product"; }
    std::string usage() const override;
    Result handle(const std::vector<std::string>& args) override;

private:
    Result add(const std::vector<std::string>& args);
    Result list() const;
    Result show(const std::vector<std::string>& args) const;
    Result price(const std::vector<std::string>& args);
    Result discount(const std::vector<std::string>& args);
    Result remove(const std::vector<std::string>& args);
    Result export_csv(const std::vector<std::string>& args) const;
    Result import_csv(const std::vector<std::string>& args);

    InventoryService& service_;
};

}  // namespace inventory
