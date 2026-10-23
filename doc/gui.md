# Interfaz gráfica

```sh
./build/sign --gui
```

La ventana usa GTK 4 y libadwaita. Tiene cinco vistas: Inicio, Firmar, Validar,
Ajustes y Acerca de. Ajustes permite editar la configuración y los perfiles;
Acerca de muestra la identidad y los enlaces del proyecto. Validar permite
procesar varios archivos firmados. Inicio y Firmar conservan por ahora su texto
de identificación.

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
  estado predeterminado es Automático.
- **Automático**: usa la selección automática o el valor predeterminado del
  motor. En perfiles escribe un valor vacío explícito para ignorar el global;
  en los ajustes globales elimina el campo, porque allí no hay nada que heredar.
- **Personalizado**: permite escribir un valor o elegir una opción concreta.

Las cadenas vacías (`token = ""`, `hash = ""`, etc.) restauran los valores
predeterminados correspondientes. Las opciones booleanas también admiten `""`
para ese estado, además de `true` y `false`. En listas, el valor vacío es `[]`:
`modules = []` usa p11-kit y `pincmd = []` solicita el PIN interactivamente.
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

Cada fila se puede desplegar para consultar la firma criptográfica, la confianza
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
