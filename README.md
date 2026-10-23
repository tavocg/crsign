# CRSign

Firmador digital en C, con soporte para CAdES, PAdES, XAdES, JAdES con
validación de largo plazo.

## Uso

```sh
sign tokens                # listar tokens
sign doc.pdf               # firmar
sign verify doc-signed.pdf # verificar
```

## Representación gráfica de la firma PDF

```sh
#           page number     x     y  imagen dimensiones
sign documento.pdf -n 1 -x 36 -y 48
sign documento.pdf -n 2 -x 36 -y 48 -S marca.png -w 180 # por defecto 144
sign documento.pdf -n 1 -x 36 -y 48 -S marca.svg -h 60
```

> [!NOTE]
> Las coordenadas y dimensiones usan puntos PDF (72 puntos por pulgada).

## Configuración

```toml
version = 0.1

[[profiles]]
label = "visible"
signature_image = "marca.svg"
```

```sh
sign -p visible documento.pdf -n 1 -x 36 -y 48 -w 180
sign -p visible documento.pdf -n 1 -x 36 -y 48 -S otra.png -h 60
```

La imagen del perfil solo se usa al solicitar una firma visible con `-n`, `-x`
e `-y`; sin estos argumentos se conserva el firmado invisible. El tamaño sigue
usando el ancho predeterminado de 144 puntos si no se indica `-w` o `-h`.

La apariencia se incorpora en `/AP` del widget del campo de firma, siguiendo
[ISO 32000-1, sección 12.7.4.5](https://developer.adobe.com/document-services/docs/assets/35e4369068f86065372c18787171a17e/PDF_ISO_32000-1.pdf),
y queda cubierta por el ByteRange firmado. El diseño de la marca es configurable;
no demuestra por sí mismo que la firma sea válida. PAdES define los requisitos
criptográficos en [ETSI EN 319 142-1](https://www.etsi.org/deliver/etsi_en/319100_319199/31914201/01.02.01_60/en_31914201v010201p.pdf).
Para firmas visibles se desactiva `/NeedAppearances`; los demás campos de un
formulario deben tener sus apariencias preparadas antes de firmar.
Sin opciones de apariencia se conserva la firma invisible. Solo se admite
apariencia en PDF/PAdES, con las mismas restricciones actuales de libssig sobre
documentos cifrados o ya firmados.

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
certificado, TSA y formato. Un perfil vacío hereda los valores globales.

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
El archivo se lee al firmar y listar tokens, incluso sin `-p`, para aplicar los valores globales.
Si no existe y no se solicita un perfil, se conservan los valores predeterminados.
`-p` se admite para firmar y listar tokens. Verificar y mostrar ayuda no leen
esta configuración. Al listar tokens solo se aplica la selección de módulos;
no se ejecuta `pincmd`.
Se rechazan TOML inválido, etiquetas duplicadas, campos desconocidos del perfil
seleccionado y versiones no compatibles. La versión es obligatoria; por ahora
se acepta `0.1` tanto numérico como texto (`"0.1"`), sin migración automática.

`pincmd` permite obtener el PIN de un comando, tanto globalmente (antes de
`[[profiles]]`) como por perfil:

```toml
version = 0.1
pincmd = "pass show crsign/default"

[[profiles]]
label = "bccr"
pincmd = ["pass", "show", "crsign/bccr"]

[[profiles]]
label = "interactivo"
pincmd = []
```

El perfil hereda el comando global si omite `pincmd`; su propio comando lo
reemplaza. `pincmd = []` fuerza la entrada interactiva. Se admite una cadena con
comillas de estilo shell o una lista de argumentos; la lista evita ambigüedades
con rutas de Windows. El ejecutable se busca en `PATH` o se usa su ruta explícita,
y hereda el directorio de trabajo. No se ejecuta un shell ni se expanden variables
o tuberías automáticamente; para usarlas hay que invocar el shell explícitamente.

El comando debe finalizar correctamente y devolver una sola línea UTF-8 no
vacía, de hasta 1024 bytes, en stdout. Se elimina únicamente el salto de línea
final LF o CRLF; se conservan los espacios. Su stdin está desconectado y su
stderr se descarta para evitar que el PIN aparezca en diagnósticos. Si falla o
devuelve un PIN inválido, la firma se detiene sin reintento ni solicitud
interactiva adicional. Solo se ejecuta si el token necesita un PIN y no tiene
teclado propio.

## Módulos PKCS#11

Sin `modules`, se conserva la detección de módulos registrados en p11-kit.
Para cargar una o varias bibliotecas directamente, configura una lista global o
por perfil:

```toml
version = 0.1
modules = ["/usr/lib/pkcs11/vendor.so", "/opt/other/libpkcs11.so"]

[[profiles]]
label = "bccr"
modules = ["modules/vendor.so"]

[[profiles]]
label = "automatico"
modules = []
```

Un perfil que omite `modules` hereda la lista global. Su propia lista reemplaza
la global; `modules = []` restablece la detección de p11-kit. Una lista no vacía
carga exclusivamente esas bibliotecas, en el orden indicado. Todas deben cargar
e inicializarse correctamente; si alguna falla se liberan las anteriores y se
detiene la operación. Las rutas repetidas se cargan una sola vez.

Las rutas relativas parten de la carpeta de `crsign.toml`, también sin `-p`.
No se expanden `~` ni variables. Usa bibliotecas `.so` en Linux, `.dylib` o `.so`
según el proveedor en macOS y `.dll` en Windows, compatibles con la arquitectura
del programa y con sus dependencias instaladas. En TOML, las comillas simples
permiten escribir rutas de Windows sin escapar las barras:
`modules = ['C:\Program Files\Vendor\pkcs11.dll']`.

`sign tokens -p bccr` permite consultar los tokens y certificados usando los
módulos del perfil. `sign -p bccr archivo.pdf` usa la misma selección al firmar.
Si aparecen varios tokens, elige uno con `-t URI`, usando la URI completa que
muestra `sign tokens`.

La CLI permite repetir `-m PATH` o `--module PATH` para indicar los módulos:

```sh
sign tokens -m /ruta/modulo1.so -m /ruta/modulo2.so
sign -p bccr -m /ruta/vendor.so archivo.pdf
```

Si se proporciona algún `-m`, la lista completa de la CLI reemplaza la del perfil
o la global, independientemente del orden de los argumentos. Cada opción recibe
una sola ruta, sin separadores especiales. Las rutas relativas de la CLI parten
del directorio de trabajo; se pueden usar comillas para rutas con espacios.
Sin `-m`, se aplica la configuración o la detección de p11-kit como antes.
`-m` se admite al firmar y listar tokens, y se rechaza al verificar.


## Confianza del sistema

La validación de documentos, TSA y OCSP incorpora la confianza del sistema a
través de libssig. En Linux conserva los almacenes predeterminados de OpenSSL,
incluidos `SSL_CERT_FILE` y `SSL_CERT_DIR`. En Windows agrega los certificados
públicos de `ROOT` y `CA` del usuario actual, que incluyen los del equipo; en
macOS agrega las anclas del sistema, los certificados con ajustes de confianza
de usuario/administrador y los certificados públicos de la búsqueda de llaveros.
No abre claves privadas ni modifica los almacenes.

Una cadena que termina en un ancla importada del almacén nativo debe superar
las comprobaciones de OpenSSL y la evaluación nativa de esa misma cadena,
con su fecha de validación y el propósito de sello de tiempo cuando corresponda.
Un certificado del almacén de intermediarios no se convierte por sí solo en una
raíz confiable. Los rechazos nativos se informan como `certificate rejected` y
no activan el respaldo de raíces embebidas.

`--ca-file` y `--tsa-ca-file` siguen aportando anclas explícitas e independientes;
las rutas y variables de confianza de OpenSSL siguen activas. Esas fuentes
explícitas no se someten a las reglas del almacén nativo. La evaluación nativa no
descarga emisores ni respuestas OCSP/CRL; libssig conserva sus comprobaciones de
evidencia de revocación. Si el sistema elige una cadena diferente, se rechaza esa
ruta en lugar de atribuirle la confianza de otra cadena.

La selección automática de TSA usa la misma integración. La CI comprueba la
capa común en Linux y los almacenes nativos en Windows y macOS.

## Construcción

Requiere CMake, un compilador C99, argp, libssig >= 0.1.4, libcurl >= 7.85,
pkg-config, GLib >= 2.40/GIO, librsvg >= 2.52, GdkPixbuf, Cairo/PangoCairo y tomlc17 (API con `toml_get`, por ejemplo 20260501).
La interfaz gráfica también requiere GTK 4. GLib/GIO y tomlc17 se usan igualmente
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
Windows UCRT64 y macOS. Los workflows no generan ni suben paquetes distribuibles.
El firmado con un token físico se comprueba por separado con el controlador del
proveedor. Se conservan [las instrucciones de compilación nativa y empaquetado
manual](packaging/README.md) para preparar futuros releases.

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
