- [ ] Mientras se esté firmando o validando, permitir visitar otras secciones y
      que sigan las colas en segundo plano, si este ya es el caso, entonces
      sólamente reemplazar el ícono de la sidebar con un spinner mientras
      finaliza la cola (similara como ocurre al listar devices).
- [ ] Evaluar la posibilidad de usar polkit o su equivalente en Windows/Mac, con
      el prompt nativo para el pin como fallback.
- [ ] Comprender funcionalidad de "extender" la firma, ver si es congruente
      implementarlo y en cuyo caso buscar la mejor manera de integrarlo.
- [ ] Pulir comportamiento con varios lectores/tarjetas (también dar a escoger
      cuando hayan varios certificados en una tarjeta).
- [ ] La firma del appimage y del msi por alguna razón tarda demasiado, cuando
      se usa el ejecutable local es más rápido.
- [ ] Terminar de configurar .clang-format
- [ ] Empaquetar (ya existe el workflow, pero es mejor testear los previews
      antes de sacar la tag) y testear GUI y CLI en Linux/Windows. También la
      desinstalación. De momento únicamente los ficheros en releases, empaquetar
      para distribuciones y demás debería depender de otros repositorios?
- [ ] Trazar tratamiento del PIN, ya que es la superficie de ataque de este
      programa, verificar todos los lados donde se manipula que se haga de
      manera segura.
- [ ] Formalizar documentación, crear sitio web, screenshots.
- [ ] Tag y Release.
