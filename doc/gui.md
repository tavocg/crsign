# Interfaz gráfica

```sh
./build/sign --gui
```

La ventana usa GTK 4 y libadwaita. Tiene seis vistas: Inicio, Firmar, Validar,
Dispositivos, Ajustes y Acerca de. Ajustes permite editar la configuración y los perfiles;
Acerca de muestra la identidad y los enlaces del proyecto. Validar permite
procesar varios archivos firmados. Firmar permite preparar y ejecutar una cola de
documentos. Inicio muestra Favoritos y Recientes.

## Inicio, historial y búsqueda

Inicio muestra Favoritos y Recientes, ordenados por la fecha de firmado. Las
secciones vacías se ocultan. Cuando no hay documentos registrados, Inicio también
se oculta del menú y la aplicación abre Firmar. Con historial, abre Inicio.
La sección aparece automáticamente al registrarse la primera firma satisfactoria.

La vista predeterminada es una cuadrícula de miniaturas. Cada sección muestra
los primeros 25 documentos; «Mostrar más» agrega otros 25 sin renderizar el resto
del historial. Para PDF se genera una miniatura de la primera página en segundo
plano; otros formatos muestran un icono de documento. El botón junto a los
controles de la ventana alterna entre «Mostrar lista» y «Mostrar cuadrícula».
La lista muestra las columnas Documento, Firmado y los controles del archivo.

Solo desde Recientes se puede marcar o desmarcar un favorito: con la estrella
o con el menú de clic derecho. El menú muestra una marca cuando ya es favorito.
Favoritos y Recientes conservan las mismas entradas, sin duplicarlas en el archivo
de datos. Los botones y menús «Abrir ubicación del archivo» usan el gestor de
archivos del sistema. También están disponibles en cada documento de las colas
de Firmar y Validar, y mediante clic derecho en el lector PDF.

La lupa a la izquierda de «Menú» abre una búsqueda por nombre en el historial.
La coincidencia aproximada permite omitir caracteres y no distingue mayúsculas
ni tildes. Los resultados se ordenan por relevancia y fecha, con carga de 25 en
25. Seleccionar un resultado no abre el documento ni inicia una firma: cada fila
permite abrir únicamente su ubicación. Los favoritos se modifican en Recientes.

## Datos de la aplicación

Los datos se guardan bajo `g_get_user_data_dir()/crsign/`: en Linux, por defecto
`~/.local/share/crsign/`, respetando `$XDG_DATA_HOME`; las otras plataformas usan
su directorio de datos de usuario. No se guardan en `crsign.toml`.

`history.json` usa `version: 1` y registra la ruta absoluta del archivo firmado,
la fecha en microsegundos UTC desde el inicio de la época Unix y el estado de
favorito. Lo comparten la CLI y la GUI. Una entrada se registra solo después de
guardar correctamente una firma, incluso si un documento posterior del flow
falla. Reutilizar una ruta actualiza su fecha y conserva el favorito. Las
validaciones y los intentos fallidos no agregan entradas.

No hay un límite fijo de documentos. La lectura y escritura del historial se
limitan a 16 MiB para impedir que crezca accidentalmente sin control. Un archivo
inválido o demasiado grande se conserva sin sobrescribirlo; un fallo al guardar
el historial no invalida la firma que ya se guardó.

`preferences.json` tiene su propia versión y conserva las preferencias de la interfaz:

```json
{"version":1,"home":{"view":"grid"},"devices":{"hardware_only":true}}
```

`view` admite `grid` o `list`. La última selección se restaura al abrir otra
ventana o reiniciar. `devices.hardware_only` conserva el filtro «Solo hardware»
y usa `true` cuando aún no se ha configurado. La estructura permite añadir otras
preferencias y conserva los campos desconocidos de la misma versión al guardar
cualquier preferencia. Las versiones
desconocidas se rechazan para evitar sobrescribir datos de una versión futura.
El archivo de preferencias se limita a 64 KiB.

Ambos archivos usan reemplazos atómicos, permisos privados y un bloqueo entre
procesos (`state.lock`) para combinar actualizaciones concurrentes. Abrir la
aplicación sin modificar preferencias ni firmar no crea estos archivos.

## Firmar

El selector del encabezado elige los ajustes globales o un perfil por nombre
(o etiqueta cuando no tiene nombre). Se pueden arrastrar archivos o abrir el
selector nativo. Se admiten documentos de cualquier tipo: un único PDF abre
el lector inmediatamente; varios archivos abren la cola.

El lector muestra todas las páginas con desplazamiento continuo. Inicialmente
ajusta la escala para que una página completa quepa en altura y anchura. Los
botones flotantes `+` y `−`, abajo a la derecha, cambian el zoom; también sirven
`Ctrl` + `+`, `Ctrl` + `−` y `Ctrl` + rueda. Con la rueda, el zoom conserva bajo
el cursor el mismo punto del documento, limitado por los bordes del área de
desplazamiento. Las páginas se renderizan en segundo plano conforme se necesitan
y se mantiene una caché limitada.

En el lector, haz clic sobre una página para colocar la imagen con el tamaño
predeterminado, o arrastra para definir su tamaño. Las coordenadas y medidas
son puntos PDF sobre la página visible, considerando recorte, rotación y escala.
Se conserva la relación de aspecto y se ajusta el tamaño entre los límites
compartidos con la CLI (12–720 puntos por dimensión). Si no cabe, se muestra un
error. Arrastrar sobre la imagen ya colocada la mueve, conserva sus dimensiones
y la limita a los bordes de su página. El cursor es una mano sobre la imagen,
una mano cerrada mientras se mueve y una cruz fuera de ella. Las tres esquinas
sin equis muestran controles para redimensionar, con cursor diagonal. Al
arrastrarlos se conserva la proporción y permanece fija la esquina opuesta;
los límites de tamaño y de página se aplican en puntos PDF a cualquier zoom.
El selector inferior permite saltar a una página y refleja la página
visible al desplazarse; la equis quita la representación.

Los botones de imagen, alineados a la derecha, permiten elegir SVG, PNG, JPEG y los formatos disponibles en
GdkPixbuf. El botón de restauración aparece solo al elegir una imagen distinta
de la del perfil y recupera esa imagen o la marca predeterminada. La marca
muestra primero el titular del certificado, luego «Firmado digitalmente», la
fecha y «Representación gráfica. Verifique la validez.». La fecha y los datos definitivos
de esa marca se generan al firmar. Si aún no hay un certificado seleccionable,
la previsualización muestra un titular ilustrativo.

Al seleccionar una imagen propia, incluida la de un perfil, aparece un menú de
tres puntos junto al selector de imagen. Contiene «Añadir información de firma»,
activado inicialmente para cada documento. La imagen
conserva su proporción y se centra sobre el sello con titular, fecha y aviso de
validación. El conjunto se coloca, arrastra y dimensiona como un único rectángulo;
al cambiar la opción se conserva su ancho y se recalcula la altura. Si no cabe,
se muestra el error y se mantiene la composición anterior. Al desactivarla queda
únicamente la imagen. La elección se conserva por documento en el flow.
La marca predeterminada ya incluye esa información y no muestra este control.

«Continuar» vuelve a la cola; también se puede continuar sin firma gráfica.
Cada PDF sin representación tiene un icono de advertencia con un tooltip,
y junto a «Firmar» aparece un aviso que permite continuar igualmente. Seleccionar un documento
PDF de la cola vuelve a abrir su lector y conserva la representación elegida.
Los documentos de otros tipos se firman sin representación gráfica, no muestran
esa advertencia y seleccionarlos no abre el lector ni muestra un aviso.

La barra superior de la cola muestra «Total: n» y «Limpiar todo», que vacía la
cola y cancela cualquier previsualización pendiente. Cada fila permite abrir la
ubicación del archivo y quitarlo, con la carpeta a la izquierda de la equis.

Al completar correctamente el firmado, se vacía la cola de Firmar y se abre
Validar con los archivos de salida, conservando los ajustes de confianza del
perfil y el original cuando la firma es separada. Los resultados anteriores de
Validar se conservan. Si falla el firmado, la cola permanece disponible.

La GUI construye un [flow JSON versionado](flows.md) con los ajustes y rutas de
cada documento. Se ejecuta un único flow a la vez y se detiene ante el primer
fallo. El PIN se solicita al ejecutar mediante una ventana modal, salvo que
el token tenga teclado propio o se haya configurado `pincmd`. Se conserva en
memoria únicamente durante esa ejecución para reutilizarlo con el mismo token,
y se borra después; no se incluye en el JSON ni en la configuración.
Las salidas siguen los nombres de la CLI (`archivo-signed.ext`) y no se
sobrescriben archivos existentes. Al cerrar la ventana se cancelan los
siguientes documentos y no se escribe el resultado de una operación cancelada.

Los errores de la GUI se presentan con mensajes traducidos según su categoría,
sin los prefijos de diagnóstico del flow. Un error de firmado abre un diálogo;
el mismo mensaje no se repite en el cuerpo de la aplicación. La cancelación del
PIN no abre otro diálogo. Los diagnósticos del motor y de la CLI se mantienen
separados de estos mensajes.

El lector usa Poppler GLib y Cairo; comparte con el motor de firma los
decodificadores de imágenes y los límites de tamaño. La firma criptográfica
sigue realizándose mediante libssig.

## Dispositivos

La búsqueda comienza al abrir la aplicación, en segundo plano. Mientras se
explora, el icono de Dispositivos en el menú se sustituye por un spinner.
Seleccionar un lector abre sus datos al lado de la lista, con los slots del módulo
PKCS#11 como desplegables. En ventanas pequeñas, los datos ocupan toda la sección
y el botón «Volver» regresa a la lista. Los detalles de cada slot se construyen
únicamente al desplegarlo, para evitar cargar miles de controles durante el escaneo.
Dentro de cada slot se muestran certificados y llaves sin desplegables anidados, con sus
identificadores, algoritmos, tamaños en bits y los atributos públicos disponibles.
El URI del token dispone de un botón para copiarlo al portapapeles.
También se muestran el fabricante, modelo, número de serie, versiones, memoria
y estado del PIN. No se solicita PIN ni se leen valores de llaves privadas.
Algunos objetos no son visibles hasta iniciar sesión en el token.

Por defecto sólo se exploran slots de hardware; los certificados de los tokens
de software no se leen. El menú de tres puntos del encabezado contiene «Solo hardware», marcado por
defecto. Desmarcarlo incorpora los tokens y certificados de software. El cambio
se conserva durante la sesión y no modifica `crsign.toml`.

La aplicación comprueba cambios aproximadamente cada segundo mediante eventos
no bloqueantes del módulo y los metadatos de slots y tarjetas. Al retirar,
insertar o cambiar una tarjeta vuelve a cargar la información. Se conserva el
botón «Actualizar», a la derecha del resumen, que además vuelve a leer la configuración global de
módulos y certificados. Si no se especifican módulos, se usa OpenSC embebido
junto con los módulos descubiertos por p11-kit.

La barra superior muestra siempre el estado con texto atenuado a la izquierda
y «Actualizar» a la derecha. Sin lectores indica «No se encontraron lectores»;
con lectores muestra los totales de lectores, slots, certificados y llaves.
El resumen de Validar usa el mismo tono atenuado.

La identidad y agrupación de lectores dependen de lo que publique el módulo
PKCS#11; no se enumeran dispositivos USB que no estén expuestos por un módulo.
Por certificado se comprueba por separado la cadena de confianza del sistema
y la de CRSign, incluidos los certificados incorporados y configurados. Son
comprobaciones locales de cadena y vigencia: no consultan revocación ni prueban
que una llave privada pueda firmar. Un tamaño inferido por ID de certificado
se identifica como tal.

La barra lateral tiene el título «Menú». Con una anchura de hasta 640sp pasa
a ser un panel superpuesto: aparece un botón para abrirla en la cabecera del
contenido. Al seleccionar otra vista, el panel se cierra. Al ensanchar la
ventana, la barra lateral vuelve a mostrarse junto al contenido. Las unidades
sp se adaptan al tamaño de texto del sistema.

`sign/gui/window.c` contiene el marco de navegación y las vistas, separado del
arranque de la aplicación en `sign/gui.c`. Los identificadores de las vistas
son estables e independientes de sus nombres traducidos. El tema sigue las
preferencias del sistema.

La arquitectura de [gettext](../po/README.md) permite ampliar las traducciones.
Se incluye español y se usa inglés cuando no hay una traducción disponible.
Para comprobar el español sin cambiar el idioma del escritorio:

```sh
LC_ALL=en_US.UTF-8 LANGUAGE=es ./build/sign --gui
```

La localidad indicada debe existir en el sistema. Gettext mantiene los textos
en inglés con `LC_ALL=C` o `C.UTF-8`, incluso si se define `LANGUAGE`.

## Ajustes y perfiles

Ajustes lee `crsign.toml` al crear la ventana, aunque no se visite esa sección.
La ubicación coincide con la usada por la CLI: el directorio que resuelve `g_get_user_config_dir()`, seguido de `crsign/crsign.toml`.
Respeta `XDG_CONFIG_HOME` y los directorios apropiados de cada plataforma.

El selector permite editar los valores globales o un perfil. Muestra su nombre
y, cuando no tiene nombre, su etiqueta. Los botones `+` y
`−` crean y eliminan perfiles; cada perfil necesita una etiqueta única, que
permite seleccionarlo con `sign --profile ETIQUETA`. El nombre es opcional.

Los cambios válidos se guardan automáticamente tras una breve pausa al editar.
Abrir la aplicación conserva los valores predeterminados sin crear archivos;
el primer cambio válido crea el directorio y `crsign.toml` con `version = 0.1`.
También se guardan cambios pendientes válidos al cerrar la ventana.

Cada ajuste distingue los estados siguientes:

- **Global**: omite el campo en el perfil y hereda el ajuste global. Es el estado
  inicial de los perfiles. No aparece al editar los ajustes globales; allí el
  estado predeterminado es Auto.
- **Auto**: usa la selección automática o el valor predeterminado del
  motor. En perfiles escribe un valor vacío explícito para ignorar el global;
  en los ajustes globales elimina el campo, porque allí no hay nada que heredar.
- **Personalizado**: permite escribir un valor o elegir una opción concreta.

Las cadenas vacías (`token = ""`, `hash = ""`, etc.) restauran los valores
predeterminados correspondientes. Las opciones booleanas también admiten `""`
para ese estado, además de `true` y `false`. En listas, el valor vacío es `[]`:
`modules = []` usa OpenSC embebido y p11-kit; `pincmd = []` solicita el PIN interactivamente.
La ausencia del campo en un perfil conserva su herencia global.

La lista personalizada de módulos tiene un botón para agregar rutas y otro para
eliminar cada módulo. Solo aparece al elegir Personalizado. `pincmd` permite
escribir un comando con argumentos entre comillas; los arreglos de argumentos
existentes se conservan, incluidos argumentos vacíos. La imagen es una preferencia;
su posición y dimensiones se decidirán al firmar. El tipo de contenido se detecta
al firmar y no forma parte de los ajustes TOML ni de la GUI.

El selector de configuración usa el mismo estilo que los selectores de opciones.
Debajo están los botones para eliminar, crear perfiles y recargar el archivo.
La vista solo muestra mensajes cuando hay un error; el guardado es automático.

Antes de escribir se validan los valores globales y todos los perfiles con las
mismas reglas y opciones compartidas con la CLI. Un error se muestra en la vista
y conserva el último archivo guardado. Un archivo inválido o de una versión
desconocida deshabilita la edición; se puede corregir externamente y pulsar
«Recargar configuración». Este botón también descarta ediciones pendientes.

El guardado reemplaza el archivo de forma atómica y detecta cambios externos:
si otro programa lo modifica, hay que recargarlo antes de guardar. Se conservan
los ajustes admitidos, pero se normaliza el TOML y no se conservan sus comentarios
ni su disposición original. No se almacena el PIN ni se ejecuta `pincmd` al editar.

## Acerca de

La vista muestra el icono redondo SVG de `packaging/icon-rounded.svg`,
`CR Sign vX.X.X` con la versión de CMake, una descripción, el copyright y enlaces
a la licencia GPL-3.0-or-later, el código fuente y el registro de incidencias.
El icono se incorpora como recurso GResource en el ejecutable, para que funcione
sin depender del directorio de trabajo ni de archivos externos de la instalación.

## Validar documentos

La sección Validar acepta uno o varios archivos firmados. Se pueden arrastrar
sobre la sección o pulsar el área de selección para abrir el selector de
archivos de GTK, que utiliza el diálogo nativo o el portal del escritorio cuando
está disponible. No hay restricciones de extensión: el motor reconoce los
contenedores CAdES, PAdES, XAdES y JAdES.

El área de selección es clickeable, sin fondo de botón, y conserva el indicador
de foco para el teclado. El arrastre negocia siempre una copia de las referencias
a los archivos, aunque el gestor de archivos llegue con «mover» seleccionado,
como puede ocurrir con Nautilus en Wayland. Nunca confirma un movimiento ni
solicita eliminar los originales. Si falla la transferencia, la vista muestra
el error.

La validación empieza inmediatamente en segundo plano. Los archivos se procesan
en cola, uno por uno, y se pueden añadir más mientras se trabaja. Cada archivo
usa su propio flow JSON de validación: un fallo no impide procesar los siguientes.
Si otro flow está ejecutándose en el proceso, la cola espera a que termine.
Los archivos originales se leen sin modificarlos.

Seleccionar un archivo abre sus detalles al lado de la lista. En ventanas pequeñas,
los detalles ocupan toda la sección y «Volver» regresa a los archivos. Cada firmante
dispone de un desplegable para consultar la firma criptográfica, la confianza
de la cadena, el formato, los firmantes, los titulares y emisores de certificados,
su vigencia y huella SHA-256, los niveles detectados y validados, y los sellos de
tiempo disponibles. Una firma criptográficamente correcta con un certificado no
confiable se muestra como fallo de confianza. La comprobación de revocación se
identifica por separado; no se infiere que un certificado no esté revocado cuando
no se ha comprobado.

Se aplican los certificados de confianza globales de `crsign.toml`: `ca`,
`intermediates`, `tsa.ca`, `tsa.intermediates`, `no_bundled_certs` y `cert_bundles`.
Los perfiles de firmado, módulos PKCS#11 y comandos de PIN no se utilizan para
validar. La configuración se lee al iniciar el trabajo de cada archivo.

Si una firma separada requiere su archivo original, la fila ofrece «Elegir
contenido original…» y vuelve a validarse al seleccionarlo. «Limpiar resultados»
retira las filas cuando no hay trabajo ni diálogos pendientes. Cerrar la ventana
descarta la cola restante y deja terminar la lectura en curso sin actualizar la
interfaz destruida. No se descargan evidencias de revocación desde la red.

Con las pruebas habilitadas, Linux puede ejecutar `gui_native_drop` si están
disponibles Xvfb, D-Bus y `xdotool` (incluidos en `nix develop`). La prueba usa
dos procesos GTK y movimientos reales del puntero en un escritorio aislado:
transfiere varios archivos, negocia copia y comprueba que el origen no reciba
una orden de borrado y que los archivos permanezcan intactos.

## Diagnosticar fallos en una cola de firma

Cierra primero la instancia abierta de crsign. Ejecuta desde una terminal y
reproduce el fallo una vez:

```sh
G_MESSAGES_DEBUG=crsign-signing LIBSSIG_PKCS11_TRACE=1 ./build/sign --gui > /tmp/crsign-signing.log 2>&1
```

Las líneas `crsign-signing-DEBUG` indican el número de archivo en la cola,
la etapa, el resultado, el tiempo empleado y el `CK_RV` de firma. Las trazas
propias de ese dominio no incluyen PIN, rutas de documentos, contenido,
identificadores del token ni certificados. Otros mensajes de la aplicación o
del driver pueden aparecer en el mismo archivo.

Compara el último `phase=begin` con su `phase=end`: `modules-open` identifica
la carga e inicialización del driver; `tokens` y `certificates`, la lectura;
`pin-provider`, la obtención del PIN; `libssig-sign`, la firma y sus servicios
de validación; `context-close`, la liberación del contexto; `output-write`,
el guardado. Un `status` de timestamp/evidencias con `CK_RV=0` no demuestra un
fallo del driver. Un código PKCS#11 tampoco identifica por sí solo su origen.

Al informar un fallo, indica si ocurre en el primer archivo o en uno posterior,
si los mismos archivos funcionan individualmente, el perfil/nivel utilizado y
el modelo y versión del driver. Conserva el registro de esa ejecución para
comparar las etapas sin repetir firmas a ciegas.

`LIBSSIG_PKCS11_TRACE=1` agrega líneas `libssig-pkcs11` con el nombre de la
llamada y su retorno: apertura/cierre de sesión, login de usuario y contextual,
consulta de autenticación, `C_SignInit` y las dos llamadas de `C_Sign` para
consultar longitud y obtener la firma. No registra los argumentos de esas
llamadas. Permite distinguir un login rechazado de un `C_Login` exitoso seguido
de `CKR_USER_NOT_LOGGED_IN` al preparar o ejecutar la firma.

Para investigar dentro de una compilación local de OpenSC en Linux, puede
compilarse por separado el observador de etapas con los encabezados de esa misma
compilación. No forma parte del build normal ni cambia el driver instalado.
Ejecuta estos comandos desde la raíz del repositorio crsign:

```sh
nix develop --command cc -std=gnu99 -Wall -Wextra -Werror -shared -fPIC \
  -I../opensc/src -I../opensc/build scripts/trace-opensc-sign.c \
  -ldl -o build/trace-opensc-sign.so
LD_PRELOAD="$PWD/build/trace-opensc-sign.so" \
  G_MESSAGES_DEBUG=crsign-signing LIBSSIG_PKCS11_TRACE=1 \
  ./build/sign --gui > /tmp/crsign-signing.log 2>&1
```

Cierra la instancia previa antes de lanzarlo. Las líneas `opensc-sign` muestran
entrada y resultado de `sc_pkcs15_compute_signature`, selección de archivo,
`sc_set_security_env` y `sc_compute_signature`. Solo distinguen selección de
raíz, aplicación u otro archivo. Durante el login y la firma también registran
las categorías `SELECT`, `VERIFY(data/status)`, `MSE` y `PSO`, con su retorno y
palabra de estado. No imprimen identificadores, claves, PIN, cuerpos de APDU,
contenido ni firmas. La ausencia de líneas no demuestra ausencia de
llamadas: una biblioteca enlazada sin interposición puede impedir observarlas.
Retira `LD_PRELOAD` al terminar el diagnóstico.
