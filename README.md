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

## Construcción

Requiere CMake, un compilador C, glibc (argp), libssig y libcurl >= 7.85.
La interfaz gráfica también requiere pkg-config y GTK 4.

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/ruta/instalacion/libssig
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
