# Documentos de identidad por su código de barras

**Hecho (2026-10-26), probado con tests, sin probar a mano con una cámara y un DNI.**
El DNI argentino trae en el frente un código de barras 2D (PDF417) con los datos de
la persona. El programa lo lee de una cámara (la del tótem de registro, o cualquier
clave de video) y lo convierte en **datos normalizados**, iguales para cualquier
documento. Otros documentos (la licencia de conducir de EE.UU., el documento de otro
país, una credencial con QR) entran **por plugin**.

(No confundir con los «documentos» del evento —los PDF de la pestaña Documentos,
herramientas `documento_agregar`, `documento_texto`…—: acá son documentos de
IDENTIDAD, categoría MCP `identidad`.)

## Las piezas

| Qué | Dónde |
|---|---|
| Los datos normalizados, la interfaz «tipo de documento» y el catálogo | `src/core/documentos/Documento.{hpp,cpp}` (C++ puro, en `ba_core`) |
| El DNI argentino (de fábrica) | `src/core/documentos/DniArgentino.{hpp,cpp}` |
| Leer códigos de una imagen (PDF417, QR, DataMatrix, Aztec, Code128, Code39) | `src/core/documentos/lector/LectorCodigos.{hpp,cpp}`, biblioteca `ba_codigos` (zxing-cpp) |
| Junta las dos cosas, fuera del hilo de la interfaz, y pregunta a los plugins | `src/app/ui/LectorDocumentos.{hpp,cpp}` (uno solo, de `TotemsViewModel`) |
| Los tipos de los plugins | `@plugin.documento` del SDK (`src/plugins/sdk/ba_plugin/__init__.py`), `plugins::Plugins::tiposDocumento` |
| El paso del tótem | `registro/Conversacion` (puro, con test) y `ui/TotemsViewModel` (ver REGISTRO-TOTEM.md) |
| MCP | `documento_leer`, `documento_tipos` (en `src/app/mcp/McpTotemTools.cpp`, categoría `identidad`) |

## Los datos normalizados

`{tipo, pais, numero, apellido, nombres, sexo, nacimiento, emision, vencimiento,
nacionalidad, tramite, ejemplar, cuil, crudo, extra}` — todo texto, las fechas en ISO
(`1990-03-15`), el sexo `M`/`F`/`X`, los nombres con mayúscula inicial (el código no
trae tildes: «Perez»). Lo que el documento no trae queda vacío: **nunca se inventa**.
`extra` es para lo que un tipo de plugin quiera sumar (estado, clase de la licencia).
Del tótem además viene `origen`: `codigo` (leído), `tipeado` o `dicho` (solo el número).

## El DNI argentino

Dos formatos en la calle; se aceptan los dos (los tests usan datos SINTÉTICOS):

- **Nuevo** (DNI tarjeta desde ~2012): 8 o 9 campos separados por «@»:
  `trámite@apellido@nombres@sexo@DNI@ejemplar@nacimiento@emisión[@cuil]`, por ejemplo
  `00500000001@PRUEBA@JUANA ANDREA@F@11222333@A@15/03/1990@20/04/2016@274`. El noveno
  son las dos primeras cifras del CUIL y la última («274» → 27-11222333-4): se usa
  **solo si el dígito verificador cierra**.
- **Viejo** (~2009-2012): empieza con «@», 16 o 17 campos:
  `@DNI@ejemplar@?@apellido@nombres@nacionalidad@nacimiento@sexo@emisión@trámite@?@vencimiento@…`
  (el número viene con espacios atrás y a veces una letra adelante).

Lo esencial para aceptarlo: un número de 6 a 9 dígitos y una fecha de nacimiento que
sea fecha. Un código de otra cosa que casualmente tenga arrobas (un correo, una url)
no pasa.

## Modular: los tipos

Un **tipo de documento** dice si un código es suyo y lo traduce. Se identifican por id:
`dni-ar` (de fábrica) o `plugin:<plugin>/<nombre>` (de un plugin que corre).
`LectorDocumentos::traducir` prueba, para cada código leído, los tipos **aceptados** en
orden (vacío = todos, los de fábrica primero): los de fábrica en el acto, los de los
plugins por su canal con un tope de 3 s (un plugin colgado no traba nada: se sigue con
el próximo). Qué tipos acepta cada campo `documento` de un tótem es configuración
(`documentos: ["dni-ar", "plugin:documento-aamva/licencia"]`): **cambiar de tipo de DNI
es elegir otro tipo, sin tocar el código**.

### Un tipo por plugin

```python
import ba_plugin as ba
plugin = ba.Plugin()

@plugin.documento("licencia", titulo="Licencia de conducir (EE.UU.)", formatos=["PDF417"], pais="US",
                  como_se_dice="tu licencia", ejemplos=[{"formato": "PDF417", "texto": "@\n...ANSI 636014..."}])
def licencia(codigo):          # {"formato", "texto", "bytes"}
    if "ANSI " not in codigo["texto"]:
        return None            # no es mío: la app sigue con otro tipo
    return {"numero": "...", "apellido": "...", "nombres": "...", "nacimiento": "1990-03-15"}

plugin.correr()
```

- Declara `{"t": "documento", nombre, titulo, formatos, pais, como_se_dice, descripcion,
  ejemplos}`; la app le pide `pedido {accion: "documento", args: {nombre, formato,
  texto, bytes_b64}}` y espera `{"datos": {...} | null}`. Sin `numero` cuenta como no
  leído; lo que no es un campo normalizado va a `extra`.
- `plugin_probar` corre los `ejemplos` (con `"espera": false` los que NO tiene que leer)
  y muestra qué devolvió cada uno.
- El ejemplo que viene con el programa: **`documento-aamva`** (licencias de conducir de
  EE.UU. y Canadá, PDF417 AAMVA), categoría «Documentos de identidad» en «Nuevo desde
  un ejemplo». Sirve de molde para cualquier otro documento con un código parseable.

## Leer el código: zxing-cpp

[zxing-cpp](https://github.com/zxing-cpp/zxing-cpp) (Apache-2.0) lee bien el PDF417
(zbar no). **Versión 2.3.0, SHA-256 fijo** (`versions.cmake`, `ZXING_CPP_VERSION` /
`ZXING_CPP_SHA256`), construido **desde la fuente en los tres sistemas** como libdeflate:
`cmake/BaZxing.cmake` usa `extern/zxing-cpp` si `make deps-zxing` (o `make deps-src`)
la bajó, y si no baja el tarball del tag y verifica el hash. Solo lectores y el escritor
viejo (sin zint; lo usa el test para generar un PDF417), estático, sin ejemplos. Va
fuera del sub-build /MT de Windows: el launcher y el gateway no leen códigos. Si no se
puede traer, la configuración frena (`ba_falta`). La licencia viaja en
`data/licencias/zxing-cpp-Apache-2.0.txt`.

El lector prueba girado, chico y espejado (la cámara de adelante de una tablet; zxing lo
hace solo para PDF417 y QR). En un cuadro de 1280x720 tarda decenas de milisegundos: por
eso corre en el hilo propio de `LectorDocumentos`.

## Los datos son sensibles

- Quedan en el **padrón del evento** (local, `datos[<clave>]` del asistente), que
  caduca con el evento. No van a ningún servicio de afuera.
- En los registros, a lo sumo `documentos::paraLog`: el tipo y los últimos tres dígitos
  («dni-ar …333»).
- Por **MCP** (el asistente puede ser un modelo en la nube) salen resumidos:
  `documento_leer` devuelve `{tipo, nombre, numero: "…333"}`, `totem_registrados` pone
  los documentos en ese resumen, y `totem_estado` tapa los números largos de lo dicho y
  oído (`documentos::taparNumeros`).
- Los plugins: la ayuda les pide no mandarlos afuera ni imprimirlos enteros.

## MCP (categoría `identidad`)

| Herramienta | Para qué |
|---|---|
| `documento_tipos` | Los tipos que se entienden (de fábrica y de plugins) y si la compilación trae el lector |
| `documento_leer` | Buscar códigos en una clave de video (`registro:<canal>`), una imagen (`archivo`, `imagen_base64`) o un texto ya leído, con los tipos aceptados (`tipos`); devuelve el resumen |

## Tests

- `test_documentos` (ba_core): las ayudas (fechas, mayúsculas con Ñ y partículas, puntos,
  CUIL, el log y el resumen que no muestran el documento), el DNI nuevo (con y sin CUIL,
  con un CUIL que no cierra), el viejo, y lo que no es un DNI (basura, campos de menos,
  un correo, fecha imposible, número corto); el catálogo con los aceptados.
- `test_codigos` (ba_codigos): **genera con zxing un PDF417 con un DNI sintético**, lo pega
  en un cuadro BGRA de 1280x720, lo lee de vuelta (también espejado y con bytes de relleno
  por fila) y sale el mismo DNI; un QR; una imagen vacía.
- `test_registro_conversacion`: el paso del documento (ver REGISTRO-TOTEM.md).
- `test_plugins_ejemplos`: los ejemplos de `documento-aamva` (dos licencias que lee, un DNI
  y un QR que no).

## Falta probar a mano

- Con un DNI de verdad frente a la cámara de una TV, una tablet y un celular: a qué
  distancia lo lee, con qué luz, con el reflejo del plástico. Si cuesta, subir la
  resolución que manda la página o recortar el centro del cuadro antes de leer.
- Un DNI viejo (formato que empieza con «@») real.
- El plugin AAMVA con una licencia real (los ejemplos son sintéticos).
- Windows y Linux: que zxing compile en MSVC/MinGW (Docker) y en el gcc de Linux.
