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

Publish the pinned libssig commit before publishing a CRSign commit that points
to it, so remote recursive checkouts can retrieve the dependency. No generated
certificates, private keys or personal test documents are committed.

The `bundles_cli` test builds separate synthetic embedded hierarchies and checks
system priority, selection, expired roots, DER intermediate merging and a local
TSA without card access. `cert_catalog` audits the shipped public CA certificates,
including their DER fingerprints and chains, and tests generator rejection paths.
The installed Sign binary only includes the reviewed public catalogue.
