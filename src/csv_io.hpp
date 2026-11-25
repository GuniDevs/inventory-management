#pragma once

#include <string>

#include "inventory_service.hpp"
#include "models.hpp"

namespace inventory {

// Writes every product to a CSV file: sku,name,quantity,reorder_level,price_cents. The path must pass
// is_safe_csv_path (a plain relative name ending in .csv).
Result export_products_csv(const InventoryService& service, const std::string& path);

// Reads products from a CSV file in the same format and adds them. A bad row is skipped and counted; the message
// names the first one. The result is ok only when every row was added.
Result import_products_csv(InventoryService& service, const std::string& path);

}  // namespace inventory
