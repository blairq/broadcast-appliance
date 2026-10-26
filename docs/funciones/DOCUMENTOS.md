# Documentos de identidad por su código de barras

**Hecho (2026-10-26), probado con tests, sin probar a mano con una cámara y un DNI.**
El DNI argentino trae en el frente un código de barras 2D (PDF417) con los datos de
la persona. El programa lo lee de una cámara (la del tótem de registro, o cualquier
clave de video) y lo convierte en **datos normalizados**, iguales para cualquier
documento. Si el código no sale, se lee el **dorso**: los tres renglones de la zona de
lectura mecánica (MRZ), que transcribe un modelo de visión (el local, sin que la foto
salga de la máquina; si no, el propio del operador) y se validan con sus dígitos
verificadores. Otros documentos (la licencia de conducir de
EE.UU., el documento de otro país, una credencial con QR) entran **por plugin**.

(No confundir con los «documentos» del evento —los PDF de la pestaña Documentos,
herramientas `documento_agregar`, `documento_texto`…—: acá son documentos de
IDENTIDAD, categoría MCP `identidad`.)

## Las piezas

| Qué | Dónde |
|---|---|
| Los datos normalizados, la interfaz «tipo de documento» y el catálogo | `src/core/documentos/Documento.{hpp,cpp}` (C++ puro, en `ba_core`) |
| El DNI argentino (de fábrica: frente y dorso) | `src/core/documentos/DniArgentino.{hpp,cpp}` |
| El dorso: la MRZ TD1 (parser, verificadores, correcciones, el pedido al modelo y su respuesta) y el tipo genérico `mrz-td1` | `src/core/documentos/Mrz.{hpp,cpp}` (C++ puro, en `ba_core`) |
| Mostrarle una imagen a un modelo de visión, local o propio (lo comparten `imagen_preguntar` y el dorso) | `src/app/agent/Mirada.{hpp,cpp}`, `MiradaCuerpo.cpp` |
| La visión del modelo local: el complemento del catálogo y `--mmproj` | `datos/modelos-catalogo.json` (tipo `vision`), `agent/ModelosLocales::mmprojPara`, `agent/ServidorModeloLocal` |
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

## El dorso: la zona de lectura mecánica

Pedido del usuario: «si no anda el código de barras, que pida la parte de atrás y le haga
OCR». El dorso del DNI (y de las cédulas de muchos países) trae abajo tres renglones de
30 caracteres `A-Z0-9<` con el formato **TD1 de la OACI** (Doc 9303, parte 5):

```
IDARG11222333<5<<<<<<<<<<<<<<<    tipo, emisor, número (6-14) y su verificador (15), opcionales
9003152F3404207ARG<<<<<<<<<<<4    nacimiento+verif, sexo, vencimiento+verif, nacionalidad, opcionales, compuesto
PRUEBA<<JUANA<ANDREA<<<<<<<<<<    apellido<<nombres
```

(sintético).

### Quién hace el OCR: un modelo de visión

No se vendorea ningún motor de OCR (pedido del usuario: «probá hacer el OCR con el modelo
del asistente nomás»). Los renglones los transcribe un modelo de visión
(`agent/Mirada`), en este orden:

1. **El modelo local**, con el proyector de visión de su modelo base: el llama-server
   del programa (`agent/ServidorModeloLocal`) carga `--mmproj` si está bajado el
   complemento **«Visión para los modelos de 4B»** del catálogo de modelos locales
   (`unsloth/Qwen3.5-4B-mmproj-F16`: `mmproj-F16.gguf` de `unsloth/Qwen3.5-4B-GGUF`,
   revisión `e87f176479d0855a907a41277aca2f8ee7a09523`, sha256
   `cd88edcf8d031894960bb0c9c5b9b7e1fea6ebee02b9f7ce925a00d12891f864`, 672 MB, Apache-2.0;
   el sha256 es el oid LFS que publica Hugging Face, y la app lo verifica al bajarlo).
   Qué proyector va con qué .gguf lo dice `ModelosLocales::mmprojPara` (la misma `base`
   en el catálogo: los reactivos de 4B son Qwen3.5-4B ajustados). Se baja en
   Configuración → Modelos locales o con `modelo_local_bajar`. Pedido a
   `http://127.0.0.1:<puerto>/v1/chat/completions`: **la foto no sale de la máquina**.
   El tótem lo hace cargar cuando empieza a pedir el documento (cargar tarda) y lo suelta
   cuando nadie muestra un documento; si el chat del Asistente tiene cargado OTRO modelo,
   no se lo cambia (y se usa el propio, si hay). Con el servidor ya cargado con el mismo
   modelo (el chat), lo comparten.
2. **El modelo propio** del operador (`Config/modelo-propio.json` con la credencial
   `modelo-propio-api-key`, el mismo de `imagen_preguntar`), si no hay visión local o no
   contestó. La foto va solo a ese servidor.

Sin ninguno de los dos no hay dorso: `documento_tipos` dice `dorso: {disponible: false,
falta}` y el tótem no lo pide (se tipea el número o se saltea).

**La foto y el pedido** (`agent::pedidoDelDorso`), lo que anduvo en la prueba con un DNI
real (dorso, cámara FaceTime de 1920x1080, ~6 s por foto con el modelo local en una M1
Pro): la foto ENTERA que manda la página del tótem (`cuadro`; las del video WebRTC, a
960x540, daban «NO SE VE»), recortada a la **franja de abajo** (desde el 58 % del alto,
ancho completo), JPEG 95, sin sistema, temperatura 0, 200 tokens, sin pensar, 20 s de
tope, **de a una por conexión**. La pregunta (`documentos::pedidoMrz`): «Esta es la parte
de atrás de un documento de identidad. Abajo hay una zona de lectura mecánica (MRZ) de
tres renglones de 30 caracteres cada uno, solo con A-Z, 0-9 y el signo <. Transcribila
EXACTAMENTE, carácter por carácter, contando bien los <. Contestá SOLO los tres
renglones, uno por línea, sin nada más. Si no se ve, contestá NO SE VE.»

### Validar lo que contesta (un modelo puede inventar)

Pasa por el parser TD1 puro (`documentos::leerTd1`, `interpretarRespuestaMrz`):

- saca los renglones de un texto libre (bloques de código, espacios, minúsculas, «« por
  «<<», los tres pegados) y, si un renglón no tiene 30 (el modelo cuenta mal el relleno:
  en la prueba, 29 y 31), **suma o saca «<» en la tira de «<» más larga**, hasta tres;
  nunca toca un dato;
- corrige por **posición**: dígito donde va dígito (O/Q/D→0, I/L→1, Z→2, S→5, G→6, B→8…),
  letra donde va letra (al revés). En el número (que admite las dos cosas) prueba cambiar
  solo los caracteres de la minoría (en un número casi todo de dígitos, que una letra sea
  un dígito; nunca al revés) y gana la corrección con menos cambios que cierra su
  verificador; si hay dos igual de buenas, no se acepta. Los opcionales NO se corrigen
  (no tienen verificador propio: «arreglarlos» para que cierre el compuesto sería
  inventar);
- **el criterio** (de la prueba real: en 14 fotos, número, nacimiento y vencimiento
  cerraron 14/14; el compuesto, 8/14, por un «<» corrido en los opcionales): tienen que
  cerrar los verificadores del **número, el nacimiento y el vencimiento** (y las fechas
  ser fechas) **Y** el **compuesto** cierra **o otra foto coincide** en todo lo que
  importa (tipo, emisor, número, fechas, sexo, nacionalidad y el renglón del nombre, que
  no tiene verificador): `documentos::ConfirmadorMrz`, uno por conexión, que se reinicia
  cada vez que se vuelve a pedir el documento.

Los renglones aceptados se traducen como un código más, con formato `MRZ`, por los mismos
tipos (`LectorDocumentos::traducir`; los tipos no exigen el compuesto: eso ya lo decidió
quien leyó, como zxing decide si un código está bien leído):

- **`dni-ar`** entiende también el dorso (emisor `ARG`, tipo `I`): el número del DNI en el
  campo del número de documento (posiciones 6-14, con su verificador; así lo describe la
  documentación pública que encontramos, p. ej. Wikipedia «Argentine identity card», y así
  cerró en la prueba real); si ahí no hay un número de DNI, se lo busca en los datos
  opcionales del renglón 1 (por si alguna serie trae otra cosa). Del dorso no salen
  trámite, ejemplar ni CUIL; sí el vencimiento. `extra.lado = "dorso"`.
- **`mrz-td1`** (de fábrica): cualquier cédula OACI con MRZ (otro país, otro documento).
  El catálogo prueba primero `dni-ar`.

La conversación: REGISTRO-TOTEM.md, «El DNI por la cámara».

### Privacidad

Con el modelo local la foto no sale de la máquina; con el propio va **solo** a ese
servidor (el que configuró el operador), nunca a otro servicio: si es un servicio en la
nube, es su servidor y su decisión, igual que con `imagen_preguntar`. Lo que contesta el
modelo no se registra; en los registros, a lo sumo el tipo y los últimos tres dígitos y
con qué modelo se leyó, o el motivo sin datos («no cierra el verificador del número»);
por MCP, el resumen de siempre, y los renglones de un dorso no se devuelven nunca
(tampoco cuando no se aceptan).

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
| `documento_tipos` | Los tipos que se entienden (de fábrica y de plugins, con `dorso: true` los que se leen también por atrás), si la compilación trae el lector y si se puede leer el dorso (`dorso: {disponible, con, falta}`) |
| `documento_leer` | Buscar códigos en una clave de video (`registro:<canal>`), una imagen (`archivo`, `imagen_base64`) o un texto ya leído (`formato: "MRZ"` para unos renglones), con los tipos aceptados (`tipos`); `lado`: `frente`, `dorso` o `auto` (el código y, si no hay documento, el dorso); devuelve el resumen, con qué modelo se leyó y, si el dorso no se aceptó, el motivo (`dudosa`: llamar de nuevo con otra foto de la misma fuente) |

## Tests

- `test_documentos` (ba_core): las ayudas (fechas, mayúsculas con Ñ y partículas, puntos,
  CUIL, el log y el resumen que no muestran el documento), el DNI nuevo (con y sin CUIL,
  con un CUIL que no cierra), el viejo, y lo que no es un DNI (basura, campos de menos,
  un correo, fecha imposible, número corto); el catálogo con los aceptados.
- `test_codigos` (ba_codigos): **genera con zxing un PDF417 con un DNI sintético**, lo pega
  en un cuadro BGRA de 1280x720, lo lee de vuelta (también espejado y con bytes de relleno
  por fila) y sale el mismo DNI; un QR; una imagen vacía.
- `test_mrz` (ba_core): el verificador de la OACI (con el ejemplo de Doc 9303), los
  renglones de un texto libre, la TD1 con sus correcciones por posición, el número largo,
  lo que no se acepta (verificadores que no cierran, fecha imposible, basura, una MRZ
  inventada), el DNI por el dorso (también con el número en los opcionales), `mrz-td1`, el
  catálogo y lo que contesta un modelo: renglones de 29, 31 y 27 ajustados en el relleno,
  el compuesto que no cierra por un «<» corrido (dudosa) y el criterio de dos fotos. Todas
  sintéticas.
- `test_documento_dorso` (app, sin red): el pedido del dorso (la pregunta, la franja de
  abajo de un cuadro de 1920x1080, el cuerpo) y la validación de las respuestas (buena,
  corregible, inventada, «NO SE VE», una descripción, otro país con un campo de DNI,
  dudosa y confirmada por otra foto).
- `test_registro_conversacion`: el paso del documento y el del dorso (ver REGISTRO-TOTEM.md).
- `test_plugins_ejemplos`: los ejemplos de `documento-aamva` (dos licencias que lee, un DNI
  y un QR que no).

## Falta probar a mano

- Con un DNI de verdad frente a la cámara de una TV, una tablet y un celular: a qué
  distancia lo lee, con qué luz, con el reflejo del plástico. Si cuesta, subir la
  resolución que manda la página o recortar el centro del cuadro antes de leer.
- Un DNI viejo (formato que empieza con «@») real.
- El plugin AAMVA con una licencia real (los ejemplos son sintéticos).
- Windows y Linux: que zxing compile en MSVC/MinGW (Docker) y en el gcc de Linux.
- El dorso de punta a punta en la app (tótem → foto → modelo local → confirmar): la prueba
  real se hizo a mano con llama-server y el mismo pedido, no con este código. Que el
  complemento de visión baje y verifique, que llama-server arranque con `--mmproj` en
  Windows (Vulkan) y Linux, y cuánto tarda cargar el modelo la primera vez.
- Con el modelo propio como respaldo (no se probó).
