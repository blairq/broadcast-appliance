# Sala de videoconferencia (Proceso B)

Guía del módulo de conferencia: lobby de admisión, invitados, archivos y
pantallas compartidas, y cómo entran los invitados (Cloudflare, red local o un
servidor propio por WireGuard).

## Arquitectura

```
 navegador del invitado ──WS /ws──┐                  ┌── consola web (Web/admin)
   (Web/guest)          ─HTTP────┤                  │
                                 ▼                  ▼   API de control (REST + SSE)
                     ┌──────────── B: ba-gateway ─────────────┐  127.0.0.1:8788
                     │ SignalingServer ── Room (modelo) ── AdminApi │ ◄── app Qt (ConferenceClient)
                     │ WebrtcHub (libwebrtc) ── MediaPump (rutas)   │
                     │ TlsTerminator (HTTPS)  WgDevice + IpStack (lwIP)
                     └──────────────┬─────────────────────────┘
                                    │ memoria compartida
                     A: ba-app (motor, escenas, OperatorFeed, vista Qt)
```

- **`Room`** es el modelo: quién espera, quién está adentro y en qué slot, el
  chat, los archivos y la **ruta de medios** de cada participante. No sabe de
  sockets ni de WebRTC; se prueba sola (`test_room`).
- **La API de control es la única forma de operar la sala.** La consola web y
  la vista Qt son dos clientes de la misma API; ninguna tiene lógica de sala.
- **`MediaPump`** mueve los medios según la ruta de cada sesión:

| Ruta | Sube (participante → A) | Baja (A → participante) |
|---|---|---|
| `Lobby(k)` | cámara → `BA.Lobby.k` (solo el operador) · voz → `BA.Audio.Lobby.k` solo si "hablar" | cámara del operador `BA.Operator` (o placa) · voz del operador solo si "hablar" |
| `Program(n)` | cámara → `BA.Guest.n` · pantalla → `BA.GuestShare.n` · voz → `BA.Audio.Guest.n` | programa `BA.Program` + retorno de audio |

  Admitir a alguien **cambia su ruta, no su conexión**: la misma
  PeerConnection pasa de "ve al operador" a "ve el programa", sin cortes.

## Lobby privado

- Quien espera **solo ve al operador**: nunca a los demás del lobby ni el
  programa (en el programa aparecen los de adentro).
- El operador ve la cámara de cada uno (hasta `lobbySlots`, 4 por defecto).
- **Hablar** con alguien del lobby es privado: solo esa persona escucha al
  operador, y el operador la escucha por sus auriculares (monitoreo, nunca la
  mezcla del programa).
- Con qué aparece el operador ante el lobby se elige en la página Sala
  ("Te ven con / Te oyen con"): fuentes existentes del motor. A lo publica
  desde una vista propia de libobs (640x360, escalada en GPU), sin tocar el
  programa.

## Invitados

- Entrada: nombre + sala + PIN → lobby → admitido. Un enlace de invitación
  puede traer los datos en el fragmento (`#room=..&pin=..&name=..&go=1`); el
  fragmento no viaja al servidor.
- Reconexión: la sala le da a cada participante un id y una **clave**; una
  recarga de la página o un corte de red lo reengancha a su lugar (30 s).
- Compartir **pantalla o ventana** (`getDisplayMedia`): segundo track de video,
  renegociado; llega a A como `BA.GuestShare.n`, con una fuente
  "Invitado n — pantalla" (oculta) lista en la escena.
- Compartir **archivos** (imagen, video, audio): subida en streaming, tipo por
  contenido (no por nombre), tope por archivo. "Tomar" los copia a
  `<datos>/Media` y los agrega a la escena actual.
- Chat (a todos, al operador, o del operador a uno), mano levantada, pedidos
  del operador (silenciar, apagar cámara, cortar pantalla, bajar la mano).

## API de control

Siempre en `127.0.0.1:8788` (nunca por el túnel). Credenciales: `Bearer` con el
token de `<runtime>/gateway-admin.json` (solo-usuario) o la cookie de la
consola web (escrituras con `X-BA-Admin: 1`, defensa CSRF).

| Método | Ruta | |
|---|---|---|
| GET | `/api/v1/room` | estado completo |
| GET | `/api/v1/events` | SSE: `room` (estado) y `chat` |
| POST | `/api/v1/room` | `{lobby?, locked?}` |
| POST | `/api/v1/pin` | `{pin?}` (vacío = generar) |
| POST | `/api/v1/participants/<id>/admit\|reject\|kick` | |
| POST | `/api/v1/participants/<id>/talk` | `{on}` hablar en privado (lobby) |
| POST | `/api/v1/participants/<id>/request` | `{what}` |
| POST | `/api/v1/chat` | `{text, to?}` |
| GET / DELETE | `/api/v1/shares/<id>/content` · `/api/v1/shares/<id>` | |
| GET | `/api/v1/network` | HTTPS, túnel, IPs y enlaces |
| POST | `/api/v1/network/server-config` | `{endpoint, interface?}` → `wg0.conf` del servidor |
| POST | `/api/v1/login-code` · `/api/v1/login` | acceso de la consola web |

## WebRTC real

`libwebrtc` M123 (Shiguredo) compilado contra la libc++ de Chromium
(`std::__Cr`). El gateway se compila **aparte**, con clang y esos headers
(`cmake/gateway-webrtc`), y enlaza todo estático: el binario depende solo de
`libc` y `libm`. Sin audio del sistema (un ADM propio "tira" del audio
remoto) ni X11.

```
make deps-webrtc BA_WEBRTC_TARGET=linux   # SDK + headers de libc++ (hashes fijos)
make build                                 # compila tambien el gateway real
```

## Acceso directo (sin Cloudflare)

### HTTPS

Los navegadores solo dan cámara y micrófono en páginas seguras. B atiende
HTTPS en `httpsPort` (8789) y reenvía al servidor de la sala con la IP real del
invitado (PROXY v1).

- Certificado propio: `Config/tls/cert.pem` + `key.pem` (p. ej. Let's Encrypt
  del dominio público) → sin avisos.
- Si no hay, uno **autofirmado** (ECDSA P-256) con las IPs de la LAN y la
  dirección pública, reutilizado entre arranques. El navegador avisa la
  primera vez; aceptado, todo funciona.

### Túnel WireGuard

Para que los invitados entren directo por internet: un servidor propio con IP
pública (un VPS Linux) y un túnel **WireGuard en espacio de usuario** (sin
drivers ni permisos de administrador) con una pila IP propia (lwIP):

```
invitado ─► IP publica:443/80  ─┐ (DNAT en el servidor)
invitado ─► IP publica:40000-40099/udp ─┤
                                        ▼
                         WireGuard (UDP) ─► B: WgDevice + lwIP (10.99.0.2)
                                             TCP 80/443 → servidor / HTTPS (IP real por PROXY)
                                             UDP 40000+ → WebRTC (candidatos con la IP publica)
```

Puesta en marcha (página Sala → **Configurar servidor WireGuard…**):

1. Poner `host:puerto` del servidor (y su IP o dominio público si es otro).
2. **Generar configuración**: se crean las claves del servidor y su
   `/etc/wireguard/wg0.conf` con las redirecciones; este equipo queda
   configurado y el gateway se reinicia.
3. En el servidor: `apt install wireguard iptables`, guardar el archivo y
   `systemctl enable --now wg-quick@wg0`. Abrir en el firewall del proveedor:
   UDP 51820, TCP 80/443 y UDP 40000-40099.

La clave privada de este equipo vive en `Config/wireguard.key` (solo-usuario)
o en `BA_WG_PRIVATE_KEY`; la PSK opcional, en `BA_WG_PSK`.

## Seguridad

- API de control solo en loopback; token por arranque en un archivo 0600.
- Tokens de invitado de un solo uso (anti-replay) + rate-limit de PIN por IP.
- Clave de participante aleatoria (reconexión y subidas); no sale en la API.
- Subidas: tipo por contenido, nombre en disco elegido por el servidor,
  temporal + rename, tope de tamaño y cantidad.
- La cabecera PROXY solo se acepta desde 127.0.0.1 (puente del túnel y HTTPS).
- WireGuard: Noise IKpsk2, anti-replay de 2048, rechazo de claves de orden
  bajo, cookies; validado contra implementaciones de terceros.

## Pruebas

`test_room`, `test_gateway_http`, `test_wireguard`, `test_ipstack` y
`test_webrtc_loopback` (ver `docs/tests.rst`). De punta a punta se verificó con
navegadores Chrome reales (cámara/micrófono sintéticos): lobby privado, admisión
sin cortes, HTTPS por la LAN, y el túnel WireGuard contra el gateway real.
