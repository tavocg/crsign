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

El workflow contiene la lista exacta de paquetes de cada gestor. libssig se
incorpora estáticamente; las otras bibliotecas se toman de Homebrew/MSYS2.
Windows usa `curl-winssl` (Schannel); macOS usa libcurl del SDK/sistema para
conservar la confianza TLS nativa. Las actualizaciones de los paquetes no
requieren cambiar fuentes o hashes de OpenSSL, JSON, XML o PDF en CRSign.

`bootstrap-native.py` solo compila tomlc17 **R20260501** y, con `--with-argp`,
argp-standalone **1.2.0** para MinGW. macOS usa argp de Homebrew. Estos dos
fallbacks producen archivos estáticos; no son una segunda variante de CRSign.
Las descargas tienen SHA-256 fijado y se verifican también al reutilizar la caché.
CMake no descarga dependencias durante la configuración.

```sh
python3 scripts/bootstrap-native.py --prefix "$PWD/build/native-prefix"
# Windows, desde UCRT64:
python scripts/bootstrap-native.py --prefix "$PWD/build/native-prefix" --with-argp
# Reutilizar exclusivamente archivos verificados:
python scripts/bootstrap-native.py --prefix "$PWD/build/native-prefix" --with-argp --offline
```

Se necesitan Python >= 3.10, CMake y Git, además de los paquetes del workflow.
Usa un prefijo nuevo al migrar desde el preparador anterior, que también
compilaba Jansson, zlib, libxml2 y QPDF.

## Compilar e instalar en un prefijo temporal

Tras instalar los paquetes y ejecutar bootstrap:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DHEADLESS=OFF -DBUILD_TESTING=ON \
  -DSIGN_TEST_LIBSSIG=OFF -DCMAKE_PREFIX_PATH="$PWD/build/native-prefix"
cmake --build build --parallel 4
ctest --test-dir build --parallel 4 --output-on-failure
```

En UCRT64 puedes añadir `-G Ninja`. En macOS conserva las rutas de los kegs,
el SDK y el objetivo de despliegue indicados en `native-build.yml`.
La base MIME se incorpora por defecto en libssig; `LIBSSIG_MAGIC_DATABASE`
permite indicar la base que corresponde a la versión de libmagic enlazada.
`MAGIC` sigue siendo un override explícito, incluidos sus errores.

## Crear y comprobar el archivo distribuible

El paquete Windows es portable: el ZIP contiene `bin/sign.exe`, sus DLL y los
recursos necesarios. Se extrae y se ejecuta sin instalar esas bibliotecas por
separado. El controlador PKCS#11 del proveedor sigue siendo externo.
En macOS, el script genera una carpeta reubicable y un `.tar.gz`; todavía no
crea un bundle `.app` ni realiza notarización.

Desde el sistema de destino, utiliza un directorio de salida nuevo. Con libssig
integrada, la caché de libssig está en `build` y no necesita un argumento separado:

```sh
python scripts/package-native.py --build build \
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
