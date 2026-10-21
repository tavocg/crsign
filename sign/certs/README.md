# Embedded certificate catalogue

Sign uses the OpenSSL system trust store first. Embedded intermediates are
untrusted chain-building candidates. Embedded roots are consulted locally when
primary issuer lookup cannot find the issuer; they do not override rejected,
expired or revoked chains. Document and TSA trust remain independent.

The initial `bccr` bundle contains the public national root and 12 policy/SINPE
intermediates from `fdcr/artifacts/bccr-certs-26.08`. Their original filenames,
extensions and bytes are preserved, including DER/PEM encoding and line endings.
CMake converts them into the PEM strings embedded in the executable; source
files are never rewritten. Code-signing certificates and
GlobalSign roots in the source snapshot are deliberately excluded. No private
keys or personal certificates are included. These files are committed public
application resources, unlike local personal test certificates.

The manifests record the supplied vendor snapshot and original filenames.
The national root's published SHA-1 fingerprint
`68a24d2b2cb5c4ce9fe300e3b6fcd1dfeeb9c311` and serial number
`74b8cf638fab72bc46c1d2bacd00050c` were compared with the
[BCCR certificate page](https://www.bccr.fi.cr/cr/es/firma-digital/informacion-general/certificados-de-la-autoridad-certificadora-ca.html).
The manifest records this reference and the computed SHA-256 DER fingerprint;
`cert_catalog` checks both fingerprints, the serial, CA constraints and all
intermediate chains. Files are taken from the supplied snapshot; build and tests
do not depend on the legacy download endpoint. Treat changes to root
fingerprints as trust-policy changes requiring publisher provenance review.

## Use

```sh
sign verify document-signed.pdf                   # System, then all bundles
sign verify --cert-bundle bccr document-signed.pdf
sign verify --no-bundled-certs document-signed.pdf
```

The same options work with signing. `--cert-bundle NAME` is repeatable and
restricts embedded certificates only; system trust and explicit `--ca-file`,
`--intermediates`, `--tsa-ca-file`, `--tsa-intermediates` remain available.
Unknown names and combining bundle selection with `--no-bundled-certs` fail
before accessing a token. No automatic TSA endpoint is selected: keep using
`-A URL` when the requested level needs timestamping.

Verification text and JSON report the anchor source (`system`, `additional`,
`bundled`), SHA-256 DER fingerprint, and bundle name when fallback supplied it.
A successful certificate chain does not by itself establish legal accreditation.
There is no PIN, card, signature or HTTP retry to choose another bundle.
Existing LT/LTA OCSP/CRL and TSA requests keep their normal behavior.

## Add a bundle

Create `sign/certs/NAME/manifest.json` and copy reviewed original public certificates. No C
code or CMake source-list edits are needed. For example:

```json
{
  "schema_version": 1,
  "id": "spain",
  "description": "Reviewed public issuer certificates",
  "certificates": [
    {
      "file": "root.pem",
      "sha256": "SHA256_OF_DER_LOWERCASE_HEX",
      "role": "root",
      "usages": ["document", "tsa"],
      "source": "Publisher URL and release identifier"
    },
    {
      "file": "intermediate.pem",
      "sha256": "SHA256_OF_DER_LOWERCASE_HEX",
      "role": "intermediate",
      "usages": ["document"],
      "source": "Publisher URL and release identifier"
    }
  ]
}
```

Each file must contain exactly one DER or PEM certificate (at most 64 KiB),
regardless of its extension. Keep the publisher’s original name and bytes.
Root entries must be self-signed CAs; intermediates never become anchors. Bundle identifiers are unique lowercase
names. DER fingerprints, not issuer names or URLs, identify catalogue entries.
OpenSSL selects the actual chain using issuer names, AKI/SKI and normal signature
and path validation. Certificates with shared names and different keys are
covered by the synthetic suite. Shared roots across bundles are allowed.

CMake runs `embed.py` using Python 3’s standard library and the OpenSSL CLI. It
validates the certificate, converts DER or vendor PEM into canonical PEM in
memory, and compiles the generated catalogue into Sign.
`cmake -S . -B build` followed by `cmake --build build` (or `make` inside `build/`) performs
everything automatically. Changes to certificate files regenerate the catalogue.
No conversion was added to runtime. There are no certificate downloads at
configure, build or runtime. The optional `file_sha256` field pins the original
file bytes; `sha256` always identifies the DER certificate independently of its source encoding. Git
attributes disable line-ending conversion for `.crt`, `.cer`, `.der`, and `.pem`.
The generator rejects changed fingerprints, duplicate entries within a
bundle, invalid roles/usages and escaping paths. It accepts up to 1024 entries;
joined runtime bundles are bounded to 16 MiB per trust group. libssig bounds
fallback roots to 16 MiB / 1024 certificates and checks CA status and self-signatures.

Preserve older generations for historical signatures. Review and update the
catalogue when issuers rotate or revoke certificates; installing a newer Sign
updates its built-in catalogue. System trust remains enabled and may reflect
newer administrative decisions. Use `--no-bundled-certs` when local policy must
use only system/explicit trust, including administrative root removals.

## Tests

`cert_catalog` audits the shipped public files and generator rejection paths.
`bundles_cli` uses a separate test binary with synthetic roots, equal issuer
names, explicit selection, disabled fallback, DER intermediates, expired roots,
system precedence, and a loopback TSA (one HTTP request). Synthetic roots are
never linked into the installed Sign binary. Both tests are wired in
`tests/CMakeLists.txt` and run in the existing headless and GTK workflows.
