# Tests

Uses Catch2 v3 test framework. Each test is a separate executable target defined in the root CMakeLists.txt via `add_test(target_name test_file)`.

## Test placement rule

Test files must mirror the `src/` subfolder structure. A test for source in `src/<component>/` goes to `test/<component>/`. Provider-specific tests (e.g. ByBit) use `test/bybit/` matching `src/data/bybit/`.

Do not add a new test source per case. First look for an existing test source in that folder that already covers the same class or pipeline stage and add the case there; when the existing name is too narrow for what the file now covers, rename it to the more general name (`test_sorted_data_feed.cpp` → `test_data_feed.cpp`) and retarget the `add_unit_test` line. A new file is only justified when no existing source in the folder shares the fixture or the subject.

## Adding a new test

In root `CMakeLists.txt` add a single line inside the `if(NOT ANDROID)` block:
```cmake
add_test(test_<name> test/<component>/test_<name>.cpp)
```
The `add_test` CMake function creates an executable, links `core`, `Catch2::Catch2WithMain`, and all common dependencies automatically.

## Building a single test

```sh
cmake --build cmake-build-debug-clang-19 --target test_<name>
```

## Running a test

```sh
./cmake-build-debug-clang-19/test_<name>
```

Catch2 CLI options apply, e.g. `--list-tests`, `-c "section name"`, `[tag]`.

## Naming and tags

- Name a `TEST_CASE` / `TEMPLATE_TEST_CASE` with a minimal noun phrase for the behaviour under test (`"persisted feed"`, `"subscription condition"`). The file already scopes the component; do not prefix the name with the class under test and do not restate the assertion.
- Tags carry exactly two entries: the component tag (`[datahub]`, `[connect]`, `[engine]`, `[cockpit]`, `[render]`) and the requirement UID last (`[DATAHUB-023]`). No feature or adjective tags — the requirement id is the classifier.
- `SECTION` names are minimal scenario labels (`"runtime records"`, `"cached records"`, `"persisted records"`).
- A requirement binds to exactly one routine (see `req/README.md`). Cover type variants with one `TEMPLATE_TEST_CASE` over the types and scenarios with `SECTION`s inside it, sharing file-scope test vectors (`seed`, `tail`), instead of adding tagged `TEST_CASE`s per variant.
- Comments only where a test vector or fixture encodes a non-obvious invariant; the name and the assertions are the description.
