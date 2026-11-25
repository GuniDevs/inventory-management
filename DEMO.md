# Demo branch: clean code, full test coverage

This branch adds catalog maintenance and CSV exchange to the inventory app, written the way we want pull requests to look.
It is one of two demo branches for InfraOps. The other one, `feature/csv-backup-and-bulk-import`, adds the same features with
deliberate problems.

## What the pull request adds

| Function | File |
| --- | --- |
| `InventoryService::adjust_stock`, `set_price`, `apply_discount`, `remove_product`, `reorder_suggestions` | `src/inventory_service.cpp` |
| `ProductRepository::remove` | `src/repository.cpp` |
| `escape_csv`, `split_csv_line`, `is_safe_csv_path` | `src/util.cpp` |
| `export_products_csv`, `import_products_csv` | `src/csv_io.cpp` |
| `product price / discount / remove / export / import`, `stock adjust / reorder` | the controllers |

## What InfraOps should show

| Check | Expected |
| --- | --- |
| Build | passes (CMake, no outside libraries) |
| SAST | nothing expected: no unsafe C functions, no shell calls, file paths are checked, sizes are limited (flawfinder reports nothing) |
| Quality | only minor findings: `misc-const-correctness` and `readability-use-anyofallof` in the new code, plus an enum size hint and two "unchecked optional" notes in `fulfill_order` that are already on `main`. No function is over the complexity, length or parameter limits |
| Test coverage | every new function and every path of it (error cases, limits, round trips) has a test |

Measured on Ubuntu 24.04 with g++ and gcovr: all new code is executed by the tests; the only lines not run are the
interactive loop in `main()` and a few closing braces that gcov attributes to exception cleanup.
