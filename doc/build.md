# Construcción

Requiere CMake, compiladores C99 y C++ (C++20 para compilar QPDF), argp, libssig >= 0.1.5, libcurl >= 7.85,
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
git submodule update --init --recursive
nix develop
cmake -S . -B build
cmake --build build
./build/sign --gui
```

Por defecto se compila con GTK (`HEADLESS=OFF`) y sin pruebas (`BUILD_TESTING=OFF`).
Para compilar sin GTK, añade `-DHEADLESS=ON`.

## Dependencias estáticas

CRSign compila automáticamente libssig desde el submódulo `tests/libssig` y la
incorpora estáticamente. No hace falta compilarla ni instalarla por separado.
También requiere archivos estáticos de tomlc17, Jansson, zlib, libxml2 y QPDF. argp
se obtiene de libc en Linux/glibc o de un archivo estático externo. CMake falla
si encuentra libssig compartida o faltan los archivos requeridos; no hay una
segunda variante dinámica de CRSign.

`nix develop` proporciona los archivos estáticos requeridos, incluidos libxml2 y QPDF. En Windows/macOS ejecuta
`scripts/bootstrap-native.py` como indica [la guía nativa](../packaging/README.md).
Ese paso compila tomlc17, Jansson, zlib, libxml2 y QPDF desde fuentes fijadas y
verificadas. QPDF se configura después de instalar las dependencias pequeñas.
El código de CRSign continúa siendo C; el enlazador C++ incorpora las
dependencias necesarias de QPDF sin aplicar opciones de C++ a los archivos C.

```sh
git submodule update --init --recursive
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Para trabajar con una compilación externa o el repositorio hermano:

```sh
cmake -S ../libssig -B ../libssig/build-static -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF
cmake --build ../libssig/build-static --parallel
cmake -S . -B build -Dlibssig_DIR="$PWD/../libssig/build-static"
```

`libssig_DIR` debe apuntar al directorio que contiene `libssigConfig.cmake`.
Para volver a la copia fijada, configura con `cmake -S . -B build -Ulibssig_DIR`.
Mantén disponibles los paquetes de desarrollo de las dependencias en ambos casos.

GTK/GLib, OpenSSL, curl, libmagic y la infraestructura PKCS#11 conservan el
enlace dinámico, junto con JPEG y el runtime C++ que necesita QPDF. Algunas de ellas también necesitan zlib compartida;
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
