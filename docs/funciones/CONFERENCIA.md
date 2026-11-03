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
| `Backstage(k)` | cámara → `BA.Backstage.k` (preview chico a ~10 fps, solo el operador) si está entre los primeros `backstagePreviews` (6) de la fila; si no, nada | programa `BA.Program` + retorno de audio |

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

### Entrada escalonada (`room.entradaPorSegundo`)

Pasar a la sala es un solo camino para todos —admitir de a uno, **Admitir a
todos**, el lobby apagado, la entrada directa y la prueba de sala— y va a un
ritmo: **2 por segundo** de fábrica (de 1 a 100, en Configuración → Conferencia
«Invitados que entran por segundo», o `settings_set` con
`{"room.entradaPorSegundo": 5}`; rige en el acto, sin reiniciar la conferencia).
Cada uno que entra es una conexión WebRTC que se negocia, una cámara más que
decodificar y una grilla que se reacomoda: de a 120 juntos la conferencia y la
app se ahogaban y el mosaico se veía trabado.

- El primero que llega a una sala tranquila pasa en el acto; si llegan muchos a
  la vez, pasan en orden de llegada (120 de golpe a 2 por segundo: un minuto).
- Los aprobados que esperan su turno **no están rechazados**: siguen en el lobby
  con «Entrando…» y su lugar en la fila («Hay N personas antes que vos para
  entrar»). Su lugar en el programa queda reservado: no se aprueban más que los
  lugares libres.
- El operador los ve en el snapshot de la sala (`room_status`): `entrando`
  (cuántos esperan), `entradaPorSegundo` (el ritmo) y `entrando: true` en cada
  uno; la consola web lo muestra junto a los lugares libres («N entrando»).

## Modo oradores

Para cuando hay mucha gente que va a hablar por turnos (página Sala →
**Oradores**, o la consola web).

- Con el modo activo, **admitir** a alguien lo pone en la **fila de oradores**
  (no necesita slot): mira el programa en vivo (ruta `Backstage`) y su
  navegador le muestra su lugar y cuánto falta.
- Cada orador tiene un **tiempo**: el general (se aplica al entrar) o uno propio
  (✎ en la tarjeta). La fila se **reordena** con ◀ ▶.
- **Al aire**: *uno a la vez* (el que estaba sale) o *varios*; entra en el
  **slot por defecto** (o el primero libre). Sacarlo del aire lo devuelve a la
  fila como "habló", nunca lo echa.
- El **reloj nunca saca a nadie**: llega a cero y sigue en negativo (`+m:ss`).
  El invitado lo ve en grande; los últimos 10 s el número crece y late, y
  pasado el tiempo se pone rojo. El operador lo ve en la tarjeta, en un reloj
  flotante sobre cualquier página de la app y en la tarjeta "Oradores" del
  dashboard.
- **Carrusel**: *manual*; *al sacar, entra el siguiente*; o *automático por
  tiempo* (pasado cero + un margen configurable, rota solo).
- Cada tarjeta tiene cara: video en vivo (al aire, el del programa; en la
  fila, el preview de los primeros de la fila: `BA.Backstage.k`, 320x180 a
  ~10 fps, que se reasigna al reordenar o al salir al aire) o, sin video, la
  foto chica que manda el navegador cada 5 s, nombre, **información** y **contacto** (los carga el invitado
  al entrar, "solo lo ve la producción", para ubicarlo si se corta; el
  operador los puede editar).

### Tablero para una tablet (solo lectura)

**Tablero ▾** da el enlace y el QR (red local o dominio público). Muestra
quién está al aire con su reloj grande, la fila con caras en vivo, datos,
contacto, tiempo de cada uno y cuándo entra cada uno, y quiénes ya hablaron
(y cuánto se pasaron). La clave viaja en el fragmento del enlace y cambia en
cada arranque de B.

## Video en vivo en las vistas web (relay)

La consola web y el tablero muestran el video de los invitados. B lo reenvía
como un **SFU**: toma los paquetes codificados tal como llegan del invitado
(antes de decodificarlos para A) y los copia a la PeerConnection de cada vista.

- El invitado **sube una sola vez**, sin importar cuántas vistas lo miren.
- B **no re-codifica**: copia bytes (una copia por cuadro, compartida por
  todas las vistas); el costo por vista es solo de red.
- Una vista nueva arranca en un keyframe (se le pide uno al invitado); los
  NACK los atiende B desde su historial; si una vista se congela, pide otro
  keyframe.
- Señalización en un solo pedido HTTP (offer y answer con candidatos). Los
  espectadores caídos se cierran solos; tope de `kMaxViewers` (48).

## Grilla de personas

Una fuente de escena (**Escenas → Agregar → Grilla de personas**, tipo `ba_grilla`)
muestra a la gente conectada —los admitidos de la Conferencia y los integrantes
habilitados de la Asamblea— en un mosaico que crece y se acomoda solo. Quién va en
cada grilla y dónde lo decide la app (`src/app/escenas/GrillaPersonas.hpp`); el
mosaico lo pinta B (`src/gateway/Grillas.hpp`) en el canal `BA.Grilla.<k>`, y su
registro dice cada 30 s «grillas: BA.Grilla.1 1920x1080, N personas, X ms por cuadro».
Los ajustes están en las Propiedades de la fuente y en `grilla_personas_ajustar`.

**Las entradas se juntan** (`grilla.reacomodoMinMs`, de fábrica 1500 ms, en
Configuración → Conferencia). Cuando entra alguien la grilla se reacomoda: las
celdas cambian de tamaño y B anima el viaje de cada cara. Antes cada entrada era un
reacomodo entero (y B rearrancaba el viaje de todas desde quietas y le pedía otra
calidad a cada cámara), así que con mucha gente entrando el mosaico no terminaba
nunca de moverse. Ahora una grilla reacomoda por entradas como mucho una vez en ese
lapso: el primero que llega después de un rato entra en el acto, los que llegan en
el medio esperan y entran **todos juntos** en el próximo reacomodo, con una sola
animación. El orden sigue estable (nadie de los que ya están se mueve más que por el
reacomodo) y el orador y los fijados a mano no esperan (el fundido al dar la palabra
es inmediato). Los que se van salen en el acto. `grilla_personas_estado` dice en cada
grilla cuántos esperan (`entrando`). 0 = como antes, cada entrada reacomoda.

### Cuando alguien habla (`al_hablar`)

| Opción | Qué pasa |
|---|---|
| **Sin cambios** (`sin_cambios`) | La grilla no se toca: nadie se mueve ni crece. Al orador solo se le pide su cámara en alta (entra con el fundido). |
| **Resaltar en su lugar** (`resaltar`, de fábrica) | Nadie se mueve. El orador lleva el borde del color de resaltar y, según **Agrandar al que habla** (`agrandar`, aparte del modo; de fábrica `no`, así las grillas armadas se ven igual que antes): `encima` (su celda crece un 6 % sobre las vecinas, sin achicar a nadie) o `achicando` (todas al 94 % y la del orador llena su lugar). |
| **Destacar** (`destacar`) | El orador pasa a un lugar grande (`destacar_lugar`: centro o izquierda; `destacar_tamano`: % del lienzo) y los demás a una franja (`franja`: abajo o al costado). Los demás conservan **su orden** (se achican y se corren, no se mezclan); los que no entran, «+N». La animación dura `transicion_ms`. Al terminar, cada uno vuelve a su lugar. |
| **Superponer sin tapar** (`superponer`) | El recuadro del orador va en el medio y la grilla le deja el hueco: los demás se acomodan alrededor, nadie queda tapado (`superponer_tamano`). Con `superponer_completa`, el orador a pantalla completa. Si no entran alrededor del hueco, se arma como «destacar» con la franja abajo. |
| **Cortar a la escena del orador** (`escena`) | Esta grilla no cambia (salvo `agrandar`). El botón **Armar la escena del orador** (o `grilla_personas_escena_orador`) crea, si no está, la escena «Orador · <grilla>» con una grilla «<grilla> · orador» que muestra únicamente al orador (`muestra: orador`, `sigue`: esta), a todo el lienzo y con su nombre y cargo, y la anota en `escena_orador`. **Pasar a la escena del orador** (`pasar_a_escena`): `a_mano` (de fábrica: corta el operador o la línea de tiempo); `automatico` (al cambiar el orador la app pone su escena al aire en `salida_orador`, "" = la principal, y cuando ya no hay orador vuelve a la de antes, salvo que el operador haya cortado a otra; el tiempo mínimo y la histéresis son los del orador); `asistente` (se lo pide al asistente de automatización, que corta si está autorizado, y el operador **siempre** recibe el aviso: «¿Paso a la escena del orador: X?» y, cuando el corte se ve, «El asistente pasó a …»). La grilla completa sigue en otra salida (salidas múltiples). |
| **Nada** (`nada`) | Como antes de todo esto: sin orador (ni capa alta para nadie); solo el borde a quien habla (si «Borde a quien habla»). |

**Quién es el orador** (`disparo`): el que habla con **voz sostenida** (`voz_ms`, de
fábrica 1,5 s: una tos no alcanza), el que tiene **la palabra** en la Asamblea, el
**orador al aire** de la Conferencia (modo oradores) o **a mano** (el menú de cada cara
en la vista de la grilla: «Destacar como orador», o `grilla_personas_destacar`; lo
elegido a mano gana a cualquier disparo, en el acto). **Tiempo mínimo en pantalla**
(`minimo_ms`, 4 s): el orador no cambia antes (que no salte con cada tos); si se va, sí.
Con voz, cuando nadie habla se queda el último que habló. En un grupo de grillas, el
orador de una grilla queda en esa grilla (el reparto no se lo lleva a otra del grupo).

### La cámara del orador: alta definición con un fundido

Regla: **al darle la palabra se usa el video de alta definición que manda esa persona,
con un fundido, y nunca negro ni salto.**

1. Al orador (y al que está a punto de serlo, si **Precargar al siguiente**: el
   siguiente de la fila de la Asamblea o de la Conferencia; con voz, el que empezó a
   hablar y todavía no llegó a `voz_ms`) se le pide la **capa alta** de su cámara
   (`docs/arquitectura/06-canal-web.md` §7).
2. Mientras no llega el primer cuadro HD se sigue viendo la **baja**. En una conexión
   nueva (el integrante al que se le acaba de dar la palabra) la baja llega primero y
   sale mientras tanto.
3. Cuando llega la HD, **fundido** de la baja a la HD en `fundido_ms` (de fábrica
   300 ms), en la grilla y en el canal de la app (el lugar del programa o el remoto
   con la palabra). La baja sigue prendida ese tiempo (sus cuadros refrescan lo que se
   va) y **recién después se pausa**.
4. Para no cargar de más: solo el que pasa a grande sube a la alta; las miniaturas no
   cambian de capa por achicarse un poco (suben pasando 260 px de alto y bajan recién
   por debajo de 180), y no sube más de una miniatura por vuelta (cada subida es un
   cuadro clave). Las miniaturas van en la baja (180p a 15 cps); quien no está en
   ninguna grilla ni al aire, en ninguna. El que deja de ser orador (o precargado)
   **no** se queda en alta por esa histéresis: sigue en alta solo si su celda pasa los
   260 px (antes cada orador que pasaba quedaba en alta en su celda de ~200 px; con 40
   invitados terminaban los 40 en alta, 68 Mbps).
5. **Alta definición hasta N personas** (`alta_hasta`, de fábrica 10; 0 = sin límite):
   si la grilla muestra **más** de N personas, todas sus miniaturas van en baja,
   sean del tamaño que sean; solo el orador y el precargado siguen en alta. Con N o
   menos, la regla por tamaño del punto 4. Para que no salte con uno que entra y sale
   justo en el borde, vuelve a la regla por tamaño recién con N − 1 personas.
   `grilla_personas_estado` dice en cada grilla `alta_hasta` y `en_baja`, y en cada
   persona su `capa` (`alta` | `baja`).
6. **Alta definición como mucho N a la vez** (`alta_max`, de fábrica 4, de 1 a 64): el
   tope duro, sean 5, 10 o 40 invitados. De fábrica 4: un debate entre dos, el que queda
   en espera y el presentador. Si piden la alta más personas que eso, se elige por
   prioridad: el orador (o los oradores, si hay varias grillas con oradores distintos),
   después el **presentador** de la conferencia (que ahora pide la alta por sí mismo en
   cualquier celda), después el precargado (el siguiente de la fila), después los
   **fijados** a mano que la piden por su lugar y al final el resto por tamaño. Dentro de
   cada grupo, el que ya estaba en alta va antes que el que llega (nadie parpadea en el
   borde del tope) y después el más grande. El fundido no cambia: al dar la palabra se
   sigue viendo la baja hasta el primer cuadro HD y se funde. Con varias grillas manda el
   `alta_max` más chico (la capa es de la persona, no de la celda).
   `alta_hasta` y `alta_max` no se pisan: `alta_hasta` dice **cuándo** las miniaturas
   pueden pedir la alta por su tamaño; `alta_max`, **cuántos** como mucho, por la razón
   que sea. Los dos los aplica solo `CapasGrilla` (un solo dueño).

La capa de cada persona tiene **un solo dueño** para lo que se ve en las grillas: la app
(`CapasGrilla`, en `src/app/escenas/GrillaPersonas.hpp`) la manda en cada celda y B la
respeta sin volver a decidir por el tamaño. B solo le suma lo que se ve de esa persona
**fuera** de la grilla: su lugar del programa en la escena al aire o armada (el invitado
del cupo que no está en ninguna de esas escenas no pide la alta por estar en el cupo) y
las vistas web que la miran.

B anota en su registro cada pedido («capas: a <pid> se le pide la alta») y cada llegada
(«capas: llegó la HD de <pid> (N ms desde el pedido); fundido de M ms»).

### Nombres

**Nombre en cada celda** (`nombres`): sin nombre, el nombre, o el nombre y el cargo o
la banca (Conferencia: el campo «info» de la ficha; Asamblea: `cargo_oficial` y `banca`
del integrante). Estilo: alto del rótulo (`nombre_tamano`, % del alto de la celda),
abajo o arriba (`nombre_lugar`), color del texto y del fondo (`nombre_color`,
`nombre_fondo`). B no tiene letras: la app dibuja cada rótulo con la letra de la
interfaz (cualquier escritura) como una máscara y se lo manda una vez
(`POST /api/v1/grillas/rotulos`); B lo pinta escalado al alto de cada celda.

## API de control

Siempre en `127.0.0.1:8788` (nunca por el túnel). Credenciales: `Bearer` con el
token de `Runtime/gateway-admin.json (dentro del bundle)` (solo-usuario) o la cookie de la
consola web (escrituras con `X-BA-Admin: 1`, defensa CSRF).

| Método | Ruta | |
|---|---|---|
| GET | `/api/v1/room` | estado completo |
| GET | `/api/v1/events` | SSE: `room` (estado) y `chat` |
| POST | `/api/v1/room` | `{lobby?, locked?}` |
| GET / POST / DELETE | `/api/v1/prueba-sala` | la prueba de sala: credencial de prueba, entrada directa, cierre (ver [PRUEBA-SALA.md](PRUEBA-SALA.md)); `{medida}` para el probador de capacidad remoto ([CAPACIDAD.md](CAPACIDAD.md)) |
| POST / DELETE | `/api/v1/capacidad/red` | secreto para medir la conexión por el túnel ([CAPACIDAD.md](CAPACIDAD.md)) |
| POST | `/api/v1/pin` | `{pin?}` (vacío = generar) |
| POST | `/api/v1/participants/<id>/admit\|reject\|kick` | |
| POST | `/api/v1/participants/<id>/talk` | `{on}` hablar en privado (lobby) |
| POST | `/api/v1/participants/<id>/request` | `{what}` |
| POST | `/api/v1/chat` | `{text, to?}` |
| POST | `/api/v1/speakers` | `{enabled?, defaultSec?, carousel?: manual\|chain\|time, graceSec?, multi?, defaultSlot?}` |
| POST | `/api/v1/speakers/next` | al aire el siguiente de la fila |
| POST | `/api/v1/participants/<id>/onair` | `{on}` |
| POST | `/api/v1/participants/<id>/move\|time\|requeue\|profile` | `{index}` · `{sec}` · — · `{name?, info?, contact?}` |
| GET | `/api/v1/participants/<id>/avatar` | foto de la tarjeta (JPEG) |
| GET | `/api/v1/board-link?target=local\|remote` | enlace + QR del tablero |
| POST / DELETE | `/api/v1/view` · `/api/v1/view/<id>` (`/keyframe`) | video en vivo (relay) |
| GET / DELETE | `/api/v1/shares/<id>/content` · `/api/v1/shares/<id>` | |
| GET | `/api/v1/network` | HTTPS, túnel, IPs y enlaces |
| POST | `/api/v1/network/server-config` | `{endpoint, interface?}` → `wg0.conf` del servidor |
| POST | `/api/v1/login-code` · `/api/v1/login` | acceso de la consola web |
| GET / POST | `/api/v1/grillas` | la grilla de personas: el estado entero (`{fundido_ms, precarga, grillas: [{canal, …, rotulos, celdas: [{persona, x, y, w, h, resaltar, capa, rotulo}]}]}`, ver `src/gateway/Grillas.hpp`) |
| POST | `/api/v1/grillas/rotulos` | `{rotulos: [{id, w, h, a8}]}` las máscaras de los nombres |

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

- Certificado propio: dejar `cert.pem` + `key.pem` en `Credenciales/` (p. ej.
  Let's Encrypt del dominio público); al arrancar se importan cifrados (y se
  borran los originales) → sin avisos.
- Si no hay, uno **autofirmado** (ECDSA P-256) con las IPs de la LAN y la
  dirección pública, reutilizado entre arranques. El navegador avisa la
  primera vez; aceptado, todo funciona.

### Túnel WireGuard

Para que los invitados entren directo por internet: un servidor propio con IP
pública (un VPS Linux) y un túnel **WireGuard en espacio de usuario** (sin
drivers ni permisos de administrador) con una pila IP propia (lwIP):

```
invitado ─► IP publica:443/80  ─┐ (DNAT en el servidor)
invitado ─► IP publica:40000-41599/udp ─┤
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
   UDP 51820, TCP 80/443 y UDP 40000-41599 (el rango de WebRTC que diga Configuración →
   Red de la conferencia; la configuración generada redirige ese mismo rango).

La clave privada de este equipo vive cifrada en `Credenciales/wireguard-key.cred`
o en `BA_WG_PRIVATE_KEY`; la PSK opcional, en `BA_WG_PSK`.

**Cuánto anda el túnel**: la tarjeta **Sala → Enlace** mide subida, bajada, demora, jitter
y pérdida por el túnel contra un eco nuestro en el servidor (`ba-enlace-eco`, UDP 51821
por wg0; el instalador está en la misma tarjeta) y dice si entran los invitados del
evento: [ENLACE.md](ENLACE.md).

### Cuántos puertos UDP hacen falta

Cada conexión (un invitado, uno del lobby, una vista) abre un puerto del rango de
WebRTC (Configuración → Red de la conferencia, de fábrica 40000-41599) por **cada red
de la máquina**: una por interfaz y familia (IPv4, IPv6), más una con el túnel de
WireGuard. Las interfaces virtuales (el puente de las máquinas virtuales de la Mac,
Docker, Hyper-V, VPN como Tailscale) no le sirven a ningún invitado y no se usan
(`red.rtcVirtuales` las vuelve a sumar); las de enlace local tampoco. Con una red
cableada o wifi con IPv4 e IPv6 son dos por conexión: los 1600 puertos de fábrica
alcanzan para 800 conexiones, y con el túnel (tres por conexión) para 533: el tope de
invitados (512, `ipc::kMaxInvitados`) más el lobby. El rango puede tener hasta 2000
puertos (`kTopeRangoRtc`; con el túnel cada uno es un pcb de lwIP, `MEMP_NUM_UDP_PCB`).
Hasta noviembre de 2026 el de fábrica era 40000-40099 (unas 50 conexiones): una
configuración que lo tiene tal cual pasa sola al nuevo al leerse; uno elegido a mano
se respeta. Si hay un firewall o una redirección en el router para el rango viejo,
hay que ampliarla.

El gateway anota al arrancar cuántas entran («WebRTC: puertos UDP …: entran N
sesiones») y el panel **Uso de la conferencia** avisa en rojo si no alcanzan para la
sala llena (el cupo y el lobby) o si una conexión se quedó sin puerto. A ese invitado
se le dice («la conferencia se quedó sin puertos de red…») en vez de dejarlo
«conectando».

### Hasta cuántos invitados

El cupo (Configuración → Invitados, de fábrica 2) va de 1 a **512**
(`ipc::kMaxInvitados`, `src/core/ipc/LugaresSala.hpp`: un solo número para la app, el
gateway y el MCP; hasta noviembre de 2026 era 64 y con 80 pedidos entraban 68). Lo que
limita de ahí para abajo es la máquina, no una constante:

- **Canales**: cada lugar con canales (los del cupo, creados al subirlo) son ~25 MB de
  memoria compartida (cámara y pantalla a 1280×720 con tres cuadros, la vista chica y el
  audio). En macOS y Linux las páginas se ocupan cuando se escriben; en Windows cuentan
  contra la memoria virtual comprometida desde que se crean (512 lugares ≈ 12 GB). Un
  canal que no se pudo crear se anota en gateway.log («no se pudo publicar … (se salta)»).
- **Conexiones**: cada puerta del gateway acepta `room.maxConexiones` (de fábrica 1024,
  antes 256; un hilo por conexión) y cada dirección IP `room.maxConexionesPorIp` (32:
  con mucha gente detrás de un mismo NAT hay que subirlo).
- **TURN** (los de afuera por pinggy): `turn.maxAsignaciones` 640 y `turn.maxConexiones`
  1280 (antes 64 y 128).
- **Puertos UDP**: el rango de fábrica alcanza para el tope (arriba).
- **Lobby**: esperan a la vez hasta 512 (`maxWaiting`, antes 16: el 17.º recibía «lobby_full»);
  esperar es solo el WebSocket (las cámaras del lobby son las 4 de `lobbySlots`).
- **Interfaz**: «En la conferencia» y la lista de la sala son grillas y listas que solo
  dibujan lo que se ve; el menú «Agregar fuente» lista los primeros 16 lugares y el resto
  por número («Otro invitado…»).

`tools/carga/medir-sala.py --cupo` sube lo que haga falta para N (hasta 512) y lo
vuelve al final.

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
`test_webrtc_loopback` (ver `docs/desarrollo/tests.rst`). De punta a punta se verificó con
navegadores Chrome reales (cámara/micrófono sintéticos): lobby privado, admisión
sin cortes, HTTPS por la LAN, y el túnel WireGuard contra el gateway real.
