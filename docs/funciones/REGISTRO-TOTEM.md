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

**Un guion, y un modelo que entiende (2026-10-26).** El guion de abajo sigue llevando la
estructura; lo que dice la persona lo entiende además un modelo de lenguaje: ver «El modelo que
entiende». Sin modelo, todo esto sigue igual.

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

**Hasta ocho a la vez.** Varios tótems en distintas puertas (o celulares). Los canales
1..8 del gateway: un solo tope para los dos procesos, `ipc::kCanalesRegistro`
(`src/core/ipc/CanalesRegistro.hpp`; en el gateway `kMaxTotems`, en la app
`FrameService::kRegistroSlots` y `OperatorFeed::kMaxRegistro` salen de ahí). La voz de la
persona sube por WebRTC a `BA.Audio.Registro.<k>` (`OperatorFeed::escucharRegistro`)
y se pasa a texto con el **mismo** reconocedor del dictado (`agent::Voz::abrirOido`,
clave `registro:<conn>`). El asistente habla con **la voz del atendedor**
(`audio/Habla`, la de `AtencionViewModel`) y baja por `BA.Audio.RegistroVoz.<k>`
(`emision/VozAlLobby`, ahora con el canal como parámetro) solo a esa conexión.
Mientras habla, al oído le llegan ceros: no se transcribe a sí mismo (y WebRTC le
cancela el eco en el navegador). Sin voz a texto o sin voz, la página muestra el
texto y se puede tipear. El reconocedor del tótem se elige en la pestaña Agentes (Parakeet de
fábrica o Nemotron, streaming: varias personas a la vez con un solo modelo). Con
Nemotron y «la persona puede interrumpir» (de fábrica prendida), no van ceros: si
la persona habla mientras el asistente habla, la voz se corta en el acto y se la
escucha (`TotemsViewModel::interrumpido`; ver AUDIO-TTS.md, «Dos reconocedores»).

## El modelo que entiende (iniciativa mixta)

**Hecho (2026-10-26), probado contra un servidor falso; sin probar con el modelo local real ni
con gente.** En la prueba con personas el guion solo entendía poco (correos mal dictados, las
correcciones), no contestaba preguntas y obligaba a contestar de a un dato. Ahora:

- **El guion pregunta, el modelo entiende.** El guion (`Conversacion`) sigue preguntando campo
  por campo en su orden, confirma, pide la foto y el documento: es lo que guía a quien no sabe
  qué decir. Pero lo que la persona dice en cada turno va a un modelo de lenguaje
  (`registro/AgenteRegistro`, `registro/AgenteTotem`), sea la respuesta a la pregunta o un
  parrafazo («soy Jorge Ortega, de Mendoza, mi correo es…, mi teléfono…»). El modelo anota
  **cada** dato que reconoce con la herramienta `registro_poner {campo, valor}` (varias en un
  turno) y el guion, al avanzar, saltea lo que ya tiene dato.
- **Los datos los guardan las herramientas, no el modelo.** `registro_poner` pasa por los
  mismos normalizadores del guion (`Conversacion::proponer` → `entender()`): si no valida,
  devuelve el error y el modelo repregunta con sus palabras (una segunda vuelta del mismo
  turno). Lo anotado queda **pendiente** hasta el «sí»: el guion lo confirma todo junto en una
  frase («Anoté nombre Jorge Ortega, correo jorge punto ortega arroba gmail punto com y teléfono
  …; ¿es correcto?»), con los botones «Sí, es correcto» / «No, corregir» de la pantalla. Un
  «sí» o un «no» suelto los resuelve el guion al instante (`Conversacion::paraElGuion`); con
  más palabras («sí, y mi teléfono es…», «no, el correo es con jota») van al modelo, que
  corrige ese campo o llama `registro_confirmar`. Un «no» suelto a varios datos: «¿Qué dato
  está mal?» (sin modelo, se preguntan de nuevo).
- **Preguntas sobre el evento.** El tótem tiene una **ficha del evento** (`ficha`, texto libre:
  horarios, lugar, salas; en PanelTotems y por `totem_cambiar`). El modelo contesta con eso y
  el guion retoma la pregunta pendiente; a lo que no está en la ficha o se va de tema, dice
  amablemente que no puede ayudar con eso, y el guion retoma.
- **Qué dice cada uno.** Las preguntas y confirmaciones salen del guion (frases conocidas y
  cortas); el modelo solo dice lo que el guion no sabe decir (la respuesta a una pregunta, una
  repregunta), **en streaming y frase por frase**: la primera frase se sintetiza mientras el
  modelo escribe la segunda (`TrozadorFrases`: corta en . ? ! … ; : con espacio después, no en
  «gmail.com»), y la pantalla muestra el texto a medida que llega.
- **El modelo es un rol: «registro»** (pestaña Agentes), con su prompt corto y editable
  (`{evento}`, `{campos}` y `{ficha}` los pone la app). De fábrica el **modelo local**
  (`broadcast-reactivo` en el llama-server del programa, el mismo que lee el dorso: se carga
  cuando se conecta un tótem y se suelta cuando no queda ninguno): contesta rápido y lo que
  dice la gente no sale de la máquina. El modelo propio, OpenRouter o Gemini con clave, solo si
  el organizador los elige (la pestaña Agentes y PanelTotems avisan que la voz y los datos
  salen). Los de cuenta (Claude, OpenCode Go) no: solo andan por su agente de consola, que
  tarda minutos en arrancar. **No hay agente de consola**: un cliente de
  `/v1/chat/completions` en la app (`agent/ChatEnVivo`, con `stream: true` y herramientas, sobre
  `net/HttpsClient`), un pedido por turno con el estado del guion y los últimos cuatro
  intercambios. Por tótem se apaga con `conModelo: false`.
- **Varias conversaciones a la vez:** contra un mismo servidor van a lo sumo
  `registro.agenteParalelo` pedidos juntos (2); los demás esperan en una cola, y el tiempo de
  la cola cuenta.
- **El guion es el respaldo, sin perder nada.** Si el modelo no empieza a contestar en
  `registro.agentePrimeraS` (5 s), si el turno pasa `registro.agenteTurnoS` (15 s) o si falla,
  lo oído lo contesta el guion con sus normalizadores; con `registro.agenteFallas` (2) fallas
  seguidas, la conversación sigue con el guion solo. Lo anotado queda. La atención remota pausa
  también al modelo (lo que estuviera pensando no se dice). `totem_estado` dice con qué entiende
  (`agente`; por conexión `modo`: `agente` | `guion`, y `modoPorQue`).

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
- **No lo leyó en 15 s:** si hay **dorso** (abajo), le pide la parte de atrás; si no, le
  dice qué corregir (tenelo quieto, del lado del código, sin reflejo; «ese código no es de
  un documento que pueda leer» si vio otro código) y vuelve a buscar. A la segunda, si no
  es obligatorio, lo saltea («seguimos sin el documento»); si es, le pide que tipee el
  número y sigue buscando.
- **El dorso** (solo si hay un modelo con visión —el local con su complemento de visión
  o el propio—: `Conversacion::ofrecerDorso`, que `TotemsViewModel` pone al empezar con
  `LectorDocumentos::dorsoDisponible`): «No pude leer el código. Dalo vuelta y mostrame la
  parte de atrás, la de los renglones de letras y números de abajo». La conversación sigue
  en `Documento` con `buscandoDorso()`; la página pasa al estado `dorso` («Mostrá la parte
  de atrás: los renglones de abajo») y sigue mandando fotos enteras (`cuadro`). Cada foto se
  lee con zxing (por si vuelve a mostrar el frente) y, **de a una por conexión**, la franja
  de abajo va al modelo de visión (`LectorDocumentos::leerDorso`: primero el local, en
  127.0.0.1; si no, el propio), que transcribe la zona de lectura mecánica. Se acepta si
  cierran los verificadores del número, el nacimiento y el vencimiento y, además, el
  compuesto u otra foto que coincida (`ConfirmadorMrz` de la conexión; DOCUMENTOS.md, «El
  dorso»). Lo leído se confirma igual que el frente; con «no», «mostrame de nuevo la parte
  de atrás» (y las fotos de antes ya no cuentan). El modelo local se hace cargar cuando
  empieza el paso del documento y se suelta cuando nadie muestra uno. El tope del paso con
  el dorso es 25 s (no se corta con una foto en el modelo, hasta 45 s); al vencer, lo de
  siempre: saltear o tipear. En el registro, «documento leído por el dorso, con el modelo
  local (dni-ar …333)».
- **Respaldo:** tipear el número en la pantalla (`{numero, origen: "tipeado"}`, sin
  confirmar) o dictarlo («once, dos dos dos…», se confirma). «No tengo» lo saltea si no es
  obligatorio. Los silencios no cuentan mientras lo muestra.
- En el registro de la app, solo «documento leído (dni-ar …333)». Por MCP los documentos
  salen resumidos (ver DOCUMENTOS.md, «Los datos son sensibles»).
- En la interfaz: en el editor del campo, qué tipos acepta (los de fábrica y los de los
  plugins que corren; ninguno marcado = cualquiera). Los registrados muestran el documento
  como «Juana Andrea Prueba · DNI 11.222.333».

## Atención remota

Una persona del equipo, desde cualquier lado (un celular o una compu), **ve todas las
conversaciones** de los tótems del evento, **mira y escucha** una en vivo, **interviene
cuando quiere** y **completa o corrige los datos**. Todo por web: la página
`/registro-atiende/` del gateway.

**Un link por evento** (`<origen>/registro-atiende/#t=<token>`), como el de un tótem:
token de 256 bits en el fragmento, guardado con su sal en el almacén de secretos
(`registro-<sha256(carpeta|@atiende)>`), hash con sal en el gateway, intentos fallidos
por IP (los mismos que los tótems), Origin igual a Host. Se puede volver a mostrar y
**renovar** (el viejo deja de andar y se corta a quien lo tenía abierto). Se eligió uno
compartido y no uno por persona porque es lo más simple de repartir en la puerta de un
evento; cada una pone **su nombre** al entrar (queda en su navegador), y es lo que ve la
persona del tótem: «Te atiende Laura». Sin evento o sin tótems no hay credencial y no
entra nadie. En la app: Personas → Registro, «Atención remota» (copiar, QR, renovar,
quiénes la tienen abierta); por MCP `totem_atencion_enlace`.

**Mirar y escuchar.** Al elegir una conversación, la página abre UN WebRTC con el gateway
(una sesión del pool, ruta `RegistroAtiende(k)`): baja la cámara del tótem (los mismos
cuadros que suben a `BA.Registro.<k>`, achicados y a 15 por segundo) y lo que se oye ahí,
la voz de la persona MÁS la del asistente. Cambiar de conversación cambia la ruta, sin
renegociar. Sube su micrófono, que el gateway usa SOLO mientras la atiende.

**Atender.** «Atender yo» pide el micrófono y la app decide (una persona por
conversación; si otra ya la atiende, se lo dice). Atendida: el guion se pausa
(`Conversacion::atender`: no pregunta, no cuenta silencios, lo oído y lo tipeado no lo
hacen hablar), el asistente se calla en el acto (lo que faltaba decir se tira y lo que la
síntesis entregue tarde no sale) y la pantalla del tótem dice en grande «Te atiende
Laura». La voz de quien atiende baja **solo a ese tótem**, mezclada con la del asistente
(`gateway/ColaVoz.hpp`: un track tiene una sola fuente; el anillo del asistente, que la
app llena cada 10 ms, marca el reloj). La persona del tótem le contesta y ella la oye. Lo
que se le entiende igual se muestra (a quien atiende también). Si quien atiende se va
(cierra la página, se corta), la conversación vuelve al asistente.

**Completar o corregir** en cualquier momento, atendiendo o no: cada campo es editable;
lo que se pone va a la conversación como un dato tipeado (sin confirmación hablada) y se
ve en el tótem. Vacío borra el dato. Sin atender, si era el que se pedía, el asistente
sigue con el próximo.

**Terminar:** «Guardar registro» (como si hubiera terminado el guion, sin el paso de la
foto: padrón y, si la cara no se conocía, la libreta con la mejor foto que se vio; no
anda si falta un obligatorio), «Devolver al asistente» (sigue desde el próximo campo sin
dato) o «Cancelar».

Lo mismo desde la app (Atender, Datos…, Devolver, Guardar, Cancelar en cada conversación;
sin voz) y por MCP (`totem_atender {conn, accion: atender | guardar | devolver |
cancelar, quien?}`, `totem_dato {conn, clave, valor}`).

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
{clave, etiqueta, tipo, ejemplo}`, `listo {texto}`, `reiniciar`, `atiende {activo,
nombre}` (la atiende una persona: «Te atiende …»). El gateway le
manda además `audio-error` y `cortado`. La página manda `soltar` al recibir
`listo` (en el celular; con `?kiosco=1` se queda con el canal).

### La atención remota

Página (`/registro-atiende/`, `Web/registro-atiende`): `GET /ws?registro_atiende=1`,
primer mensaje `{type:"auth", t, nombre}` → `{type:"hola", aconn, nombre}`. Después
`{type:"mirar", conn}` → `{type:"mirando", conn, canal, habla}` (también cuando la
conversación toma o suelta su canal); `{type:"oferta", sdp}` (un audio y a lo sumo un
video) → `respuesta` / `ice`; `{type:"atender" | "devolver" | "guardar" | "cancelar",
conn}` y `{type:"dato", conn, clave, valor}` van a la app.

API de control:

- `POST /api/v1/registro/atencion {sal, hash_token}` — la credencial del link (`{}` =
  sin atención remota). La app la publica junto con los tótems.
- `POST /api/v1/registro/atendida {conn, aconn}` — quién atiende esa conversación (`""`
  = el asistente). Solo entonces la voz de esa página baja al tótem; quien la atiende
  pasa a mirarla.
- `POST /api/v1/registro/enviar {aconn, msg}` o `{atiende: "*", msg}` — a las páginas de
  atención. En `GET /api/v1/registro/estado`, `atienden: [{aconn, nombre, ip, lugar,
  desdeMs, mira, canal, habla, medios}]`.

SSE `registro`: `{tipo: "atiende-conexion", aconn, nombre, conectado, ip, lugar}`,
`{tipo: "atiende", aconn, nombre, accion, conn, clave?, valor?}`, `{tipo: "intento",
atiende: true, ip}`.

App → páginas de atención: `conversaciones {evento, lista: [{conn, id, totem, canal,
audio, estado, paso, quien, oido, dice, campos, atendida, atiende, atiendeConn, enCurso,
pedido, editables: [{clave, etiqueta, valor, listo, tipo, obligatorio, opciones}]}]}`
(la misma fila que la pestaña Registro y `totem_estado`, juntada cada 150 ms) y `aviso
{texto, conn}` (lo que no se pudo: «Ya la atiende Laura», «Eso no parece un correo»).

## Dónde está

- El modelo que entiende: `src/app/registro/AgenteRegistro.{hpp,cpp}` (lo puro: frases,
  streaming, herramientas, turnos), `src/app/registro/AgenteTotem.{hpp,cpp}` (los tiempos y el
  respaldo), `src/app/agent/ChatEnVivo.{hpp,cpp}` y `ChatEnVivoFuentes.cpp` (el cliente y qué
  servidor usa cada proveedor), el rol `registro` en `src/app/agent/Roles.cpp`, la región «El
  modelo que entiende» de `TotemsViewModel.cpp` y la de «Con un modelo que entiende» de
  `Conversacion`. Tests: `test_registro_agente` (sin red) y `test_registro_agente_totem`
  (contra un servidor falso: varios datos de una, frase por frase, repregunta, el corte por
  tiempo que cae al guion, dos conversaciones con un solo lugar).
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
- La atención remota: `src/gateway/RegistroAtiende.cpp` (el borde), `src/gateway/ColaVoz.hpp`
  (la mezcla), la ruta `RegistroAtiende` en `MediaRoute.hpp`/`MediaPump.cpp`,
  `SignalingServer::ofertaRegistroAtiende`, `assets/web/registro-atiende/`; en la app, la
  región «La atención remota» de `TotemsViewModel.cpp` y `Conversacion::atender/devolver/
  poner/guardarYa`. Tests: `test_registro_atiende` (gateway), `test_registro_atencion` (app).

## Falta probar a mano

- El modelo que entiende con el modelo local de verdad: cuánto tarda la primera palabra en frío
  y en caliente (si 5 s alcanzan), si anota bien varios datos de una y las correcciones, si
  contesta con la ficha y se niega a lo demás, si no habla de más cuando solo anotó. Con
  varios tótems a la vez (la cola, el tiempo que espera el segundo). Con el modelo propio y con
  OpenRouter.

- El paso del documento con un DNI de verdad: distancia, luz, reflejo; la confirmación
  hablada; tipear el número en el celular.

- Con una TV o tablet con cámara y un celular: reconocer, saludar por el nombre,
  dictar un correo y un teléfono, corregir con «no», tipear, cancelar.
- Que la voz del asistente no se transcriba (el margen de 350 ms al terminar).
- Ocho conversaciones a la vez y la novena esperando.
- La atención remota con un celular de verdad: mirar y oír un tótem (que no se oiga a sí
  misma), atender (que la persona del tótem la oiga y no haya eco), completar un dato,
  guardar, devolver; dos personas atendiendo a la vez conversaciones distintas; entrar por
  el túnel (TURN).
- Chromecast con `?pantalla=1` sobre el HTTPS del aparato (certificado propio: la
  TV puede no aceptarlo; con el túnel anda). Si no, «Un código QR» con el QR
  `registro:<id>` (va por HTTP) muestra lo mismo.
