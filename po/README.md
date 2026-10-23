# Traducciones de la interfaz

La GUI usa GNU gettext con el dominio `crsign` y catálogos UTF-8. Los textos
originales están en inglés; `es.po` aporta la traducción española. Se respeta
el idioma del sistema, con inglés como alternativa cuando no hay traducción.
Los mensajes de la CLI siguen usando su idioma actual.

- `POTFILES.in`: archivos fuente de los que se extraen mensajes.
- `crsign.pot`: plantilla para traductores.
- `LINGUAS`: idiomas que se compilan e instalan.
- `<idioma>.po`: traducciones editables; no se versionan los binarios `.mo`.

Los textos nuevos se marcan con `_()` o `N_()` desde `sign/gui/i18n.h`.
`N_()` permite marcar las etiquetas de estructuras estáticas y traducirlas
cuando se construyen los widgets. Los identificadores internos de navegación
no se traducen.

Para extraer mensajes y actualizar los catálogos existentes:

```sh
cmake --build build --target update-translations
```

Edita los `.po` y vuelve a compilar. El objetivo `translations` ejecuta
`msgfmt --check` y forma parte de la construcción de la GUI, aunque las pruebas
estén deshabilitadas. Los catálogos quedan en `build/locale` y se instalan en
`share/locale/<idioma>/LC_MESSAGES/crsign.mo` por defecto.

Para añadir un idioma, por ejemplo francés:

```sh
msginit --input=po/crsign.pot --locale=fr --output-file=po/fr.po
```

Añade `fr` a `po/LINGUAS`, traduce `po/fr.po` y reconfigura CMake. Para probar
el español desde el árbol de compilación:

```sh
LC_ALL=en_US.UTF-8 LANGUAGE=es ./build/sign --gui
```

Las instalaciones resuelven los catálogos respecto al prefijo del ejecutable,
incluidas las instalaciones reubicadas. En desarrollo se usa `build/locale`
cuando no hay catálogos instalados. Una compilación headless no requiere
libadwaita ni las herramientas de gettext.

La localidad indicada debe existir en el sistema. Gettext mantiene los textos
en inglés con `LC_ALL=C` o `C.UTF-8`, incluso si se define `LANGUAGE`.
