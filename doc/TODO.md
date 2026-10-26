- [ ] Reemplazar el ícono de la sidebar con un spinner mientras
      finaliza la cola (similar a como ocurre al listar devices).
- [ ] Mientras se esté firmando o validando, permitir visitar otras secciones y
      que sigan las colas en segundo plano, si este ya es el caso, verificar que
      Usuario clickea firmar -> firmado -> automáticamente lleva a la sección de validación.
      Usuario clickea firmar -> visita otra sección -> firmado -> no lleva a la sección de validación.
- [ ] Si se está firmando una cola, pero, al momento
      de terminar el usuario se encuentra en otra sección que no sea la de
      firmado (incluso la de validación), que muestre una notificación de
      "n/N archivos firmados".
- [ ] Agregar una barra de carga en cada documento de la
      cola tanto de firmado como de validado, así se podría verificar por cuál
      archivo va actualmente la cola, antes de implementar esto, determinar los
      requisitos bien, ya que puede ser que se tenga que modificar libssig para
      permitir un callback o algo similar para pollear el estado.
- [ ] En la cola de firmado, ir moviendo los archivos firmados a la cola de validación automáticamente.
      - Esto igual debe preservar la funcionalidad de cuando el usuario está en
        la sección de firmado al finalizar la firma, automáticamente lo mueve a
        la sección de validación.
      - Esto igual debe preservar la funcionalidad de cuando el usuario está en
        otra sección que no sea la de firmado al finalizar la firma, envía la
        notificación n/N archivos firmados, sin redirigir a la sección de validación.
- [ ] Evaluar la posibilidad de usar polkit o su equivalente en Windows/Mac,
      para digitar el pin, con el prompt actual como fallback.
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
