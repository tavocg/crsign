# Ejemplos y configuración

Los [flows JSON de firmado y validación](flows.md) son la base interna para ejecutar lotes desde la GUI.

## Contenido de certificados

Las salidas JSON omiten el certificado DER por defecto. Añade `--content` para
incluirlo en hexadecimal:

```sh
sign tokens --json --content
sign verify documento.pdf --json --content
```

En `tokens` el campo se llama `der_hex`; en `verify`, `certificate_der_hex` dentro
de cada firmante. La opción también muestra el DER en la salida de texto.

## Interfaz gráfica

```sh
sign --gui
```

Consulta [la guía de la GUI](gui.md) para la navegación adaptable y los idiomas.

## Representación gráfica de la firma PDF

```sh
#           page number     x     y  imagen dimensiones
sign documento.pdf -n 1 -x 36 -y 48
sign documento.pdf -n 2 -x 36 -y 48 -i marca.png -w 180 # por defecto 144
sign documento.pdf -n 1 -x 36 -y 48 -i marca.svg -h 60
```

> [!NOTE]
> Las coordenadas y dimensiones usan puntos PDF (72 puntos por pulgada).

## Perfiles de firmado

`sign -p bccr doc.pdf` (o `--profile bccr`) carga el perfil con `label = "bccr"`
de `$XDG_CONFIG_HOME/crsign/crsign.toml`. Si la variable no está definida o está
vacía, en Linux y macOS se usa `~/.config/crsign/crsign.toml`; en Windows se usa
la carpeta local de datos de aplicaciones que resuelve GLib.

```toml
version = 0.1

# Nota: los argumentos dados por línea de comandos tienen prioridad.
[[profiles]]
name = "BCCR"
label = "bccr"
token = "pkcs11:token=..."
cert_id = "00ff"
algorithm = "rsa-pss"
hash = "sha384"
tsa = { ca = "certs/tsa-root.pem", intermediates = "certs/tsa-chain.pem" }
```

Las opciones de firmado también pueden indicarse globalmente, antes de
`[[profiles]]`. La prioridad es argumentos CLI, perfil, valores globales y
valores predeterminados. Omitir un campo en un perfil hereda el valor global;
un campo explícitamente vacío (`token = ""`, `hash = ""` o `modules = []`)
restaura la selección automática o el valor predeterminado, ignorando el global.
La etiqueta de un perfil siempre debe ser una cadena no vacía y única.
`mime_type` no es un ajuste TOML; el motor detecta el tipo de contenido al firmar.
La selección de bundles de un perfil reemplaza la selección global del grupo.

La [sección Ajustes de la GUI](gui.md#ajustes-y-perfiles) permite crear y editar
este archivo y sus perfiles con guardado automático.

`pincmd` permite obtener el PIN de un comando, tanto globalmente (antes de
`[[profiles]]`) como por perfil:

```toml
version = 0.1
pincmd = "pass show crsign/default"

[[profiles]]
label = "bccr"
pincmd = ["pass", "show", "crsign/bccr"]
```

## Módulos PKCS#11

La CLI permite repetir `-m PATH` o `--module PATH` para indicar los módulos:

```sh
sign tokens -m /ruta/modulo1.so -m /ruta/modulo2.so
```

Sin `modules` (o con `modules = []`), se usa OpenSC embebido junto con los
módulos registrados en p11-kit, incluidos los de
`~/.config/pkcs11/modules/vendor.module`. No hace falta instalar el módulo
PKCS#11 de OpenSC. Una lista explícita o `-m` selecciona únicamente esos módulos;
no añade OpenSC ni los descubiertos automáticamente. Si varios proveedores
exponen la misma tarjeta, utiliza `-m` para seleccionar el proveedor deseado.
Para cargar una o varias bibliotecas directamente, configura una lista global o
por perfil:

```toml
version = 0.1
modules = ["/usr/lib/pkcs11/vendor.so", "/opt/other/libpkcs11.so"]

[[profiles]]
label = "bccr"
modules = ["modules/vendor.so"]
```

```sh
sign -p bccr -m /ruta/vendor.so archivo.pdf
```

`sign tokens -p bccr` permite consultar los tokens y certificados usando los
módulos del perfil. `sign -p bccr archivo.pdf` usa la misma selección al firmar.
Si aparecen varios tokens, elige uno con `-t URI`, usando la URI completa que
muestra `sign tokens`.

## Confianza del sistema

La validación de documentos, TSA y OCSP incorpora la confianza del sistema a
través de libssig. En Linux conserva los almacenes predeterminados de OpenSSL,
incluidos `SSL_CERT_FILE` y `SSL_CERT_DIR`. En Windows agrega los certificados
públicos de `ROOT` y `CA` del usuario actual, que incluyen los del equipo; en
macOS agrega las anclas del sistema, los certificados con ajustes de confianza
de usuario/administrador y los certificados públicos de la búsqueda de llaveros.

`--ca-file` y `--tsa-ca-file` siguen aportando anclas explícitas e independientes;
las rutas y variables de confianza de OpenSSL siguen activas. Esas fuentes
explícitas no se someten a las reglas del almacén nativo. La evaluación nativa no
descarga emisores ni respuestas OCSP/CRL; libssig conserva sus comprobaciones de
evidencia de revocación. Si el sistema elige una cadena diferente, se rechaza esa
ruta en lugar de atribuirle la confianza de otra cadena.

## Tests

The complete offline test suites are registered in `tests/CMakeLists.txt` and
include the pinned libssig/ETSI submodules. See [tests/README.md](../tests/README.md)
for recursive checkout, dependencies, CTest selection and CI instructions.

Public certificate bundles and trust options are described in [sign/certs/README.md](../sign/certs/README.md).

## Firmas sucesivas y renovación

Volver a firmar un contenedor compatible agrega otro firmante y conserva las
firmas anteriores. La detección del formato es automática; en PDF cada nueva
representación gráfica pertenece a una nueva firma incremental.

```sh
sign documento.pdf -o primera.pdf
sign primera.pdf -o segunda.pdf
sign segunda.pdf --json --extend -A https://tsa.example/ -l B-LTA -o renovado.pdf
sign firma.p7s --original documento.bin -o otra-firma.p7s
```

`--extend` renueva evidencias sin usar un token ni pedir PIN; requiere una TSA
para los niveles con sello de tiempo y las raíces de confianza correspondientes.
No cambia la fecha declarada ni la expiración del certificado y no repara firmas
inválidas. Los PDF certificados, cifrados o con XFA devuelven error.
En perfiles de la GUI se puede activar «Renovar evidencias existentes sin
firmar» e indicar el archivo original para firmas separadas. Validar muestra un
desplegable por firmante, con su validez y detalles independientes.
