# Registro de personas por tótem

**Hecho (2026-10-25), sin probar a mano.** En Personas → Registro se crean uno o
varios **tótems** por evento: una página web (`/registro/`) que se abre en un
televisor con cámara y micrófono, una tablet o el celular de quien escanea su QR.
La persona pone la cara, el asistente la reconoce, la saluda con voz («Hola Ana,
te damos la bienvenida a…»), le pide **hablando** los datos que se configuraron y
le contesta **con voz**. Lo que contesta va al padrón del evento y queda
acreditada. Lo mismo por MCP (`totem_*`, categoría `totems`; tema de ayuda
`totems`).

## Las decisiones

**Un guion, no un agente.** Quien conversa es un guion determinista
(`src/app/registro/Conversacion`, puro y con test): tiene que contestar en el acto
y con varias personas a la vez, y lo que pide lo decidió el organizador. Lo
difícil es **entender**: los normalizadores pasan «jorge punto ortega arroba gmail
punto com» a `jorge.ortega@gmail.com`, «once cuatro cinco seis…» a dígitos,
«dos mil veinticinco» a 2025, «dale», «de una», «ni ahí» a sí/no, «me llamo jorge
ortega» a «Jorge Ortega». Si no entiende reintenta dos veces (la segunda ofrece
tipear en la pantalla) y, si el dato no es obligatorio, lo saltea. Con
«confirmar» repite cada dato («Anoté … ¿Está bien?»); lo tipeado no se confirma.
Dos silencios largos seguidos (12 s cada uno) cortan sin guardar.

**Los datos van al padrón, no a la libreta.** `Asistente::datos` (un objeto JSON
libre, clave → valor) y `Asistente::registro` (el id del tótem); `porDonde:
"registro"`, acreditado y presencial. Los datos de un evento caducan con el evento
(cabecera de `evento/Padron.hpp`). Si la cara no se conocía se la da de alta en
Personas con su foto (`PersonasViewModel::altaConFoto`, con el nombre si lo dijo).
Padrones viejos sin `datos` cargan igual.

**Evento es todo.** La configuración es del evento abierto:
`Eventos/<x>/registro.json` (`src/app/registro/Totems`). Por tótem: `{id, nombre,
activo, titulo, subtitulo, saludo, despedida, yaRegistrado, confirmar, reconocer,
campos: [{clave, etiqueta, pregunta, tipo, obligatorio, opciones?, ejemplo?}]}`,
con tipo `texto | nombre | correo | telefono | numero | sino | opcion | documento`
(este último con `documentos`: los tipos aceptados; ver «El DNI por la cámara»). Los textos
admiten `{evento}`, `{nombre}` y `{hola}` («Hola» o «Hola Ana»). Uno nuevo pide
nombre (obligatorio) y correo.

**La url es la credencial.** `<origen>/registro/#r=<id>&t=<token>`: token de 256
bits (64 hex) en el **fragmento** (no llega al servidor por la url). El origen es
el del servicio `registro` de las direcciones (el mismo puerto HTTPS que la sesión
remota; si falta, el de `sesion`). El token y su sal se guardan cifrados en el
almacén de secretos (`registro-<sha256(carpeta|id)>`), como los links de la
sesión remota: la url se puede volver a mostrar; «renovar» la cambia. Para una TV
que solo muestra: `<origen>/registro/?pantalla=1#r=…&t=…` (dibuja el QR de la url
sin `pantalla=1`).

**Quién está frente al tótem.** La cámara sube por WebRTC, junto con el micrófono,
al canal de video `BA.Registro.<k>` (como el lobby). En la app es la clave
`registro:<k>` de `FrameService`: la misma imagen se reconoce (cada 0,6 s, fuera del
hilo de la interfaz, `PersonasViewModel::identificarFoto`, una por vez por conexión)
y se puede **mirar**: en la pestaña Registro, en un monitor (Pantallas) o donde se
muestre una clave de video. Al soltar el canal el gateway lo pone en negro, para
que la cara no quede congelada. Dos cuadros seguidos con la misma cara arrancan la
conversación. Tomar
canal también la arranca: si a los 2,5 s no apareció una cara, empieza sin ella
(solo la primera vez por canal). Con `?kiosco=1` el mismo canal atiende a varias
personas: después de terminar, la próxima arranca cuando esa cara se va (o aparece
otra conocida), o con «Tocá para empezar».

**Hasta cuatro a la vez.** Los canales de audio 1..4 del gateway. La voz de la
persona sube por WebRTC a `BA.Audio.Registro.<k>` (`OperatorFeed::escucharRegistro`)
y se pasa a texto con el **mismo** reconocedor del dictado (`agent::Voz::abrirOido`,
clave `registro:<conn>`). El asistente habla con **la voz del atendedor**
(`audio/Habla`, la de `AtencionViewModel`) y baja por `BA.Audio.RegistroVoz.<k>`
(`emision/VozAlLobby`, ahora con el canal como parámetro) solo a esa conexión.
Mientras habla, al oído le llegan ceros: no se transcribe a sí mismo (y WebRTC le
cancela el eco en el navegador). Sin voz a texto o sin voz, la página muestra el
texto y se puede tipear.

## El DNI por la cámara (campo `documento`)

Un campo de tipo `documento` no se pregunta: se **muestra**
(docs/funciones/DOCUMENTOS.md). El asistente dice «Mostrame tu DNI a la cámara, del
lado del código de barras» («tu documento» si el campo no acepta el DNI) y la
conversación pasa al estado `Documento`; la página enmarca la cámara (estado
`documento`) y ofrece tipear el número. Mientras dure el paso, `TotemsViewModel` manda
un cuadro de `registro:<k>` cada 300 ms (no se descartan: uno por conexión) al lector
de documentos (`ui/LectorDocumentos`: zxing fuera del hilo de la interfaz, el DNI de
fábrica y los tipos de los plugins), con los tipos que acepta el campo.

- **Lo leyó:** con «confirmar», «Sos Juana Andrea Prueba, DNI 11.222.333. ¿Está bien?»;
  con «no», a mostrarlo de nuevo. Al aceptarlo, el documento entero queda en
  `datos[<clave>]` (objeto: los datos normalizados con `origen: "codigo"`) y se completan
  los campos sin dato que salen de ahí: los de tipo `nombre` (nombre y apellido) y los de
  texto/número con clave `apellido`, `nombres`, `dni`, `nacimiento`, `cuil` o `sexo`. Lo
  que ya se sabía por la cara (el nombre de la ficha) no se pisa.
- **No lo leyó en 15 s:** le dice qué corregir (tenelo quieto, del lado del código, sin
  reflejo; «ese código no es de un documento que pueda leer» si vio otro código) y vuelve
  a buscar. A la segunda, si no es obligatorio, lo saltea («seguimos sin el documento»);
  si es, le pide que tipee el número y sigue buscando.
- **Respaldo:** tipear el número en la pantalla (`{numero, origen: "tipeado"}`, sin
  confirmar) o dictarlo («once, dos dos dos…», se confirma). «No tengo» lo saltea si no es
  obligatorio. Los silencios no cuentan mientras lo muestra.
- En el registro de la app, solo «documento leído (dni-ar …333)». Por MCP los documentos
  salen resumidos (ver DOCUMENTOS.md, «Los datos son sensibles»).
- En la interfaz: en el editor del campo, qué tipos acepta (los de fábrica y los de los
  plugins que corren; ninguno marcado = cualquiera). Los registrados muestran el documento
  como «Juana Andrea Prueba · DNI 11.222.333».

## La interfaz con el gateway (lo que quedó)

API de control (loopback + Bearer):

- `POST /api/v1/registro/totems {totems: [{id, nombre, sal, hash_token}]}` — la
  lista entera de los activos; `hash_token` = sha256 hex en minúsculas de
  `(sal + token)`, igual que la sesión remota. La app la republica al abrir el
  evento, al cambiar algo y cuando el gateway vuelve.
- `GET /api/v1/registro/estado` → `{conexiones: [{conn, id, canal, ip, lugar,
  desdeMs, audio}], totems, canales}` (la app lo pide al reconectarse).
- `POST /api/v1/registro/enviar {conn, msg}` o `{id, msg}` (a todas las de un tótem).
- `POST /api/v1/registro/cortar {conn}` — también la única forma de soltar un canal
  desde la app.

SSE `registro`: `{tipo: "conexion", conn, id, conectado, ip, lugar}`,
`{tipo: "canal", conn, id, canal}` (0 = lo soltó), `{tipo: "audio", conn, id,
listo}` (el WebRTC de micrófono y cámara), `{tipo: "toque", conn, id, accion: "empezar" |
"cancelar" | "escrito", clave?, valor?}` y `{tipo: "intento", id, ip}` (un token
errado: va al registro de la app).

App → página (por `enviar`): `pantalla {titulo, subtitulo}`, `dice {texto}`,
`oido {texto}`, `estado {estado: esperando | escuchando | hablando | pensando |
foto | documento | listo | error}`, `campos {lista: [{clave, etiqueta, valor, listo}]}`, `pedir
{clave, etiqueta, tipo, ejemplo}`, `listo {texto}`, `reiniciar`. El gateway le
manda además `audio-error` y `cortado`. La página manda `soltar` al recibir
`listo` (en el celular; con `?kiosco=1` se queda con el canal).

## Dónde está

- Configuración y guion: `src/app/registro/Totems.{hpp,cpp}`,
  `src/app/registro/Conversacion.{hpp,cpp}`; tests `test_registro_totems`,
  `test_registro_conversacion` (con el paso del documento: leído, confirmado, corregido,
  fallo y salteo, tipeado, dictado); el padrón en `test_padron`.
- El documento: `src/core/documentos/` y `src/app/ui/LectorDocumentos.{hpp,cpp}`
  (docs/funciones/DOCUMENTOS.md).
- La app: `src/app/ui/TotemsViewModel.{hpp,cpp}` (`Studio.totems`; se llama así
  porque `Studio.registro` son los logs), `src/app/qml/controls/PanelTotems.qml`
  (Personas → pestaña Registro).
- MCP: `src/app/mcp/McpTotemTools.cpp` — `totem_listar`, `totem_crear`,
  `totem_cambiar`, `totem_borrar`, `totem_enlace`, `totem_proyectar`,
  `totem_estado`, `totem_registrados`. `ui_show_page {page: "personas", seccion:
  "libreta" | "registro"}`.
- Catálogo de pantallas: cada tótem activo es `registro:<id>` (contenido `url` con
  `?pantalla=1`); QR elegible `registro:<id>` (`QrViewModel::paraElegir`); fila
  «Registro de personas (tótems)» en Enlaces.
- El gateway y la página: `src/gateway/Registro.{hpp,cpp}`, `assets/web/registro/`.

## Falta probar a mano

- El paso del documento con un DNI de verdad: distancia, luz, reflejo; la confirmación
  hablada; tipear el número en el celular.

- Con una TV o tablet con cámara y un celular: reconocer, saludar por el nombre,
  dictar un correo y un teléfono, corregir con «no», tipear, cancelar.
- Que la voz del asistente no se transcriba (el margen de 350 ms al terminar).
- Cuatro conversaciones a la vez y la quinta esperando.
- Chromecast con `?pantalla=1` sobre el HTTPS del aparato (certificado propio: la
  TV puede no aceptarlo; con el túnel anda). Si no, «Un código QR» con el QR
  `registro:<id>` (va por HTTP) muestra lo mismo.
