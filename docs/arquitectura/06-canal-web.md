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
| `comun/prompter-vista.js` | `BAPrompterVista`: la vista del teleprompter (la tablet y el panel del invitado) |

Las páginas del evento (`assets/web/evento/vivo.js`, `marco.js`) no los usan: las cargan
las páginas del sitio de cada evento, que no traen `/comun/`.
