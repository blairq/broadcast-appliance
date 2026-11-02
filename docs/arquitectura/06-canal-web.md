# El canal web `/ws` (contrato v1)

Todas las páginas que hablan con la sala en vivo usan **un solo canal**: un WebSocket a
`/ws`, con **un solo formato** y **un solo cliente** (`assets/web/comun/canal.js`, `BAWs`).
Del lado de la sala (el gateway, proceso B) lo atiende `src/gateway/Canal.{hpp,cpp}`:
el apretón de manos, el primer mensaje, el lazo de lectura y el ping son los mismos para
todos los roles.

## 1. Formato

- Cada mensaje es un frame WebSocket de **texto** (opcode 0x1) con **un objeto JSON**
  que tiene `type`. Nada más: no hay frames binarios.
- Antes, los invitados, las pantallas y las superficies hablaban binario con 4 bytes de
  largo adelante (el envoltorio del canal interno de la app, `ControlProtocol`), y el
  resto texto. Se midió qué ganaba el binario: nada. El WebSocket ya delimita cada
  mensaje (el largo era redundante), la señalización WebRTC es texto (una oferta con
  cámara y micrófono midió 5,4 KB en Chrome), y lo único que podría ser binario (las
  fotos de los integrantes, 320×240 JPEG al 60 %, y los cuadros del tótem) va en base64:
  medido con la cámara de prueba de Chrome, 2,6 KB de JPEG son 3,5 KB en el mensaje; con
  una cámara real, unos KB más cada 1–3 s por aparato, nada al lado del video que va por
  WebRTC (cientos de KB por segundo).
- Una página vieja que todavía mande binario (abierta desde antes de actualizar) recibe
  `{"type":"recargar"}` y se cierra el canal: al recargarse trae la de ahora.

## 2. Entrar

La dirección lleva **solo el rol**, nada secreto (la dirección queda en los registros):

| Rol | Dirección | Primer mensaje (además de `v` y `type`) |
|---|---|---|
| invitado | `/ws` | `token`, `room`, `name`, `info`, `contact` — o, para volver, `resume`, `key` |
| pantalla (el programa en una TV) | `/ws?pantalla` | `token` — o `resume`, `key`; opcionales `canal`, `salida` |
| superficie | `/ws?superficie` | `tipo`, `id`, `k` (la clave del evento, solo las reservadas: rótulos, corrección, tablero), `p`, `vista`, `nombre` |
| sesión remota (integrante) | `/ws?sesion=1` | `i`, `t`, `pin` / `u`, `clave` / `aparato`, `registrar`, `nombre` |
| consola de la presidencia | `/ws?presidencia=1` | `c`, `t`, `pin` / `aparato`, `registrar`, `nombre` |
| registro por tótem | `/ws?registro=1` | `r`, `t` |
| atención del registro | `/ws?registro_atiende=1` | `t`, `nombre` |
| control web | `/ws?control` | `token`, `pin` (desde afuera) |
| botonera | `/ws?botonera=1` | `token` |

1. La página manda, como **primer mensaje**, `{"v":1,"type":"auth", …}`. La sala espera
   ese mensaje unos segundos (10–15); si no llega, cierra.
2. La sala contesta **siempre** con un `auth`:
   - `{"type":"auth","ok":true, …}` y lo que el rol necesite (el nombre, el `conn`, el
     perfil y las herramientas del control web, que los manda la app).
   - `{"type":"auth","ok":false,"error":"<texto para traducir>","error_codigo":"<código>"}`
     y cierra.
3. `v` es la versión del contrato. Sin `v` vale como 1 (las páginas de antes decían lo
   mismo en los bordes). Con otra, `error_codigo: "version"` y la página se recarga.

### Los códigos de error

| `error_codigo` | Qué pasó | Qué hace `BAWs` |
|---|---|---|
| `esperar` | la sala recién arrancó y no tiene todavía las credenciales de la app; o no hay lugar ahora | vuelve a probar solo, con espera creciente (2 s → 1 min) |
| `clave_mal` | la clave de la superficie no es la del evento abierto (se cambió de evento, o la sala todavía no tiene las claves) | ídem |
| `version` | la página quedó vieja | recarga la página |
| `token` | el token o el enlace no valen (vencido, ya usado, revocado) | se queda quieto (`alRechazo`) |
| `sesion` | el `resume`/`key` del invitado o de la pantalla ya no existe | se queda quieto: la página vuelve a entrar con el token o termina |
| `aparato` | el aparato registrado ya no vale (sesión, presidencia) | se queda quieto: la página pide el PIN |
| `intentos` | demasiados intentos fallidos desde esa IP | se queda quieto |
| `mensaje` | el primer mensaje no era un `auth` | se queda quieto |
| (sin código) | lo demás (PIN errado, usuario o contraseña) | se queda quieto |

Lo que se rechaza **después** de entrar no es un `auth`: `rejected` (la sala del invitado
cerrada o llena), `kicked`, `cortado` (la app lo sacó), y la página cierra el canal.

## 3. Mantenerse

- `{"type":"ping"}` → `{"type":"pong"}`, en todos los roles (lo contesta `canal::leer`).
  `BAWs` manda uno cada 15 s y, si el anterior no tuvo respuesta en 40 s, da el canal por
  caído y reconecta. Se mide desde el ping, no desde lo último que llegó: una pestaña
  escondida late una vez por minuto y no por eso está muerta.
- La sala corta una conexión que no dice nada en su latido (30 s en los bordes, 2 min en
  invitados, pantallas y superficies).
- Si se corta, `BAWs` vuelve solo con espera creciente (1 s → 30 s), y enseguida al volver
  la red o la pestaña al frente. El primer mensaje se vuelve a armar en cada intento
  (`auth()`), así lleva lo último (el `resume` del invitado, el aparato registrado).

## 4. Para todas las páginas

- `{"type":"tema"}`: cambió el tema del evento; `comun/tema.js` relee `/comun/tema.json`.
  Lo manda la sala a **todas** las conexiones abiertas de cualquier rol
  (`canal::aTodos`); solo las páginas sin canal (el público, el evento) lo releen cada 15 s.
- `{"type":"recargar"}`: la página se recarga (la superficie puede tener su `onRecargar`).
- Cada mensaje se avisa también como evento `ba-canal` en `document`.

## 5. El cliente

```js
const c = BAWs.abrir({
  rol: "sesion",                       // lo que va después de /ws?
  auth: () => ({ i, t, pin }),         // null = todavía no hay con qué entrar
  alEntrar: (m) => {},                 // auth ok
  alMensaje: (m) => {},                // lo demás (menos pong)
  alRechazo: (m) => {},                // auth ok:false que no se arregla esperando
  alEspera: (m) => {},                 // «esperar» / «clave_mal» (vuelve a probar solo)
  alCaer: () => {},                    // se cortó (vuelve solo)
  alEstado: (e) => {},                 // conectando | conectado | caido | cerrado
});
c.mandar(m); c.listo(); c.cerrar(); c.reabrir();
```

Las superficies usan `BACanal.abrir({tipo, que, onMostrar, onAviso, clave, …})`, que está
hecho sobre `BAWs` y además late cada 5 s con lo que muestra.

`comun/aparato.js` (el registro de aparatos con token de la sesión y la presidencia) sigue
igual: el token del aparato va en el primer mensaje (`aparato`).

## 6. Lo común de las páginas

| Archivo | Qué |
|---|---|
| `comun/canal.js` | `BAWs` y `BACanal` |
| `comun/reloj.js` | `BAReloj`: el desfase con el reloj de la sala; lee `t_srv_ms`, `ahoraMs`, `ahora_ms` o `serverNowMs` (no se renombran en el servidor) |
| `comun/dom.js` | `$`, `el`, `mmss` |
| `comun/vigia-video.js` | `BAVigia`: video congelado, con un solo umbral (pedir cuadro a los 2 y 6 s, rearmar a los 10 s) |
| `comun/capas.js` | `BACapas`: la cámara en dos calidades, la sala elige cuál viaja (§7) |
| `comun/prompter-vista.js` | `BAPrompterVista`: la vista del teleprompter (la tablet y el panel del invitado) |

Las páginas del evento (`assets/web/evento/vivo.js`, `marco.js`) no los usan: las cargan
las páginas del sitio de cada evento, que no traen `/comun/`.

## 7. La cámara en dos calidades (capas)

El invitado de la conferencia (`/ws`) y el integrante de la asamblea que manda medios
(`/ws?sesion=1`, `medios-oferta`) publican la cámara **dos veces** en la misma
conexión WebRTC: la capa **alta** (hasta 720p, ~1,5 Mbps) con la pista de la cámara y la
**baja** (~180p, 15 cps, ~150 kbps) con un **clon** de esa pista a 15 cps. La pantalla compartida sigue siendo otro video
aparte, sin capas. **La que pide es la sala**: según dónde se ve a esa persona le dice
al navegador qué capa prender; lo pausado no se codifica ni viaja. El cliente es
`comun/capas.js` (`BACapas`); el porqué, en `src/gateway/Capas.hpp`.

**Por qué no simulcast con `rid`:** el gateway es libwebrtc M123 como answerer y no
recibe simulcast: contesta sin `a=simulcast` y el navegador manda solo la primera capa
(lo verifica `test_webrtc_loopback`). Con dos transceptores cada capa es una recepción
normal.

**Por qué un clon para la baja:** con la misma pista en los dos senders, la fuente se
adapta al pedido más lento y la alta también sale a 15 cps. Medido en Chrome headless
(cámara falsa 720p30, 3 corridas por caso, 2026-11-01): con la misma pista, alta
1280x720 a 15 cps con la baja prendida y a 30 con la baja pausada; con el clon, alta a
30 y baja 320x180 a 15 con las dos prendidas (lo mismo del lado de quien recibe). Pasa
igual en libwebrtc nativo. El clon sigue a la pista: `enabled` y `stop()` de la cámara
lo apagan o lo sueltan también.

### Lo que manda el cliente

1. Arma los transceptores en este orden: audio; **alta** (la primera de video:
   `sendrecv` en el invitado, porque por ahí le baja el retorno; `sendonly` en la
   sesión); **baja** (`sendonly`). La alta con la pista de la cámara, la baja con su clon,
   y una sola encoding cada una (`sendEncodings: [{…}]`).
2. En la oferta dice qué `mid` es cada capa:
   - invitado: `{"type":"offer", "sdp": …, "capas": {"alta": "1", "baja": "2"}}`
   - integrante: `{"type":"medios-oferta", "sdp": …, "capas": {"alta": "1", "baja": "2"}}`

   Una renegociación (sumar la pantalla) repite `capas` con los mismos mids. Sin
   `capas` la sala trata la oferta como de antes: el primer video es la cámara y no le
   pide nada.
3. Arranca con **las dos prendidas** hasta que la sala diga otra cosa.

### Lo que manda la sala

- `{"type":"capas", "alta": true|false, "baja": true|false}`: qué capas tener prendidas.
  El cliente pone `encodings[0].active` en el sender de cada capa (`setParameters`, de a
  uno). Si llega antes de que la conexión esté negociada, lo vuelve a poner al conectar.
- Al cambiar de capa, la sala manda primero las dos en `true` y, cuando le llegó el
  primer cuadro de la nueva, la vieja en `false`: lo que se ve no se corta.
- **De la baja a la HD, con un fundido** (regla del usuario: «al darle la palabra, el
  video de alta definición: un fade in y listo»). Si se pide la alta y todavía no salía
  nada (una conexión nueva), la sala deja las dos prendidas y la **baja sale mientras
  tanto**. Cuando llega el primer cuadro de la alta, quien pinta (el canal de la app y la
  grilla de personas) **funde** de la baja a la HD en `fundido_ms` (lo manda la app con
  la grilla; de fábrica 300 ms) y la baja **sigue prendida ese tiempo**: recién al
  terminar llega `{"type":"capas","alta":true,"baja":false}`. Al bajar de la alta a la
  baja no hay fundido. Ver `src/gateway/Fundido.hpp` y `SelectorCapas`.
- `{"type":"medios", "activo": true, "previo": true}` (solo la sesión): sos el siguiente
  de la fila. Se abre la conexión y la cámara viaja (la sala pide la alta para tenerla
  lista) pero no va a ningún canal ni a la mezcla. Al darle la palabra llega
  `{"type":"medios", "activo": true, "previo": false}`: **no se renegocia**, la misma
  conexión pasa al aire.
- Los topes de siempre siguen y son **sobre la capa alta**: `{"type":"request","what":
  "calidad-720p" | "calidad-360p" | "calidad-max"}` (el operador, por la red) y
  `"calidad-grilla-180p" | "calidad-grilla-360p" | "calidad-grilla-libre"` (el lugar en
  una grilla). Vale el menor de los dos. La baja no se toca.

### Cuál pide la sala (`capaNecesaria`)

| Dónde está | Capa |
|---|---|
| Invitado en el programa, su entrada al aire o en la escena armada de alguna salida (lo dice la app por `POST /api/v1/capas`) | alta |
| Invitado en el programa, no al aire (vista chica, multiview) | baja |
| Invitado en el programa con una app que todavía no dijo nada | alta |
| Integrante con la palabra (canal 1 o 2) o el siguiente de la fila | alta |
| Lobby, fila de oradores con vista previa | baja |
| Fila sin vista previa, pantallas, atención hablada | nada |
| En una grilla de personas | la que pide la grilla (`capa` de su celda: la app la decide con histéresis, el orador en alta); con una app que no la dice, baja (cuadrito ≤ 200 px) o alta (más grande) |
| Precargado por una grilla (el siguiente orador) | alta |
| Lo mira una vista web (tablero, presidencia: se le reenvía la alta) | alta |

Un **probador de carga** que quiera comportarse como un cliente real tiene que: mandar
las dos capas con `capas` en la oferta, obedecer cada `{"type":"capas"}` (prender o
pausar sus encodings) y, en la sesión, no renegociar con `previo:false`. La sala anota
cada pedido en su registro («capas: a <pid> se le pide la alta»), cada llegada de la HD
(«capas: llegó la HD de <pid> (N ms desde el pedido); fundido de M ms») y las
estadísticas de la sesión traen `capa` (la que sale) y `capa_pedida`.
