# Sign tests

Initialize the pinned suite and its nested ETSI schema modules:

```sh
git submodule update --init --recursive
nix develop --command cmake -S tests/libssig -B build-libssig -DBUILD_TESTING=OFF
nix develop --command cmake --build build-libssig --parallel
nix develop --command cmake -S . -B build -Dlibssig_DIR="$PWD/build-libssig" -DHEADLESS=ON
nix develop --command cmake --build build --parallel
nix develop --command ctest --test-dir build --parallel 4 --output-on-failure
```

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
- `profiles_cli` checks TOML versions and schema, XDG/HOME lookup, empty profiles,
  CLI overrides (including default-valued overrides), relative certificate paths,
  and signing with automatic token/certificate and PDF format selection.
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
The CI workflow checks both headless and GTK builds without opening a window.

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
