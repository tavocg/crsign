# Repository Guidelines

## Project Structure & Module Organization

CRSign is a C99 digital-signature application with a CLI and GTK4/libadwaita GUI. Application code lives in `sign/`; `sign/gui/`, `sign/tokens/`, `sign/timestamp/`, and `sign/verify/` contain specialized modules. GUI resources include `sign/gui/style.css` and the GResource manifest. Translations live in `po/`, documentation in `doc/`, and native packaging helpers in `scripts/` and `packaging/`. Tests live in `tests/`; `tests/libssig/` is a pinned Git submodule supplying library code and reusable fixtures. Keep generated files in build directories.

## Build, Test, and Development Commands

Use `nix develop` for the Linux development dependencies, or install those listed in `doc/build.md`. Provide an installed libssig through `CMAKE_PREFIX_PATH` or `libssig_DIR`.

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/sign --gui
./build/sign tokens
```

GTK is enabled by default; add `-DHEADLESS=ON` for a CLI-only build. Tests default to disabled. Before running the full suite, initialize fixtures with `git submodule update --init --recursive` and follow `tests/README.md` to build the pinned libssig dependency. CMake does not download dependencies.

## Coding Style & Naming Conventions

Follow surrounding C99 code and format changed C files with `clang-format -i path/to/file.c`. The root `.clang-format` uses LLVM style, tabs for indentation, four-column indentation, and an 80-column limit. Use snake_case for functions, variables, and filenames, uppercase macros, and matching `.c`/`.h` module names. Preserve existing SPDX license headers.

## Testing Guidelines

CTest coordinates C test executables and Python integration scripts. Follow existing names such as `preferences_test.c`, `history_test.py`, and `arguments_cli.py`; register tests through the CMake files under `tests/`. Add regression coverage for changed behavior and run targeted tests with `ctest --test-dir build -R history --output-on-failure`, then the relevant broader suite. No numeric coverage threshold is documented. Use simulated tokens and temporary fixtures; physical-token signing is checked separately.

## Commit & Pull Request Guidelines

Recent commits use concise imperative subjects, such as “Compare normalized history paths portably in tests.” Keep commits focused. Pull requests should explain the behavior changed, relevant issues, and validation performed; include screenshots for visible GUI changes. Note platform-specific limitations. Publish libssig commits before updating the submodule pointer. Never commit generated private keys, PINs, certificates, or personal signing documents.
