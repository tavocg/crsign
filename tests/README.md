# Sign tests

Tests are disabled by default. Enable them explicitly with `-DBUILD_TESTING=ON`.
GTK is enabled by default; add `-DHEADLESS=ON` for a CLI-only test build.
Initialize the pinned suite and its nested ETSI schema modules:

```sh
git submodule update --init --recursive
nix develop --command cmake -S tests/libssig -B build-libssig -DBUILD_TESTING=OFF -DBUILD_SHARED_LIBS=OFF
nix develop --command cmake --build build-libssig --parallel
nix develop --command cmake -S . -B build -Dlibssig_DIR="$PWD/build-libssig" -DBUILD_TESTING=ON
nix develop --command cmake --build build --parallel
nix develop --command ctest --test-dir build --parallel 4 --output-on-failure
```

CRSign uses static libssig, tomlc17, Jansson and zlib; external argp is static
as well. The Nix shell supplies the small static archives. Native builds prepare
them with `scripts/bootstrap-native.py`. CI has one GTK build per platform,
without a shared/static matrix. `static_linkage` inspects the executable's actual
direct imports to catch accidental dynamic linkage of these libraries.

All registration lives in `tests/CMakeLists.txt`. The `tests/libssig` submodule
pins the same implementation and reusable tests. It contains the nested official
ETSI schema submodules; no schemas, packages or repositories are downloaded by
CMake or CTest. The Nix flake provides Python and native test dependencies. Other
environments can install `tests/libssig/tests/requirements.txt` and select their
interpreter with `Python3_EXECUTABLE`.

- `commands_cli` checks command dispatch and JSON with a hardware substitute.
- `arguments_cli` checks Unicode arguments, attached short options, long options,
  repeated values, `--`, help/version and profile precedence with either argp
  implementation. The CMake `Argp::Argp` target uses libc or an external library.
- `terminal_pin` uses real POSIX PTYs to check masked ASCII/UTF-8 input,
  backspace/line erase, empty and oversized input, redirected-input rejection,
  EOF, Ctrl+C and terminal restoration after signals.
- `pin_signing` checks a complete signing operation with a simulated token that
  requires a PIN, signing failure and protected authentication paths. These
  tests use only fixed fake PINs and never access a real card.
- `pincmd_cli` checks global/profile commands, string/argv syntax, inheritance,
  disabling the command, UTF-8/CRLF output, length limits, command failures,
  redirected input isolation, privacy of diagnostics and protected token paths.
  CLI tests isolate `XDG_CONFIG_HOME` so user PIN helpers are never executed.
- `file_io` checks native output paths, Unicode filenames, binary/empty files,
  exclusive creation and destination symlinks. On Unix it also checks private
  permissions, rejection of oversized sparse inputs and cleanup after an actual
  partial write forced by `RLIMIT_FSIZE`.
- `modules_cli` loads real fixture libraries through p11-kit and checks default
  discovery, global/profile inheritance, multiple modules, relative Unicode paths,
  duplicate paths, repeatable `-m`/`--module` overrides, signing/listing consistency
  and cleanup after partial failure.
  The library's `context_modules` test checks managed loading lifecycle and invalid
  API arguments independently. These fixtures contain no real keys or PINs.
- `profiles_cli` checks TOML versions and schema, XDG/HOME lookup, empty profiles,
  CLI overrides (including default-valued overrides), relative certificate paths,
  and signing with automatic token/certificate and PDF format selection.
- `appearance_cli` decodes SVG, PNG and JPEG, checks EXIF orientation, aspect
  ratio, placement limits, default certificate text/RFC 3339 dates, profile image
  defaults, CLI overrides and rejection of TOML geometry, then signs and verifies real PDF signatures without a card.
- `pdf_appearance` verifies real signatures on rotated/cropped pages, UserUnit,
  existing annotations, selected page, failure cleanup and appearance tampering.
  `pdf_appearance_render` also checks orientation, coordinates and transparency
  through Poppler when `pdftoppm` is installed (included in the Nix shell).
- `trust_cli` runs the actual CLI against local HTTP TSA/OCSP services and checks
  trust, transport errors, levels and failure preservation. It needs localhost
  sockets, not internet access or a physical token.
- The pinned libssig tests include fixture generation, official schema checks,
  independent signature/timestamp validation and archive renewal.
- `signatures_cli` consumes those fixtures with the real Sign verifier for all
  four formats/levels, text/JSON, renewal, wrong roots, changed content and
  missing evidence. Detached files fail clearly because Sign currently has no
  CLI argument to supply the external content.

CTest fixtures automatically generate inputs when selecting `-R signatures_cli`
or `-L conformance`, including parallel runs. Minimal CLI tests are available
with `SIGN_TEST_LIBSSIG=OFF`; disabling `BUILD_TESTING` needs no test submodules.
CI builds the complete GTK application on Linux, Windows and macOS without opening a window.

To exercise argp-standalone even on glibc, configure a separate build with
`-DARGP_INCLUDE_DIR=/path/include -DARGP_LIBRARY=/path/lib/libargp.a`, build it,
and run `ctest --test-dir BUILD -R 'arguments_cli|profiles_cli|commands_cli'
--output-on-failure`. This checks the external implementation on the host; it
does not replace native Windows/macOS execution.

The file layer also has a standalone suite that requires only a C compiler,
CMake, pkg-config and GLib/GIO, so it can be built on Windows/macOS before the
rest of the CLI is ported:

```sh
cmake -S tests/io -B build/io
cmake --build build/io
ctest --test-dir build/io --output-on-failure
```

Windows path cases cover drive-relative, UNC and mixed-separator paths. Unix
cases verify that backslashes remain ordinary filename characters. The suite
uses the actual platform's GLib behavior; running on Linux does not execute the
Windows cases. Windows symlink checks can be skipped when privileges or
filesystem support prevent creating links.

PIN tests also build independently of libssig/GTK with GLib/GIO available:

```sh
cmake -S tests/terminal -B build/terminal
cmake --build build/terminal
ctest --test-dir build/terminal --output-on-failure
```

The PTY tests are registered on Unix only. The Windows console branch can be
compiled with the standalone target; native interactive verification remains
required on Windows. `PIN:` is written to stderr so JSON stdout stays parseable.

Publish the pinned libssig commit before publishing a CRSign commit that points
to it, so remote recursive checkouts can retrieve the dependency. No generated
certificates, private keys or personal test documents are committed.

The `bundles_cli` test builds separate synthetic embedded hierarchies and checks
system priority, selection, expired roots, DER intermediate merging and a local
TSA without card access. `cert_catalog` audits the shipped public CA certificates,
including their DER fingerprints and chains, and tests generator rejection paths.
The installed Sign binary only includes the reviewed public catalogue.

The system-trust layer can also be tested without PKCS#11, QPDF or GTK:

```sh
cmake -S tests/libssig/tests/system_trust -B build/system-trust -DCMAKE_BUILD_TYPE=Release
cmake --build build/system-trust --parallel
ctest --test-dir build/system-trust --output-on-failure
```

This suite requires OpenSSL >= 3.5; set `OPENSSL_ROOT_DIR` if it is not found.
`system_trust` injects native trust decisions while exercising real OpenSSL
chains: native rejection, explicit-root precedence, historical dates, timestamp
purpose, missing issuers, expired certificates, callbacks and DER handling.
On Windows/macOS, `system_trust_os` additionally compiles and runs the real
Crypt32/Security backend against the existing public system stores, rejects an
unknown self-signed certificate and checks an explicit anchor. It installs no
certificates, changes no trust settings and accesses no private keys.
The native GTK workflow also builds and runs these standalone tests on
Windows/MSYS2 UCRT64 and macOS with Homebrew OpenSSL.

`native_packaging` checks PE/Mach-O dependency parsing, closure and cycles,
missing/conflicting libraries, relative macOS references, signing of copied
files, archive checksums and rejection of unsafe source-archive paths. It runs
on any host without Windows/macOS tools. `portable_runtime` runs a relocated
resource driver from a directory with spaces and Unicode, checks image-cache
relocation, explicit resource overrides and cache read failures.

`native-build.yml` builds the complete GTK application (`HEADLESS=OFF`) on
Windows UCRT64 and macOS, and runs the CLI and native trust suites. `build.yml`
builds and tests the GTK application on Linux. Neither workflow packages binaries
or uploads artifacts. The packaging scripts remain available for manual use;
their package check verifies a real CMS/CAdES signature and opens GTK from a
relocated installation with development-library paths removed.
The POSIX PIN tests drain PTY output while
the child runs, inspect modes through the retained master, and feed bounded
input chunks to avoid overflowing BSD terminal queues. Loopback HTTP fixtures
avoid reverse DNS lookups. See
[packaging/README.md](../packaging/README.md) for dependencies and artifact layout.

The GTK build also registers `gui_i18n_en` and `gui_i18n_es` to check real gettext
catalog loading without opening a window. `gui_i18n_installed` verifies the
catalog path and translations after relocating an executable into an installation prefix. On Linux with `xvfb-run` and
`dbus-run-session`, `gui_navigation` creates a real GTK window in a virtual
session and checks all five views, the collapsed sidebar toggle, automatic
closing on selection and restoration when widened. The Nix shell includes Xvfb.
The navigation executable is compiled on native platforms as well.

`flows` checks strict JSON parsing/schema, canonical paths and serialization,
PIN/configuration isolation, sequential stop-on-error, exclusive outputs,
reentrant calls and actual contention between threads. `flow_signatures` uses an
OpenSSL-backed PKCS#11 fixture with the production flow executor to sign arbitrary
binary data as CAdES/XAdES/JAdES and PDF with/without an appearance; the real CLI
verifies every signature against a temporary explicit fixture anchor. Neither
flow test driver depends on argp or GTK. Their test-only PIN provider uses fixed
fake credentials and never opens a terminal.
