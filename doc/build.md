# Construcción

Requiere CMake, un compilador C99, argp, libssig >= 0.2.1, libcurl >= 7.85,
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

Los submódulos de producción están en `vendor/libssig` y `vendor/opensc`.
CMake construye OpenSC estáticamente desde el commit de `master` fijado por Git,
sin descargas, usando Autoconf, Automake, Libtool, Make y los encabezados de PC/SC.
`nix develop` proporciona esas herramientas. Los archivos generados permanecen
en `build/opensc`; el submódulo no se modifica.
`SIGN_EMBED_OPENSC` está activado por defecto: si falta el submódulo, configura
después de ejecutar `git submodule update --init --recursive`. Para construir
únicamente con proveedores externos, usa `-DSIGN_EMBED_OPENSC=OFF`; en ese caso
no se construye OpenSC ni se requieren sus herramientas. El CI y los paquetes
oficiales conservan OpenSC embebido. El bootstrap nativo descarga únicamente
tomlc17 y, cuando se solicita, argp; CMake permanece offline.
El servicio PC/SC y los controladores del lector siguen siendo del sistema.

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
Windows UCRT64 y macOS con una sola configuración: libssig y OpenSC estáticos y
dependencias del sistema. Los workflows no generan ni suben paquetes
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

## Modelo de dependencias

CRSign compila e incorpora libssig estáticamente desde `vendor/libssig`; no necesita
una instalación separada. Las demás bibliotecas se descubren con CMake y
pkg-config, usando las versiones y el tipo de enlace de sus paquetes. No se
recompilan ni se fuerzan archivos estáticos de OpenSSL, Jansson, zlib, libxml2,
QPDF, JPEG o libmagic. La aplicación solo necesita un compilador C.

`nix develop` proporciona las dependencias en Linux. En Windows/macOS, los
workflows instalan los paquetes de MSYS2/Homebrew; `scripts/bootstrap-native.py`
solo prepara tomlc17 y, cuando hace falta, argp. Esas dos bibliotecas pequeñas
pueden quedar estáticas porque el preparador produce archivos `.a`.

Para trabajar con una libssig externa estática, configura `libssig_DIR` con el
directorio que contiene `libssigConfig.cmake`. Para volver a la copia fijada:

```sh
cmake -S . -B build -Ulibssig_DIR
```

La base MIME de libmagic se incorpora por defecto para evitar depender de un
`magic.mgc` externo. Puede desactivarse con `-DLIBSSIG_EMBED_MAGIC_DATABASE=OFF`;
los paquetes portables necesitan mantenerla activada. `MAGIC` conserva su función
de override explícito y `LIBSSIG_MAGIC_DATABASE` permite seleccionar una base
compatible al compilar.

Para migrar desde la configuración anterior que forzaba todas las bibliotecas
estáticas, usa un directorio de compilación nuevo, también para `native-prefix`.
Así se evitan las rutas y los archivos guardados por aquella configuración.

## Actualizar dependencias

En Linux, `nix flake update` actualiza `flake.lock`; revisa el cambio y ejecuta
el CI antes de incorporarlo. En macOS usa `brew update`/`brew upgrade`; en Windows,
`pacman -Syu` dentro de UCRT64. Conserva las bibliotecas de un mismo entorno.

Dependabot queda configurado para proponer semanalmente actualizaciones de
GitHub Actions y del submódulo libssig cuando la configuración llegue a la rama
predeterminada. Los cambios requieren revisión y CI; no se fusionan solos.
Solo tomlc17 y argp mantienen descargas propias: sus versiones y hashes están
en `scripts/bootstrap-native.py`. Para actualizarlas, cambia ambos tras verificar
la fuente y ejecuta las pruebas nativas. No hay una copia propia del resto de
las bibliotecas que mantener.

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
