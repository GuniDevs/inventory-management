# Demo branch: deliberately flawed. Do not merge.

This branch adds the same features as `feature/stock-adjustments-and-csv-exchange` (catalog maintenance and CSV exchange), but it is
written badly on purpose so InfraOps has something to find. It builds and its tests pass. Never copy code from here.

## What InfraOps should show

| Check | Expected |
| --- | --- |
| Build | passes (it compiles, with one `-Wformat-security` warning) |
| SAST | several high findings (see "Planted security problems") |
| Quality | high findings: complexity, a memory leak, an uninitialised value, copies, repeated code |
| Test coverage | partly tested: happy paths only, some functions and error paths have no test |

## Planted security problems

| Where | Problem | CWE |
| --- | --- | --- |
| `csv_io.cpp` `backup_existing` | the file name goes into a shell command (`system`), so a name like `a.csv; rm -rf x` runs code | 78 |
| `csv_io.cpp` `backup_existing` | the zip password is written in the source (`kBackupPassword`) | 798 |
| `csv_io.cpp` export and import | the path is used as given: no check for `..` or absolute paths | 22 |
| `csv_io.cpp` `export_products_csv` | `sprintf` into a 128 byte buffer; a long name overflows it | 120 |
| `csv_io.cpp` `import_products_csv` | `atoi` with no range check; `strtok`; fixed 256 byte line buffer | 190, 120 |
| `util.cpp` `format_reorder_line` | `strcpy` and `sprintf` into 16 and 48 byte buffers; a long SKU overflows them | 120 |
| `util.cpp` `make_batch_id` | `rand()` seeded with the clock, `strcpy` and `strcat` into a 24 byte buffer | 338, 120 |
| `main.cpp` | the result text, which contains what the user typed, is used as a `printf` format string | 134 |

## Planted quality problems

- `ProductController::handle` has cyclomatic complexity 25 (limit 15) and `import_products_csv` has 22.
- `register_product` takes 8 parameters (limit 6).
- `remove_product` leaks memory on the open-order path (`new` without `delete`).
- `apply_discount` reads an uninitialised value when the percent is outside 1 to 90, and never says so.
- `adjust_stock` is five levels deep with `else` after every `return`.
- Loops copy `Product`, `Order` and `OrderLine` objects, and `.size() > 0` is used instead of `.empty()`.

## Planted test gaps

The tests cover the normal case of each function. Not tested: `register_product` (no test at all); `adjust_stock` with a
zero change, no reason, or a result below zero; `set_price` with a negative price or unknown SKU; `apply_discount` with an
out-of-range percent or unknown SKU (the uninitialised-value path); `remove_product` with an unknown SKU or an open order
(the leak path); `reorder_suggestions` with nothing low; export to a file that cannot be opened; import of a missing file,
bad rows or negative numbers; every usage error of the new commands; the backup branch of `export`.

Measured on Ubuntu 24.04 with g++ and gcovr: 76 percent of lines run, against 96 percent on the clean branch.
