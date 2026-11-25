#pragma once

#include "controller.hpp"
#include "inventory_service.hpp"

namespace inventory {

// stock receive <sku> <quantity>
// stock low
// stock value
// stock adjust <sku> <change> <reason>   (the change may be negative; put a reason with spaces in quotes)
// stock reorder
class StockController : public Controller {
public:
    explicit StockController(InventoryService& service) : service_(service) {}

    std::string name() const override { return "stock"; }
    std::string usage() const override;
    Result handle(const std::vector<std::string>& args) override;

private:
    Result adjust(const std::vector<std::string>& args);
    Result reorder() const;

    InventoryService& service_;
};

}  // namespace inventory
