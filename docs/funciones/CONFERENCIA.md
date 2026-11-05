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

### El gateway en varios núcleos (2026-11)

Para pasar de decenas a cientos de invitados, B dejó de tener hilos por invitado (con 40
llegaba a 647 hilos y 45 mil cambios de contexto por segundo en Windows) y trabaja por capas
de hilos compartidos. El plan, las fronteras entre hilos y lo medido en cada paso están en
[PLAN-GATEWAY-FRAGMENTOS](../planes/PLAN-GATEWAY-FRAGMENTOS.md); los ajustes, en
Configuración → Conferencia:

- **Capa de red y capa de trabajo** (`gateway.hilosRed`, `gateway.hilosTrabajo`; 0 =
  automático: un hilo de red cada 3 núcleos físicos, de 2 a 8). Cada conexión vive en un par
  red/trabajo; el paso de la red al trabajo va **en tandas** (`gateway.redYTrabajo` `lote`,
  espera máxima `gateway.loteTrabajoUs` 500 µs) en vez de despertar al hilo por paquete.
- **El TURN y la red de WebRTC en los mismos hilos**, con la entrega **en memoria**
  (`gateway.turnEnMemoria`): lo que llega por pinggy al TURN pasa a WebRTC sin volver al
  sistema operativo ([TURN.md](TURN.md)).
- **Las colas de tareas de libwebrtc** (codificar el audio, decodificar el video…) comparten
  dos grupos chicos de hilos (`gateway.hilosColas`) en vez de una por invitado, y el video
  entrante va sin el suavizado previo al render (la grilla tiene su propio reloj).
- **El audio que sale** (el reloj de 10 ms que saca el audio de todos) se reparte en varios
  hilos (`gateway.hilosPlayout`) y no mezcla ni pasa por el procesamiento de WebRTC.
- **H.264 por la placa**: Windows decodifica con la API de video de **Direct3D 11** y
  codifica el retorno con el codificador de la placa (AMD en la PC de pruebas); macOS, con
  **VideoToolbox**. `gateway.decodificarConPlaca` apagado cae a software. La celda de la
  grilla se achica en la placa, en el hilo que decodificó.
- **Afinidad** (`gateway.afinidad`): apagada de fábrica; con mucha carga hacen falta todos los núcleos y el sistema reparte solo.

Con todo eso, en la PC de pruebas con 40 invitados el gateway bajó de 220 % a 169 % de un
núcleo, de 647 a 234 hilos y de 45 mil a 23,7 mil cambios de contexto por segundo, y el techo
pasó de ~80 a ~160 invitados ([docs/hardware](../hardware/README.md)).

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
es inmediato; con ellos entran los que llegaron antes, para no colarse). Los que se van salen en el acto. `grilla_personas_estado` dice en cada
grilla cuántos esperan (`entrando`). 0 = como antes, cada entrada reacomoda.

### La forma de las celdas (`proporcion`, `recorte_max`, `celda_min_ancho`, `celda_min_alto`)

La grilla elige cuántas columnas usar según la gente que muestra, y la **proporción
del video (16:9) siempre cuenta**:

- Con una proporción fija (`16:9`, `4:3`, …) cada celda tiene esa forma exacta y, si
  el lugar no la tiene, quedan bandas. Entre las disposiciones posibles, la de celdas
  más grandes.
- Con **Llenar el lugar** (`llenar`) la celda puede alejarse del 16:9 recortando el
  video, pero solo hasta **`recorte_max`** por lado (de fábrica 25 %: celdas entre
  4:3 y 21:9; de 0 a 40). Más allá de eso, bandas. Entre las disposiciones que
  cumplen se elige la de **más video a la vista**: recortar un poco suma (la cara se
  ve más grande), pero menos que agrandar sin recortar. Así la grilla no salta de una
  forma a otra al ir sumando gente de a uno (las columnas solo crecen).
- **Celda mínima** (`celda_min_ancho` × `celda_min_alto`, de fábrica 96×54 en un lienzo
  de 1080 de lado corto; en 1280×720 o 720×1280, 64×36). Ninguna celda baja de ese
  tamaño: si no entran todos así, los que sobran van al cuadro **«+N»** (igual que con
  el tope de celdas), nunca se achica más.

Antes (C18, visto en la PC de pruebas el 2026-11-03), con `llenar` la grilla elegía las
columnas que dejaban más área sin mirar la forma: con 22 a 25 personas salía 11×2 y con
43, 11×4, de franjas verticales de ~110×170 px con la cara cortada al medio. Ahora, en
1280×720: 22-25 → 5×5, 43 → 7×7, 64 → 8×8, todas apaisadas.

`grilla_personas_estado` dice en cada grilla `disposicion`: `columnas`, `filas`,
`celda` (`ancho`, `alto` en px del lienzo), `proporcion` (la efectiva de la celda),
`proporcion_video`, `proporcion_min` y `proporcion_max`, `minimo` (ya escalado al
lienzo), `caben` (cuántas celdas entran como mucho, con la del «+N») y `lienzo`.

### Cuando alguien habla (`al_hablar`)

| Opción | Qué pasa |
|---|---|
| **Sin cambios** (`sin_cambios`) | La grilla no se toca: nadie se mueve ni crece. Al orador solo se le pide su cámara en alta (entra con el fundido). |
| **Resaltar en su lugar** (`resaltar`, de fábrica) | Nadie se mueve. El orador lleva el borde del color de resaltar y, según **Agrandar al que habla** (`agrandar`, aparte del modo; de fábrica `no`, así las grillas armadas se ven igual que antes): `encima` (su celda crece un 6 % sobre las vecinas, sin achicar a nadie) o `achicando` (su celda crece y, a la vez, las demás se achican al 94 %; sin orador, todas enteras). |
| **Destacar** (`destacar`) | El orador pasa a un lugar grande (`destacar_lugar`: centro o izquierda; `destacar_tamano`: % del lienzo) y los demás a una franja (`franja`: abajo o al costado). Los demás conservan **su orden** (se achican y se corren, no se mezclan); los que no entran, «+N». La animación dura `transicion_ms`. Al terminar, cada uno vuelve a su lugar. |
| **Superponer sin tapar** (`superponer`) | El recuadro del orador va en el medio y la grilla le deja el hueco: los demás se acomodan alrededor, nadie queda tapado (`superponer_tamano`). Con `superponer_completa`, el orador a pantalla completa. Si no entran alrededor del hueco, se arma como «destacar» con la franja abajo. |
| **Cortar a la escena del orador** (`escena`) | Esta grilla no cambia (salvo `agrandar`). El botón **Armar la escena del orador** (o `grilla_personas_escena_orador`) crea, si no está, la escena «Orador · <grilla>» con una grilla «<grilla> · orador» que muestra únicamente al orador (`muestra: orador`, `sigue`: esta), a todo el lienzo y con su nombre y cargo, y la anota en `escena_orador`. **Pasar a la escena del orador** (`pasar_a_escena`): `a_mano` (de fábrica: corta el operador o la línea de tiempo); `automatico` (al cambiar el orador la app pone su escena al aire en `salida_orador`, "" = la principal, y cuando ya no hay orador vuelve a la de antes, salvo que el operador haya cortado a otra; el tiempo mínimo y la histéresis son los del orador); `asistente` (se lo pide al asistente de automatización, que corta si está autorizado, y el operador **siempre** recibe el aviso: «¿Paso a la escena del orador: X?» y, cuando el corte se ve, «El asistente pasó a …»). La grilla completa sigue en otra salida (salidas múltiples). |
| **Nada** (`nada`) | Como antes de todo esto: sin orador (ni capa alta para nadie); solo el borde a quien habla (si «Borde a quien habla»). |

**El cambio se anima** (2026-11-03). Con `agrandar` (`encima` o `achicando`), con
`destacar` y con `superponer`, cuando alguien toma la palabra la celda del orador **crece
suave** hasta su tamaño y las demás se achican (o se corren a la franja) **a la vez**; al
dejar de hablar, al revés. Dura `transicion_ms` (de fábrica 400 ms; 0 = en el acto) con
una curva suave (arranca y frena despacio). En esos modos y en `resaltar`, el orador lleva
**siempre** el borde del color de resaltar (`color_resaltar`), aunque «Borde a quien
habla» esté apagado, y el borde **aparece con un fundido** en el mismo tiempo (y se va
igual). Lo pinta B: solo viaja la celda cuyo lugar cambió; si la app vuelve a mandar el
mismo lugar a mitad de un viaje, ese viaje sigue sin rearrancar (antes cada pedido
rearrancaba el de todas desde quietas). Con las entradas juntas, sigue siendo un solo
reacomodo por tanda.

**Quién es el orador** (`disparo`): el que habla con **voz sostenida** (`voz_ms`, de
fábrica 1,5 s: una tos no alcanza), el que tiene **la palabra** en la Asamblea, el
**orador al aire** de la Conferencia (modo oradores) o **a mano** (el menú de cada cara
en la vista de la grilla: «Destacar como orador», o `grilla_personas_destacar`; lo
elegido a mano gana a cualquier disparo, en el acto). **Tiempo mínimo en pantalla**
(`minimo_ms`, 4 s): el orador no cambia antes (que no salte con cada tos); si se va, sí.
Con voz, cuando nadie habla se queda el último que habló.

**En un grupo de grillas, el que habla no cambia de grilla.** Se queda en su lugar, en
la grilla (y la pantalla) donde está, y se lo muestra ahí según el `al_hablar` de **esa**
grilla: resaltado en su lugar, agrandado, o destacado/superpuesto dentro de ella. Las
demás grillas del grupo no tienen orador mientras tanto. `grilla_personas_estado` dice
en cada grilla su `orador` (o `null`). Solo el orador elegido **a mano** para una grilla
(«Destacar como orador») va a esa grilla, como un fijado.

Antes (visto en la PC de pruebas el 2026-11-03, dos escenas con una grilla cada una del
mismo grupo, 60 invitados): cada vez que cambiaba el orador, el que hablaba subía a la
primera grilla del grupo y el último de esa bajaba a la segunda; al dejar de hablar, al
revés. El orador contaba como «fijado» en la grilla cuya máquina lo elegía (la primera,
porque todas aceptan a la misma gente) y además iba adelante en el orden que el reparto
corta. Ahora hay dos órdenes separados: el **de reparto** (los fijados y después
llegada, quién habla o prioridad; estable, el orador no va adelante) y la **prioridad
para entrar y para la capa alta** (el orador primero: si no hay lugar para todos, entra
igual en lugar del último elegido, y su cámara va en alta).

El orden de llegada se respeta en los dos repartos: **en orden**, la primera grilla con
los primeros que llegaron hasta su tope y la siguiente con los que siguen; **parejo**,
cada uno que llega va a la grilla que tiene menos (a igualdad, la primera), así que se
reparten por turno (1, 3, 5… en una y 2, 4, 6… en la otra) y cada grilla queda en orden
de llegada. Con las entradas juntas, el orador que llega mientras otros esperan entra en
el acto **y con él los que llegaron antes** (antes se colaba: «…33, 35, 36, 34»).

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

7. **La capa media (360p)** para las celdas grandes que no entran en la alta (pedido del
   2026-11-03: dos pantallas con grillas enlazadas, una con pocas personas en celdas de
   640×360 que se veían pixeladas con la baja estirada, porque las 4 de la alta las ocupaba
   la otra). `CapasGrilla` pone `media` a quien no quedó en alta y cuya celda pasa 200 px de
   alto (vuelve a la baja recién por debajo de 150). No cuenta para `alta_max` ni espera
   turno: **no es un tercer video**, es el transceptor de la alta topado a 360p (el mismo
   tope `calidad-grilla-360p` de siempre: escala y bitrate del codificador, sin renegociar).
   De la media a la alta (pasa a orador) solo se levanta el tope.

### Cada invitado manda solo lo que se usa

El navegador publica la cámara en dos transceptores (alta y baja), pero **solo codifica y
manda los que la sala le pide** (`encodings[0].active`): fuera del instante de un cambio
(las dos prendidas hasta que llega el primer cuadro de la nueva) va una sola capa por
invitado, y lo pausado no viaja ni se decodifica. Las capas posibles: baja (180p, 15 cps),
media (360p) y alta (720p). Se ve en `gateway_estado` → `videoSegunCarga`:
`capas` (`nada`/`baja`/`media`/`alta`, y `mandan_dos`: cuántos tienen las dos prendidas
ahora; tendría que ser 0 o casi), `entra_kbps` (`alta`, `baja`: lo que entra de cada capa)
e `invitados` (por cada uno: `capa`, `activas` y los kbps de cada capa).

### Lo que cuesta decodificar (muchos invitados)

Medido en la PC de pruebas (RX 5700 XT): el motor de video de la placa, que decodifica las
cámaras, va al 48 % con 40 invitados, al 82 % con 80 y **saturado desde 120**; saturado,
las celdas de la grilla caen solas de 15 a 7,5 cps. Dos cosas lo evitan:

1. **No se recibe a quien nadie ve** (`gateway.pausarSinVer`, de fábrica sí). Un invitado
   del programa que no está al aire ni en el preview de ninguna salida, ni en una escena
   que se **muestre** en algún lado (un monitor o ventana de escena, el multiview, una
   distribución de pantallas, una miniatura viva: van como `visibles`, en la baja), ni en
   una escena que **sale** por un canal de escena (pantallas de la sala, retornos de
   escena: cuentan como al aire), ni en una celda de una grilla (los del «+N» no tienen
   celda), ni lo mira una vista web, ni tiene su tarjeta **a la vista** en la interfaz, no
   manda la cámara: se le pausan las dos capas. Qué escenas se muestran lo dice el motor
   (`BroadcastEngine::escenasMostradas`: las vistas que pidió la interfaz y los canales de
   escena), con las escenas metidas dentro de otras. La tarjeta que no se ve
   (la interfaz la refresca una vez por segundo) se queda con la última imagen: no vale un
   decodificador. Cuando vuelve a hacer falta (aparece su tarjeta, entra a una celda, lo
   arman en el preview) la sala lo nota en ¼ s y el navegador la vuelve a mandar empezando
   por un cuadro clave (si el primero no lo es, la sala pide uno). Mientras está pausado su
   lugar del programa no muestra «sin señal»: queda la última imagen. Con 200 invitados y
   64 en la grilla, se decodifican los ~70 que se ven y no 224.
2. **Menos cuadros por segundo si la placa no da** (`gateway.cps.segunCarga`, de fábrica
   sí). Cada segundo B mira la carga del motor de video (Windows: los contadores del
   Administrador de tareas, todos los procesos; en todas: los cuadros que se pierden antes
   de decodificar). Con la carga en `gateway.cps.subirPct` (85 %) o más durante 2 s, les
   pide a los invitados en la baja menos cuadros: primero a la mitad de las celdas más
   chicas (y las tarjetas de la interfaz), después a todos; 15 → 10 → `gateway.cps.minimo`
   (7,5). Con la carga en `gateway.cps.bajarPct` (70 %) o menos durante
   `gateway.cps.recuperarS` (10 s), sube de a un escalón. La alta y la media no se tocan. Se
   ve en `gateway_estado` → `videoSegunCarga.control` (escalón, carga, medidas y las últimas
   decisiones) y `cpsBaja` (cuántos van a cada cps); el registro anota cada cambio de escalón.

Descartar cuadros antes de decodificar no sirve: el H.264 de los navegadores es todo de
referencia y la cadena de referencias se arma antes del decodificador (el siguiente quedaría
esperando al que se tiró). Lo que no se va a decodificar, mejor que no se mande.

### Resolución máxima del retorno (`retorno.tope`)

El video que reciben los invitados (el programa, el retorno) se codifica **una vez por
calidad** para todos (`retorno.capas`, de fábrica 720p, 360p y 180p) y cada conexión recibe
la que aguanta su red. Con muchos invitados por cable la red ya no frena: casi todos piden
720p y el que no da abasto es el gateway (medido con 200 en la PC de pruebas: el retorno
salía a 6 cps de 30). El ajuste **Configuración → Conferencia → «Video de retorno:
resolución máxima»** pone un techo para toda la sala:

- **Automática** (de fábrica): arranca en la calidad más alta del retorno (720p) y la
  regula el gateway según lo que mide;
- **360p, 480p, 720p, 1080p**: fija (1080p solo si el programa que llega es de 1080).

El tope cambia **qué calidades hay**, no cuántas veces se codifica: con 480p son 480p, 360p
y 180p (la de 480 aparece con su bitrate, escalado del de la de 360); con 360p, dos. El tope
que el operador le pone a una persona (calidad 360p desde la red) sigue mandando si es más
bajo, y dentro del tope cada uno recibe lo que aguanta su red.

**Cómo decide el automático.** Cada segundo, con el promedio de los últimos 3:

| Mide | De dónde |
|---|---|
| CPU de la máquina (y la del gateway, que se informa) | el sistema (Windows, macOS, Linux) |
| cps del retorno logrados contra debidos | los cuadros que salen hacia cada invitado contra los que le dio la fuente |
| descartes del codificador del retorno | las calidades que no llegan a codificar a tiempo |
| oyentes | los invitados que reciben el retorno |

- **No llega** si el codificador descarta el 10 % o más, o si el retorno sale a menos del
  70 % de lo que debería **con la máquina al 80 % o más**. Sostenido 3 s: baja un escalón
  (720p → 480p → 360p). Con la máquina holgada un retorno lento es la red de alguien: eso lo
  arregla el control de congestión de su conexión, no el tope de todos.
- **Sobra margen** si el retorno sale al 90 % o más (o no hay nadie), la máquina está al
  65 % o menos y el codificador no descarta. Sostenido 20 s: sube un escalón.
- Entre medio se queda; entre dos cambios pasan al menos 5 s.
- Al bajar de un escalón anota con cuántos invitados no llegaba: volver a él con la misma
  gente (o más) espera un minuto, y el doble cada vez que al volver no llega enseguida (hasta
  10 minutos). Con menos gente (menos del 80 %), vuelve apenas sobra margen.

**Dónde se ve.** En la Conferencia, panel «Uso de la conferencia»: «Video de retorno: hasta
480p (automático)» y por qué («Bajó: el retorno salía a 6,3 de 30 cps con la máquina al
88 %…»). Por MCP, `room_status` → `retornoTope` (`modo`, `alto`, `causa`: inicio, fijo, cps,
codificador o margen, `motivo`, `medida` y `decision`, la medida que decidió el último
cambio) y `gateway_estado` → `retornoTope` (además los umbrales y las últimas decisiones).
Se cambia con `settings_set` (`{"retorno.tope": "480p"}`; rige en el acto). Cada cambio
queda en el registro del gateway con el motivo.

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

**Cuántos aguanta de verdad** cada máquina medida (hoy: ~160 con margen y 200 andando en la PC
con Ryzen 9 5900X y RX 5700 XT; 40 con margen en la Mac M1 Pro, sin medir más arriba) está en
[docs/hardware](../hardware/README.md); para la máquina propia, **Configuración → Sistema →
Capacidad** ([CAPACIDAD.md](CAPACIDAD.md)).

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
