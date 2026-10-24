# Flows de firmado y validación

Un flow describe una lista ordenada de **archivos de cualquier tipo** y sus
operaciones de firmado o validación. Es una API interna para la GUI; no agrega
un comando ni una opción de flows a la CLI. La GUI podrá construir el JSON y
entregarlo a los mismos motores de firmado y validación que utiliza la CLI.

```json
{
  "version": 1,
  "documents": [
    {
      "input": "documentos/reporte.pdf",
      "output": "firmados/reporte.pdf",
      "level": "B-B",
      "appearance": {
        "enabled": true,
        "page_number": 1,
        "x": 36,
        "y": 48,
        "width": 180
      }
    },
    {
      "input": "datos/respaldo.bin",
      "output": "firmados/respaldo.p7m",
      "format": "cades",
      "level": "B-B",
      "appearance": { "enabled": false }
    },
    {
      "input": "datos/informe.csv",
      "output": "firmados/informe.xml",
      "format": "xades",
      "level": "B-B",
      "token": "pkcs11:token=Mi%20token",
      "cert_id": "00ff",
      "algorithm": "rsa-pss",
      "hash": "sha384"
    }
  ]
}
```

El [JSON Schema de la versión 1](flow.schema.json) describe el formato. La versión
es independiente de la versión de `crsign.toml`. Las versiones desconocidas se
rechazan; la lectura de una futura versión podrá tener sus propias reglas o
migraciones sin reinterpretar silenciosamente documentos antiguos.

## Opciones por archivo

Cada documento acepta `operation`: `"sign"` para firmar o `"verify"` para validar.
Si se omite, se usa `"sign"`, conservando los flows existentes de la versión 1.
Se permiten lotes de una sola operación y flows mixtos, como firmar un archivo y
validar su salida en el siguiente documento.

Solo `input` es obligatorio. Al firmar, `output` se calcula como en la CLI si
se omite: `archivo.bin` produce `archivo-signed.bin`. Las carpetas de salida
deben existir.
Las rutas de entrada, salida, imagen, certificados y módulos se convierten en
rutas absolutas al leer el flow, con el directorio base que proporcione quien
llama a la API. Si no se proporciona, se toma el directorio de trabajo en ese
momento. Cambiarlo después no cambia el significado del flow.

No hay restricciones por extensión. Sin `format`, el motor reconoce el encabezado
PDF y selecciona PAdES; para el resto usa CAdES. Se puede elegir explícitamente
`cades`, `pades`, `xades` o `jades`. PAdES requiere un PDF válido; los demás formatos
pueden envolver contenido arbitrario, incluido contenido binario.

Las opciones restantes son:

| Campo | Uso |
| --- | --- |
| `token`, `cert_id` | URI del token e ID hexadecimal del certificado; al omitirlos se mantiene la selección automática. |
| `algorithm`, `hash` | `rsa-pkcs`, `rsa-pss` o `ecdsa`; `sha256`, `sha384` o `sha512`. Predeterminados: RSA PKCS#1 v1.5 y SHA-256. |
| `level` | `B-B`, `B-T`, `B-LT` o `B-LTA`; predeterminado: `B-LTA`. Los ejemplos usan `B-B` para no requerir TSA. |
| `mime_type`, `detached` | Tipo de contenido y firma separada, como en la CLI. PAdES no admite firma separada. |
| `modules` | Lista de rutas a módulos PKCS#11; omitida o vacía conserva p11-kit. |
| `ca`, `intermediates` | Certificados adicionales de confianza y cadena del documento. |
| `tsa` | Objeto opcional con `url`, `ca` e `intermediates`. Sin URL se mantiene la selección automática de TSA. |
| `no_bundled_certs`, `cert_bundles` | Desactivar o seleccionar los bundles incorporados; una selección no vacía es incompatible con desactivarlos. |
| `appearance` | Representación gráfica opcional, exclusivamente para PDF/PAdES. |

`appearance` requiere `enabled`, de tipo booleano. Con `true`, requiere también
`page_number` (desde 1), `x` e `y`. `image` elige SVG/PNG/JPEG; si se omite, se
genera la marca del certificado. Se indica `width` o `height`, nunca ambos, y se
conserva la proporción. Sin ninguno se usa el ancho de 144 puntos PDF. Ambas
dimensiones deben quedar entre 12 y 720 puntos y caber en la página. Con `false`
se pueden conservar ajustes para una futura edición, pero no se carga la imagen
ni se solicita un PDF para aplicar esa representación.

`appearance.information` es un booleano opcional. Con `true` y una `image`, se
compone la imagen con el sello de información del certificado debajo. Las medidas
se aplican al conjunto, manteniendo la proporción de la imagen. La GUI activa
esta opción inicialmente y guarda la elección de cada documento. Si se omite,
se conserva el comportamiento de imagen sola de los flows anteriores. Sin
`image`, se genera la marca predeterminada con su información habitual.

## Validación en lote

```json
{
  "version": 1,
  "documents": [
    { "operation": "verify", "input": "firmados/reporte.pdf" },
    { "operation": "verify", "input": "firmados/respaldo.p7m", "ca": "raices.pem" },
    {
      "operation": "verify",
      "input": "firmados/datos.p7s",
      "content": "datos/original.bin",
      "ca": "raices.pem"
    }
  ]
}
```

Se reconoce automáticamente el formato de la firma: CAdES, PAdES, XAdES o JAdES.
`input` contiene el documento o contenedor firmado; `content` es la ruta del
contenido original **solo para firmas separadas**. También admite archivos vacíos.
Un contenedor con contenido incorporado rechaza `content` para evitar ambigüedad.

La validación acepta `ca`, `intermediates`, `tsa.ca`, `tsa.intermediates`,
`no_bundled_certs` y `cert_bundles`, con la misma política de confianza que la CLI.
Consulta las raíces del sistema y los certificados adicionales configurados en el
flow; no basta con que la firma criptográfica sea correcta. Una firma alterada,
un certificado no confiable o cualquier otro error del verificador detiene el
flow. Las comprobaciones de evidencias y niveles son las de libssig; no descarga
evidencias OCSP/CRL de la red.

Esta operación no genera archivos, abre módulos PKCS#11 ni solicita PIN. Rechaza
opciones exclusivas de firmado, como `output`, `token`, `format`, `appearance` o
`tsa.url`, para detectar errores al construir el JSON.

## Lectura y ejecución

`sign/flow.h` ofrece:

- `sign_flow_parse_json()`: valida el JSON completo y resuelve sus rutas, sin leer
  documentos, cargar módulos ni solicitar PIN.
- `sign_flow_count()` y `sign_flow_get_item()`: consultan la operación, los
  parámetros compartidos `sign_options` y el contenido separado. Los punteros
  pertenecen al flow y son de solo lectura. `sign_flow_document()` permite
  consultar directamente las opciones compartidas.
- `sign_flow_to_json()`: devuelve JSON canónico con rutas absolutas y salidas
  resueltas, para poder guardar y volver a cargar el flow sin depender del CWD.
- `sign_flow_run()`: ejecuta de forma síncrona y secuencial. La GUI deberá llamarlo
  desde un worker para mantener la interfaz disponible.
- `sign_flow_free()`: libera el flow cuando ya no se esté utilizando.

La API rechaza campos desconocidos, tipos incorrectos, claves duplicadas, texto
no UTF-8, valores incompatibles, salidas repetidas y salidas que reemplacen
entradas de firmado o entradas de validaciones anteriores. Una validación sí
puede consumir una salida producida por un firmado anterior del mismo flow.
Admite hasta 1024 archivos por flow y 8 MiB de JSON. Las opciones
se validan antes de ejecutar el primer documento; la existencia y el contenido
real de los archivos se comprueban durante la ejecución.

Hay una exclusión global **dentro del proceso**: otro intento de ejecutar un flow,
incluso desde otro hilo o desde un callback, devuelve `SIGN_FLOW_ERROR_BUSY`.
Al fallar un documento, no se inicia ningún documento posterior. Las salidas ya
completadas se conservan; no hay rollback ni sobrescritura de archivos existentes.
La exclusión se libera también al fallar, para permitir ejecutar otro flow.

`sign_flow_result` indica cuántos archivos se completaron y el índice del que
falló (desde cero). `failed_document` vale `SIGN_FLOW_NO_DOCUMENT` si no falló
ningún documento. `documents` contiene un informe por documento, con su operación,
`attempted` y `ok`; los posteriores al fallo permanecen sin intentar.

Al firmar, `signing` conserva formato, tamaño de salida y código PKCS#11. Al validar,
`verification_status` conserva el estado de libssig y `verification` conserva su
informe completo: firmantes, certificados, firma, confianza, sellos y evidencias,
incluidos los datos disponibles cuando la validación falla. `document` mantiene
el resumen del último intento de firmado por compatibilidad.

El resultado posee memoria: llamar a `sign_flow_result_clear()` después de
examinarlo, incluso tras un fallo y antes de reutilizarlo. Puede pasarse `NULL`
a `sign_flow_run()` si no se necesita el informe. Los errores se devuelven mediante
`GError`, sin salidas de progreso ni resultados de CLI en stdout.

## PIN y configuración

El JSON no acepta `pin`, `pincmd`, perfiles de configuración ni URI de token con
parámetros de consulta. Utiliza la URI de identidad que devuelve el listado de
tokens. No se leen archivos TOML ni se ejecutan comandos de PIN de forma implícita.

`sign_execution.request_pin` es un callback de ejecución. Solo se llama después
de seleccionar un token que requiere PIN. Su búfer se borra y libera al finalizar
el intento, también cuando el proveedor falla. El motor no conserva un PIN entre
archivos ni lo incluye en el flow serializado. Un proveedor puede mantenerlo
en memoria durante una ejecución para el mismo token, como hace la GUI, y
debe borrarlo al terminar. La CLI adapta su proveedor actual de terminal/`pincmd`
a esta misma interfaz; la GUI aporta su propio diálogo.
Sin proveedor, un token que exige PIN produce un error sin abrir una terminal.
Los tokens con ruta de autenticación protegida no llaman al proveedor.

```c
GError* error = NULL;
sign_flow* flow = sign_flow_parse_json(json, json_len, base_directory, &error);
if (flow) {
    sign_execution execution = {
        .request_pin = request_pin_during_execution,
        .user_data = application_state,
    };
    sign_flow_result result;
    bool ok = sign_flow_run(flow, &execution, &result, &error);
    /* Mostrar result/error; no liberar el flow mientras se está ejecutando. */
    (void)ok;
    sign_flow_result_clear(&result);
    sign_flow_free(flow);
}
g_clear_error(&error);
```

El módulo comparte opciones, validación y ejecución de un archivo con la CLI,
sin llamar a `arg_parse()` ni necesitar GTK para ejecutar un flow.
`sign_flow_from_items()` permite construirlo directamente a partir de una lista
de `sign_flow_item` con los mismos `sign_options`, sin mantener estructuras
paralelas para JSON. Produce un snapshot independiente de las opciones y lo
valida mediante el mismo esquema y resolución de rutas que el lector JSON.

`sign_execution.cancellable` acepta un `GCancellable` opcional. La cancelación
se comprueba antes de iniciar cada documento y antes de guardar una firma.
No interrumpe una operación PKCS#11 que ya esté ejecutándose, pero descarta su
resultado si se cancela antes de la escritura. El callback de PIN puede utilizar
ese mismo objeto para cancelar su diálogo.
