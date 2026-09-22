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

**Qué aplicación: DashCast** (`com.madmod.dashcast`), una receptora pública
cuyo trabajo es mostrar la URL que se le mande. La alternativa era escribir y
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

## Lo que falta

- **El programa en vivo.** Está declarado en el catálogo y marcado como
  «todavía no», pero está más cerca de lo que parece: el gateway **ya manda el
  programa por WebRTC a un navegador** (es lo que ve un invitado de la sala
  mientras espera su turno), y DashCast pone cualquier página en el televisor,
  así que tampoco hace falta registrar una receptora propia. Lo que falta es
  escribir `/cast`: la página del invitado sin las partes de invitado — recibe
  y muestra a pantalla completa, sin micrófono, sin cámara, sin chat.

  Sobre el retardo: con el reproductor de fábrica del Chromecast (HLS/DASH, que
  va por segmentos) son 1 a 5 segundos. Por WebRTC son 100–300 ms. La perilla
  concreta es `RTCRtpReceiver.jitterBufferTarget` (antes `playoutDelayHint`):
  cuántos ms retener antes de reproducir. Cerca de cero saca el colchón, al
  precio de que un hipo de red se vea como un salto. Y dos cosas que no son
  opcionales en un televisor: `<video autoplay muted playsinline>` (sin `muted`
  no arranca solo y ahí no hay nadie para hacer clic) y **H.264**, que estos
  aparatos decodifican por hardware.
- **El cue `pantalla` del guion** sigue sin ejecutarse: ahora que existe el
  modelo, es conectarlo.
- **Aparatos sin pantalla.** Un Google Nest Mini aparece en la lista y acepta
  el protocolo, pero no tiene dónde mostrar nada. El TXT del mDNS trae un campo
  `ca` con las capacidades; se podría usar para marcarlos.
