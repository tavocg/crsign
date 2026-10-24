# Compilación nativa y empaquetado manual

`native-build.yml` compila y prueba la aplicación completa con GTK
(`HEADLESS=OFF`) en Windows UCRT64 y macOS. `build.yml` hace lo mismo en Linux.
La CI ejecuta las pruebas de CLI, almacenes y recursos, sin generar archivos
distribuibles ni subir artefactos.

Los scripts de empaquetado se conservan para ejecutarlos manualmente cuando se
preparen releases con las fuentes y el programa. Las secciones de empaquetado
de este documento describen ese proceso opcional. El firmado con un token físico
se valida por separado con el controlador del proveedor.

## Dependencias

Se usa GCC/MinGW de **MSYS2 UCRT64** en Windows, y Clang con Homebrew y el SDK de
Apple en macOS. No se mezclan bibliotecas de MSYS, MINGW64 o MSVC con UCRT64.
El ZIP apunta a Windows 10 1903 o posterior, x86_64; el manifiesto activa UTF-8
para las APIs ANSI de las dependencias, sin modificar la configuración regional
del sistema. macOS usa la arquitectura del runner y objetivo de despliegue 14.0;
las bibliotecas de Homebrew pueden elevar el mínimo efectivo del paquete.
No se genera un binario universal a partir de bibliotecas de una sola arquitectura.

El workflow contiene la lista exacta de paquetes de cada gestor. Windows usa
`curl-winssl` (Schannel); macOS usa libcurl del SDK/sistema para HTTPS con la
confianza TLS nativa. La validación de documentos y TSA sigue a cargo de libssig.

`bootstrap-native.py` compila estáticamente tomlc17 **R20260501**, Jansson
**2.15.0**, zlib **1.3.2** y, con `--with-argp`, la
variante de argp-standalone **1.2.0** compatible con MinGW. macOS usa
argp-standalone de Homebrew. Los archivos descargados tienen SHA-256 fijado y
se comprueban también al reutilizar la caché. La descarga ocurre exclusivamente
en este paso explícito, nunca durante la configuración CMake de la aplicación.

```sh
python3 scripts/bootstrap-native.py --prefix "$PWD/build/native-prefix"
# Windows, desde UCRT64:
python scripts/bootstrap-native.py --prefix "$PWD/build/native-prefix" --with-argp
# Para reutilizar únicamente archivos ya verificados en build/native-deps:
python scripts/bootstrap-native.py --prefix "$PWD/build/native-prefix" --with-argp --offline
```

Se necesitan Python >= 3.10, CMake >= 3.18 y Git para este proceso. Los gestores
nativos aportan el compilador, pkg-config, GTK/GLib, libadwaita, gettext, OpenSSL >= 3.5, p11-kit,
libmagic, QPDF, Poppler GLib y libxml2. Consulta el workflow para las rutas de
OpenSSL, libxml2 y libcurl del SDK en macOS.

## Compilar e instalar en un prefijo temporal

Tras instalar las dependencias y ejecutar bootstrap, antepón
`build/native-prefix/lib/pkgconfig` a `PKG_CONFIG_PATH` para seleccionar Jansson
y zlib del prefijo. Se requiere libssig estática:

```sh
export PKG_CONFIG_PATH="$PWD/build/native-prefix/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"

cmake -S tests/libssig -B build-libssig -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF -DBUILD_SHARED_LIBS=OFF -DLIBSSIG_EMBED_MAGIC_DATABASE=ON \
  -DCMAKE_INSTALL_PREFIX="$PWD/build/native-prefix"
cmake --build build-libssig --parallel 4
cmake --install build-libssig

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DHEADLESS=OFF -DBUILD_TESTING=ON \
  -DSIGN_TEST_LIBSSIG=OFF -DCMAKE_PREFIX_PATH="$PWD/build/native-prefix"
cmake --build build --parallel 4
ctest --test-dir build --parallel 4 --output-on-failure
```

En UCRT64 puedes añadir `-G Ninja`. En macOS añade las rutas de los kegs y el SDK
indicadas en `native-build.yml`. `LIBSSIG_MAGIC_DATABASE` permite indicar el
archivo `magic.mgc` que corresponde a la versión de libmagic enlazada. La base
se comprime y se incorpora en libssig; no se busca en el equipo de compilación
al ejecutar el paquete. `MAGIC` sigue siendo un override explícito, incluidos
sus errores. libssig, tomlc17, Jansson, zlib y argp externo quedan incorporados
al ejecutable; las bibliotecas de GUI y del sistema conservan el enlace dinámico.

## Crear y comprobar el archivo distribuible

Desde el sistema de destino, utiliza un directorio de salida nuevo:

```sh
python scripts/package-native.py --build build --libssig-build build-libssig \
  --library-prefix build/native-prefix --dependency-prefix "$MINGW_PREFIX" \
  --destination build/dist/CRSign-windows-x86_64
python scripts/test-native-package.py build/dist/CRSign-windows-x86_64 \
  --openssl "$MINGW_PREFIX/bin/openssl.exe" --gui
```

En macOS pasa `--dependency-prefix` para `brew --prefix` y para los kegs de
libmagic, libxml2 y OpenSSL, como hace el workflow; indica la ruta al ejecutable
OpenSSL de Homebrew al comprobar el paquete. Omite `--gui` en la variante CLI.

El empaquetador recorre las dependencias PE/Mach-O, incluye las bibliotecas
transitivas y falla si falta alguna o hay nombres incompatibles. En macOS
reescribe las referencias a las bibliotecas copiadas y aplica firmas ad hoc.
Las bibliotecas del sistema operativo no se copian. GTK incluye esquemas,
íconos y módulos de imágenes/GIO; sus rutas se resuelven desde la instalación
trasladada. La caché de imágenes se materializa en la caché privada de CRSign,
sin ejecutar herramientas auxiliares al abrir el programa. Las variables
explícitas de recursos del usuario se respetan.

El archivo incluye la licencia del proyecto, fuentes del proyecto y libssig,
las fuentes/licencias fijadas por bootstrap, los avisos de dependencias
instalados en `share/licenses` y un manifiesto SHA-256. No incluye configuraciones
personales, PIN, claves ni controladores PKCS#11 del proveedor; estos últimos
se configuran con `modules` o `-m`.

La prueba del paquete lo copia a una carpeta con espacios y Unicode, reduce
`PATH`, elimina rutas de bibliotecas de desarrollo, comprueba ayuda, versión,
verificación de una firma real y fallos de carga de módulos. En la variante GTK
también abre la ventana. No utiliza una tarjeta ni un PIN real.

Tras extraer el archivo, usa `bin/sign` (macOS) o `bin/sign.exe` (Windows).
`--gui` abre GTK en la variante correspondiente. Son paquetes portables, sin
instalador, certificado de editor o notarización. La firma ad hoc de macOS no
sustituye una firma Developer ID ni la notarización para distribución pública.
