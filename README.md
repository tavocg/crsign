# CRSign

Firmador digital en C.

Requiere CMake, un compilador C y glibc (argp). La variante gráfica también
requiere pkg-config y los archivos de desarrollo de GTK 4.

```sh
cmake -S . -B build
cmake --build build

./build/sign --gui
```

Para compilar sin interfaz gráfica ni dependencias de GTK 4 o pkg-config:

```sh
cmake -S . -B build/headless -DHEADLESS=ON
cmake --build build/headless
./build/headless/sign --help
```
