# CRSign

Firmador digital en C.

Requiere CMake, un compilador C, glibc (argp), libssig y libcurl >= 7.85.
La interfaz gráfica también requiere pkg-config y GTK 4.

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/ruta/instalacion/libssig
cmake --build build
./build/sign --gui
```

Para compilar sin GTK, añade `-DHEADLESS=ON`. Las pruebas requieren Python 3
y el ejecutable `openssl`; pueden omitirse con `-DBUILD_TESTING=OFF`.

```sh
# Firma CAdES con contenido incluido; solicita el PIN sin mostrarlo.
./build/sign documento.txt -o documento.p7m \
  -t 'pkcs11:...' -k ID_HEX \
  -A 'http://tsa.sinpe.fi.cr/tsaHttp/'

./build/sign --verify documento.p7m
ctest --test-dir build --output-on-failure
```
