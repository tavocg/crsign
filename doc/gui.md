# Interfaz gráfica

```sh
./build/sign --gui
```

La ventana usa GTK 4 y libadwaita. Tiene cinco vistas: Inicio, Firmar, Validar,
Ajustes y Acerca de. Por ahora, cada vista muestra únicamente su nombre; el
contenido se añadirá en pasos posteriores.

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
