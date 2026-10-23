# Interfaz gráfica

```sh
./build/sign --gui
```

La ventana usa GTK 4 y libadwaita. Tiene cinco vistas: Inicio, Firmar, Validar,
Ajustes y Acerca de. Ajustes permite editar la configuración y los perfiles;
las demás vistas conservan por ahora su texto de identificación.

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
