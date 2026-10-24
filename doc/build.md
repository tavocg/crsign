# Construcción

Requiere CMake, un compilador C99, argp, libssig >= 0.1.5, libcurl >= 7.85,
pkg-config, GLib >= 2.40/GIO, Jansson >= 2.11, p11-kit, librsvg >= 2.52, GdkPixbuf, Cairo/PangoCairo y tomlc17 (API con `toml_get`, por ejemplo 20260501).
La interfaz gráfica también requiere GTK >= 4.10, libadwaita >= 1.4, Poppler GLib >= 0.82, QPDF (API C) y GNU gettext
(herramientas `msgfmt`, `msgmerge` y `xgettext`, además de libintl cuando no forma parte de libc). GLib/GIO y tomlc17 se usan igualmente
en compilaciones headless, junto con los decodificadores de imágenes; el entorno Nix incluye estas dependencias.
En Windows/macOS, libssig requiere OpenSSL >= 3.5 para consultar el propósito de
validación. Windows enlaza con Crypt32 y macOS con Security/CoreFoundation
(macOS >= 10.14). En Linux se conserva el mínimo OpenSSL 3.0 de libssig.

glibc incluye argp. En otros entornos se debe instalar una implementación
compatible, como argp-standalone. CMake detecta si argp está en la biblioteca C;
si no está, busca el encabezado y la biblioteca externos. Se pueden indicar
explícitamente con `-DARGP_INCLUDE_DIR=/ruta/include`
y `-DARGP_LIBRARY=/ruta/lib/libargp.a` (o la biblioteca equivalente del sistema).
No se descargan dependencias durante la configuración.

En Windows, la CLI obtiene los argumentos desde la línea de comandos Unicode y
los convierte a UTF-8 para las rutas, perfiles y opciones, conservando la sintaxis
de argp. Los argumentos se mantienen vivos hasta terminar la operación.

La capa de archivos usa GLib/GIO para las rutas nativas, incluidos nombres
UTF-8 en Windows, y conserva la creación exclusiva de salidas (sin sobrescribir).
La entrada del PIN usa termios en Unix y la consola Unicode en Windows, con el
eco de los caracteres desactivado: se muestra `*` al escribir y se pueden usar
retroceso y Ctrl+U para corregir. El modo del terminal se restaura al terminar o
cancelar. Sin `pincmd` requiere una terminal interactiva; no toma el PIN de stdin
redirigido, argumentos o variables de entorno. Los PIN que superen 1024 bytes
se rechazan; no se truncan. El búfer se borra antes de liberarlo tras el intento de firma.
Los tokens con teclado propio no solicitan un PIN en la CLI.

La CI compila y prueba la aplicación completa con GTK (`HEADLESS=OFF`) en Linux,
Windows UCRT64 y macOS con una sola configuración: libssig y las dependencias
pequeñas se incorporan estáticamente. Los workflows no generan ni suben paquetes
distribuibles.
El firmado con un token físico se comprueba por separado con el controlador del
proveedor. Se conservan [las instrucciones de compilación nativa y empaquetado
manual](../packaging/README.md) para preparar futuros releases.

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/ruta/instalacion/libssig
cmake --build build
./build/sign --gui
```

Por defecto se compila con GTK (`HEADLESS=OFF`) y sin pruebas (`BUILD_TESTING=OFF`).
Para compilar sin GTK, añade `-DHEADLESS=ON`.

## Dependencias estáticas

CRSign requiere archivos estáticos de libssig, tomlc17, Jansson y zlib. argp
se obtiene de libc en Linux/glibc o de un archivo estático externo. CMake falla
si encuentra libssig compartida o faltan los archivos requeridos; no hay una
segunda variante dinámica de CRSign.

`nix develop` proporciona Jansson y zlib estáticas. En Windows/macOS ejecuta
`scripts/bootstrap-native.py` como indica [la guía nativa](../packaging/README.md).
Ese paso compila tomlc17, Jansson y zlib desde fuentes fijadas y verificadas.

```sh
git submodule update --init --recursive
cmake -S tests/libssig -B build-libssig -DBUILD_TESTING=OFF -DBUILD_SHARED_LIBS=OFF
cmake --build build-libssig --parallel
cmake -S . -B build -Dlibssig_DIR="$PWD/build-libssig" -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

También puedes usar `-S ../libssig` si trabajas con el repositorio hermano.
Para una instalación de libssig estática, usa `CMAKE_PREFIX_PATH` en lugar de
`libssig_DIR`. Mantén disponibles los paquetes de desarrollo de sus dependencias.

GTK/GLib, OpenSSL, curl, QPDF, libxml2, libmagic y la infraestructura PKCS#11
conservan el enlace dinámico. Algunas de ellas también necesitan zlib compartida;
incorporar zlib en CRSign no elimina esas dependencias transitivas. Los módulos
PKCS#11 del proveedor siguen cargándose en ejecución.

## Pruebas

Para habilitar las pruebas, configura explícitamente `-DBUILD_TESTING=ON`:

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Consulta [las instrucciones de pruebas](../tests/README.md) para preparar los
submódulos y las dependencias de las suites. Python 3 y el ejecutable `openssl`
también se utilizan durante la construcción para generar el catálogo de certificados.

CMake conserva las opciones de un directorio de compilación existente. Si allí
se habían habilitado las pruebas, usa `-DBUILD_TESTING=OFF` para desactivarlas.


El ejecutable de desarrollo queda en `./build/sign`. Los catálogos de traducción
se construyen junto con la GUI; no requieren activar `BUILD_TESTING`. Consulta
[la guía de la interfaz](gui.md) y [las instrucciones de traducción](../po/README.md).
