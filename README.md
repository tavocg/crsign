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

./build/sign verify documento.p7m
./build/sign tokens
./build/sign tokens --json
./build/sign verify documento.p7m --json
ctest --test-dir build --output-on-failure
```

La firma usa B-LTA por defecto y requiere `-A URL`. `-l B-B` permite firmar
sin red; también se admiten `-l B-T` y `-l B-LT`. Los valores no distinguen
mayúsculas. PDF usa PAdES automáticamente; `-f cades|pades` permite elegir
el formato. Para LT/LTA se pueden aportar `--ca-file`, `--intermediates`,
`--tsa-ca-file` y `--tsa-intermediates`; la confianza del sistema sigue activa.
Si faltan evidencias o falla un sello, no se guarda la salida.

`sign verify` delega en libssig la detección de formato y evidencias. Verifica
B-B/B-T con confianza vigente, B-LT con revocación incluida vigente y B-LTA
con la política histórica de la biblioteca. Usa la confianza del sistema por
defecto. Texto y JSON distinguen nivel detectado, nivel validado y resultado
histórico; evidencias incompletas o inválidas producen un error sin degradar
el nivel detectado. La biblioteca no descarga evidencias durante verify.
