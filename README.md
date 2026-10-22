# CRSign

Firmador digital en C, con soporte para CAdES, PAdES, XAdES, JAdES con
validación de largo plazo.

## Uso

```sh
sign tokens # listar tokens
sign doc.pdf -A 'http://tsa.sinpe.fi.cr/tsaHttp/' # firmar
sign verify doc-signed.pdf # verificar
```

La firma usa B-LTA por defecto y requiere `-A URL`. `-l B-B` permite firmar
sin red; también se admiten `-l B-T` y `-l B-LT`. PDF usa PAdES automáticamente;
`-f cades|pades` permite elegir el formato. Para LT/LTA se pueden aportar
`--ca-file`, `--intermediates`, `--tsa-ca-file` y `--tsa-intermediates`; la
confianza del sistema sigue activa. Si faltan evidencias o falla un sello, no se
guarda la salida.

## Perfiles de firmado

`sign -p bccr doc.pdf` (o `--profile bccr`) carga el perfil con `label = "bccr"`
de `$XDG_CONFIG_HOME/crsign/crsign.toml`. Si la variable no está definida o está
vacía, en Linux y macOS se usa `~/.config/crsign/crsign.toml`; en Windows se usa
la carpeta local de datos de aplicaciones que resuelve GLib.

```toml
version = 0.1

[[profiles]]
name = "BCCR"
label = "bccr"
token = "pkcs11:token=..."
cert_id = "00ff"
algorithm = "rsa-pss"
hash = "sha384"
tsa = { ca = "certs/tsa-root.pem", intermediates = "certs/tsa-chain.pem" }

[[profiles]]
label = "automatico"
```

Solo `label` es obligatorio en cada perfil y debe ser único. `name` es
descriptivo. Todos los parámetros de firmado son opcionales: al omitirlos se
conservan los valores predeterminados y la selección automática de token,
certificado, TSA y formato. El perfil `automatico` equivale a firmar sin perfil.

Los argumentos explícitos prevalecen independientemente de su posición:
`sign -p bccr -t "pkcs11:token=Other" doc.pdf` sustituye el token del perfil.
También se admiten `level`, `format`, `mime_type`, `detached` (booleano), `ca`,
`intermediates`, `no_bundled_certs` (booleano), `cert_bundles` (lista de nombres)
y `tsa.url`, además de `tsa.ca` y `tsa.intermediates`. Los valores de algoritmos,
hashes, niveles y formatos son los mismos que acepta la CLI. Una selección
explícita con `--cert-bundle` o `--no-bundled-certs` reemplaza la selección de
bundles del perfil.

Las rutas de certificados relativas se resuelven desde la carpeta de
`crsign.toml`; las rutas pasadas por CLI siguen siendo relativas al directorio
de trabajo. No se expanden `~` ni variables de entorno dentro del TOML.
El archivo solo se lee al usar `-p`, que se admite exclusivamente para firmar.
Se rechazan TOML inválido, etiquetas duplicadas, campos desconocidos del perfil
seleccionado y versiones no compatibles. La versión es obligatoria; por ahora
se acepta `0.1` tanto numérico como texto (`"0.1"`), sin migración automática.

## Construcción

Requiere CMake, un compilador C, glibc (argp), libssig, libcurl >= 7.85,
pkg-config, GLib/GIO y tomlc17 (API con `toml_get`, por ejemplo 20260501).
La interfaz gráfica también requiere GTK 4. GLib/GIO y tomlc17 se usan igualmente
en compilaciones headless; el entorno Nix incluye ambas dependencias.

La capa de archivos usa GLib/GIO para las rutas nativas, incluidos nombres
UTF-8 en Windows, y conserva la creación exclusiva de salidas (sin sobrescribir).
Esto es un primer paso de portabilidad: la CLI, la entrada del PIN, la carga de
módulos de tarjetas y la integración con almacenes nativos todavía requieren
adaptación y pruebas en Windows/macOS. No implica soporte completo del programa
en esos sistemas.

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/ruta/instalacion/libssig -DBUILD_TESTING=OFF
cmake --build build
./build/sign --gui
```

Para compilar sin GTK, añade `-DHEADLESS=ON`. Las pruebas requieren Python 3
y el ejecutable `openssl`; pueden omitirse con `-DBUILD_TESTING=OFF`.

## Tests

The complete offline test suites are registered in `tests/CMakeLists.txt` and
include the pinned libssig/ETSI submodules. See [tests/README.md](tests/README.md)
for recursive checkout, dependencies, CTest selection and CI instructions.

Public certificate bundles and trust options are described in [sign/certs/README.md](sign/certs/README.md).
