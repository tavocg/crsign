- [ ] Buscar la mejor manera de representar las firmas, actualmente aparecen
      campos como CN=..., pero sería mejor que eso vaya en su propio campo y
      mostrar el título diferente, idealmente por ejemplo el nombre de la
      persona nada más, pero, hay que investigar si eso es estándar.
- [ ] Pulir comportamiento con varios lectores/tarjetas (también dar a escoger
      cuando hayan varios certificados en una tarjeta).
- [ ] La firma del appimage y del msi por alguna razón tarda demasiado, cuando
      se usa el ejecutable local es más rápido.
- [ ] Evaluar la posibilidad de usar polkit o su equivalente en Windows/Mac, con
      el prompt nativo para el pin como fallback.
- [ ] Trazar tratamiento del PIN, ya que es la superficie de ataque de este
      programa, verificar todos los lados donde se manipula que se haga de
      manera segura.
- [ ] Terminar de configurar .clang-format
- [ ] Empaquetar (ya existe el workflow, pero es mejor testear los previews
      antes de sacar la tag) y testear GUI y CLI en Linux/Windows. También la
      desinstalación. De momento únicamente los ficheros en releases, empaquetar
      para distribuciones y demás debería depender de otros repositorios?
- [ ] Formalizar documentación, crear sitio web, screenshots.
- [ ] Tag y Release.
