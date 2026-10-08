# Pantallas externas

Un televisor en la sala, el proyector del escenario, la pantalla del hall. La
página **Pantallas** los maneja a todos igual, y esa es la idea: que agregar
algo nuevo para proyectar no toque ni el protocolo ni la interfaz.

## Dos cosas separadas, a propósito

| | qué es | dónde vive |
|---|---|---|
| **destino** | dónde se muestra | `src/app/pantallas/` (protocolo) |
| **contenido** | qué se muestra | una página que sirve el gateway |

Hoy el único destino es el **Chromecast**. Mañana puede haber otro (una ventana
en un segundo monitor, un televisor por DIAL) y la página no cambia.

Y del lado del contenido está lo que hace que esto crezca solo: **todo lo que
se proyecta es una página web que el gateway ya sirve**. Un mensaje, un QR, la
hora y el teleprompter son la misma página (`/pantalla/`) con distintos
parámetros. Agregar algo nuevo es una entrada más en el catálogo
(`PantallasViewModel`, función `catalogo()`) y, si hace falta, un `tipo` más en
`assets/web/pantalla/app.js`. No se toca el Chromecast ni la pantalla del
operador: los campos del formulario salen del catálogo.

## Cómo se le habla a un Chromecast

`src/app/pantallas/Cast.{hpp,cpp}`. TLS al puerto 8009 con el certificado
propio del aparato (no lo firma nadie: hay que aceptarlo). Adentro, mensajes
con un largo de 4 bytes big endian y un protobuf chico (`CastMessage`) que
lleva origen, destino, un *namespace* y un texto, casi siempre JSON.

**El protobuf está escrito a mano** porque `CastMessage` tiene siete campos y
ninguno anidado: codificarlo son treinta líneas. Traer un compilador de
protobuf significaría vendorear una dependencia grande, generar código en el
build y arrastrarla a las tres plataformas, para una estructura que entra en
una pantalla.

La conversación: `CONNECT` a `receiver-0`, `PING`/`PONG` (si el aparato deja de
oírte, corta), `LAUNCH` de la aplicación receptora, y el `transportId` que
vuelve en el `RECEIVER_STATUS` es el destino de todo lo demás — con su propio
`CONNECT` antes de hablarle.

**Qué aplicación: DashCast**, una receptora pública cuyo trabajo es mostrar la
URL que se le mande. **Ojo con los dos nombres**, que cuesta una tarde: el id de
la aplicación es `84912283` y el *namespace* por el que se le habla es
`urn:x-cast:com.madmod.dashcast`. Lanzar el nombre largo devuelve `NOT_FOUND` y
el televisor se queda como estaba sin explicar mucho más.

Antes de mandar nada se pregunta `GET_APP_AVAILABILITY`: eso dice si el aparato
puede abrir DashCast **sin abrirla**, así se avisa «este no sabe mostrar
páginas» en vez de pisarle a alguien lo que está viendo para después fallar. La alternativa era escribir y
**registrar** una receptora propia en la consola de Google Cast (cuenta de
desarrollador) y servirla por HTTPS con certificado de verdad. Con DashCast no
hace falta nada de eso, y ese era el bloqueo que tenía anotado `EVENTOS.md`.

Cada orden abre el canal, hace lo suyo y lo cierra. No hay nada que mantener
vivo entre órdenes, y así no hay un hilo por televisor esperando de gusto.

## Encontrarlos

`Buscar.{hpp,cpp}`: los Chromecast se anuncian por mDNS como
`_googlecast._tcp.local`, con la misma `libmicrodns` que ya usa NDI (ahora
compilada también en Linux y macOS, no solo en Windows). De una respuesta salen
tres cosas y llegan por separado —el SRV dice máquina y puerto, el TXT el
nombre (`fn`) y el modelo (`md`), el A la dirección—, así que se juntan por
nombre de servicio y al final se descartan las que quedaron a medias.

Si el mDNS no llega (otra VLAN, un firewall), la pantalla se agrega a mano con
su IP. Y si el programa se compiló sin mDNS, lo dice en vez de mostrar una
lista vacía.

## Dos detalles que no son obvios

**HTTP, no HTTPS.** El gateway tiene HTTPS con certificado propio y un
televisor no tiene dónde aceptarlo: se planta y muestra un error. Por la red
local va HTTP.

**Cuál de nuestras direcciones.** Esta máquina puede tener seis IP (Docker,
VPN, WireGuard). Al televisor se le pasa la que está en **su** misma red,
eligiendo la de prefijo más largo en común. Si no, le mandaríamos una URL que
no puede alcanzar y la pantalla quedaría en negro sin decir por qué.

## Lo que cada pantalla tiene que estar mostrando

Un televisor de sala se apaga de noche, se reinicia con una actualización, o el
router le cambia la dirección. Si lo que proyecta viviera solo en la memoria de
la app, cada mañana habría que volver a mandárselo a mano.

Por eso se puede **fijar** un contenido: queda guardado con la pantalla y se le
manda solo cada vez que el aparato vuelve a aparecer. Se guarda contra el
**nombre de servicio mDNS**, no contra la IP — ese id no cambia cuando el DHCP
le da otra dirección, que es justo el caso que hay que sobrevivir.

La ronda de mantenimiento corre cada 15 s y reintenta con espera creciente
(hasta una vez cada cinco minutos) para no martillar a un televisor apagado.
**Nunca pisa una pantalla `ocupada`**: si alguien puso Netflix, lo decide una
persona.

Además de fijar, cada pantalla tiene sus comandos: consultar cómo está,
renombrar (el nombre que pone el operador gana sobre el que anuncia el aparato:
«TV del hall» dice más que «Percee TV»), parar y olvidar.

## Estados

`libre` (no hay nada corriendo), `mostrando` (lo nuestro), **`ocupada`** (hay
otra aplicación: Netflix, YouTube), `apagada` (no contesta) y `error`.

`ocupada` importa: mandarle algo le pisa lo que esté pasando. La interfaz lo
muestra y la ayuda del MCP le dice al asistente que pregunte antes.

## Por MCP

Categoría `pantallas`: `pantalla_listar`, `pantalla_buscar`, `pantalla_mostrar`
y `pantalla_parar`. Las pantallas se nombran por id **o por nombre** («el
televisor del hall»), que es como las piensa una persona. Todo pasa por el
mismo ViewModel que la pantalla del operador.

## Carruseles

Varias cosas rotando en una pantalla. Un carrusel es una lista de pasos, cada
uno con su contenido y sus segundos.

**Cada paso es una URL, y nada más.** Todo lo proyectable ya es una página, así
que cualquier cosa que se pueda mostrar se puede poner en un carrusel sin tocar
la página que lo rota: ella solo sabe rotar. Dos marcos que se turnan, y el que
entra se carga **escondido**: si se cambiara de golpe se vería el blanco de una
página a medio cargar en medio del salón.

La lista vive en el gateway (`/pantalla/carrusel.json?id=…`), no en la URL. Así
cambiar un paso no obliga a mandarle al televisor una dirección nueva —o sea, a
reiniciarle la página y que parpadee—: se republica y él la toma en la próxima
vuelta. Como vive en memoria, la app **las republica cada vez que el gateway
vuelve**; sin eso, un reinicio dejaba las pantallas diciendo «no encuentro ese
carrusel» (pasó).

## El latido: saber si de verdad se está viendo

Que el aparato acepte la orden **no quiere decir que la página se esté viendo**.
Pasó tal cual: el televisor aceptó, abrió el receptor y se quedó en «force
loading» para siempre, mientras del lado del operador todo decía «mostrando».
Un estado que miente es peor que no tener estado.

Por eso cada superficie web late cada 5 segundos contra `/api/superficie`
diciendo qué es y **qué está mostrando ahora** (el carrusel manda «paso 2/3:
Un mensaje»). Si deja de latir, no está. Se lee con `/api/v1/superficies`.

## Por qué hace falta HTTPS de verdad

Un receptor de Chromecast (DashCast) **es una página HTTPS**, y el navegador le
prohíbe cargar contenido `http://` — *mixed content*. Se queda «cargando» sin
decir nada. Y el HTTPS propio del gateway tampoco sirve: su certificado no lo
firma nadie que el televisor conozca.

Comprobado con una prueba de una sola variable: con el túnel apagado,
`https://example.com` abre en el televisor y nuestra página por http no, con la
URL correcta en pantalla.

O sea que **para mostrarle una página nuestra a un televisor hace falta el
túnel** (Configuración → Gateway), que da un origen con certificado real. Sin
él, la app lo dice y no manda nada, en vez de dejar el televisor colgado.
Casting de páginas de afuera (`https://…`) funciona igual.

## Una escena cualquiera, en varias pantallas a la vez

Además de «lo que sale al aire», una pantalla puede mostrar **cualquier escena
armada**, esté o no al aire: la cámara del recinto en el hall mientras el
programa muestra otra cosa, la tanda en el televisor de la cabina. Cada
pantalla con la suya. En la página es «Qué mostrar → Una escena» con la lista de
escenas; por MCP, `pantalla_mostrar {"tipo": "escena", "escena": "…"}`. Se puede
fijar como cualquier otro contenido.

Viaja igual que el programa (WebRTC, el enlace lo acuña el gateway, `/cast/`),
pero por un **canal de escena** propio:

1. La app pone la escena en un canal libre, `BA.Escena.<k>`
   (`BroadcastEngine::escenaACanal`). El motor la compone aparte
   (`verPedir("escena:<n>@pantallas<k>")`): sigue andando aunque no esté al
   aire. Sale a lo sumo a 1280×720, que es lo que el gateway le manda a un
   televisor.
2. Pide `/api/v1/cast-link?canal=k`; la URL de `/cast/` lleva `&canal=k` y la
   página lo repite al entrar por `/ws` (también al reengancharse).
3. El gateway guarda el canal con la pantalla y le da la ruta
   `MediaRoute::escena(k)`: le baja `BA.Escena.<k>` en vez del programa (el
   audio es el retorno del programa, como siempre).

**Dos pantallas con la misma escena comparten canal**: la escena se compone una
sola vez. Hay **4 canales** (`ipc::kCanalesEscena`), o sea hasta 4 escenas
distintas a la vez; la quinta dice que no hay lugar. Cada canal ocupado es una
composición más en el motor y una codificación por pantalla en el gateway.

**Un canal se suelta solo** en cuanto ninguna pantalla lo usa —pasó a otra cosa,
se paró, se apagó o se la tomó otra aplicación—: una composición entera
andando de gusto calienta la máquina (`PantallasViewModel::revisarCanales`, en
cada cambio de estado).

Lo que va en vivo (el programa o una escena) **no entra en un carrusel**: entra
con un permiso de un solo uso que acuña el gateway al mandarlo, y un paso de
carrusel es una URL fija.

Y un detalle que salió de acá: una pantalla con el programa también es alguien
que mira. La vista que baja el programa al gateway descansa si nadie la mira
hace 10 s (`retornoEnUso`), y solo contaba invitados e integrantes remotos: un
televisor solo con el programa se congelaba a los 10 s.

## Lo que falta

- **El programa en vivo ya está**, con una salvedad. `/cast/` es la página del
  invitado sin las partes de invitado: entra por `/ws` con un token que acuña
  `/api/v1/cast-link`, no publica nada y muestra lo que le baja, a pantalla
  completa. El enlace va por **HTTP y al puerto 8787** (no al de HTTPS): un
  televisor no tiene dónde aceptar nuestro certificado propio.

  **Una pantalla no es un invitado.** Entra por `/ws?pantalla=1` y queda
  *fuera* de la sala: no tiene nombre, no aparece en la lista, no pasa por el
  lobby, no ocupa un lugar de invitado y no puede escribir en el chat.

  Y **no puede mandar audio ni video**. Eso no depende de que la página se
  porte bien: el token viaja hasta el televisor y lo puede tener cualquiera, así
  que alcanza con abrir esa URL en otra máquina. Lo que lo garantiza es el
  servidor, en tres capas:

  1. `SignalingServer::soloRecibe` le mira el SDP al offer y **corta la
     conexión** si alguna pista ofrece subir. Callarse no sirve: en SDP, una
     sección sin dirección es `sendrecv`, y eso también se rechaza. Tiene su
     propio test (`test_pantalla`), porque es *la* garantía de que un televisor
     no sea un micrófono abierto en la sala.
  2. De sus mensajes solo se miran tres: `offer`, `ice` y `ping`. Un `chat` se
     descarta y queda en el registro — sin cortar la conexión, que sigue viva.
  3. Su ruta de medios es `backstage` sin preview: recibe el programa y lo que
     llegara a subir no va a ningún lado.

  Sobre el retardo: con el reproductor de fábrica del Chromecast (HLS/DASH, que
  va por segmentos) son 1 a 5 segundos. Por WebRTC son 100–300 ms. La perilla
  concreta es `RTCRtpReceiver.jitterBufferTarget` (antes `playoutDelayHint`, y
  la página pone las dos porque adentro de un televisor suele haber un Chrome
  viejo): cuántos ms retener antes de mostrar, 80 por defecto, ajustable con
  `?buffer=N`. Cerca de cero saca el colchón, al precio de que un hipo de red
  se vea como un salto. Y dos cosas que no son opcionales en un televisor:
  `<video autoplay muted playsinline>` —sin `muted` no arranca solo y ahí no
  hay nadie para hacer clic, así que el audio va en un `<audio>` aparte que se
  reintenta— y **H.264**, que estos aparatos decodifican por hardware.

  Con `?hud=1` la página muestra fps, pérdidas y el tamaño real del buffer: es
  lo que hay que mirar la primera vez, contra un televisor de verdad.

- **El cue `pantalla` del guion** sigue sin ejecutarse: ahora que existe el
  modelo, es conectarlo.
- **Aparatos sin pantalla.** Un Google Nest Mini aparece en la lista, acepta el
  protocolo **y hasta dice que DashCast está disponible**, pero no tiene dónde
  mostrar nada. O sea que `GET_APP_AVAILABILITY` no alcanza para distinguirlos.
  El TXT del mDNS trae un campo `ca` con las capacidades (un bit dice si hay
  salida de video); ese es el camino.
- **El primer casteo de verdad.** Con el id corregido, `GET_APP_AVAILABILITY`
  dice que sí en los dos aparatos de prueba, pero todavía no se mandó una URL a
  un televisor libre: el de prueba está con YouTube y no se pisa.
