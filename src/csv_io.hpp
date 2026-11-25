#pragma once

#include <string>

#include "inventory_service.hpp"
#include "models.hpp"

namespace inventory {

// Writes every product to a CSV file: sku,name,quantity,reorder_level,price_cents. An existing file is zipped as a
// backup first.
Result export_products_csv(const InventoryService& service, const std::string& path);

// Reads products from a CSV file in the same format and adds them. A bad row is skipped and counted.
Result import_products_csv(InventoryService& service, const std::string& path);

}  // namespace inventory
