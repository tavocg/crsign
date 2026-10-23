# CRSign

Firmador digital en C, con soporte para CAdES, PAdES, XAdES, JAdES con
validación de largo plazo.

## Uso

```sh
sign tokens                # listar tokens
sign doc.pdf               # firmar
sign verify doc-signed.pdf # verificar
```

Consulta los [ejemplos de uso y configuración](doc/examples.md) para firmas
visibles, perfiles, módulos PKCS#11 y opciones de confianza.

Las instrucciones de compilación están en la [guía de construcción](doc/build.md).
