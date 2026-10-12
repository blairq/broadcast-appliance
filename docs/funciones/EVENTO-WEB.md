# La página web del evento en vivo

Cada evento tiene su sitio: lo que ve el público en la dirección del evento mientras
pasa. La raíz de la dirección pública (p. ej. `https://entrevista-pepito.a.pinggy.link/`)
muestra la página de la **sección** (bloque del guion) que se está tratando, y cambia
sola cuando el guion avanza. Arriba va una franja «en vivo» (qué pasa ahora, qué sigue,
cuánto lleva). Los invitados a la sala entran por `/invitados/` (los enlaces viejos
`/#room=…` se redirigen solos).

## Dónde vive

En la carpeta del evento, así que es parte del evento y queda en el historial
(`src/app/evento/EventoWeb.hpp`):

```
<evento>/Web/
  sitio.json             título, bajada, color, fondo, subdominio, barra, direccion_al_iniciar
  estilo.css             el estilo de la plantilla
  portada.html           antes de empezar
  seccion-<bloque>.html  una por bloque del guion
  cierre.html            cuando terminó
  (imágenes, logos, otros .html: lo que se quiera)
```

- **Se genera sola** una plantilla básica al crear el evento, y al guardar el guion se crean
  las páginas de las secciones nuevas. Nunca se pisa lo editado (salvo «Rehacer todo»).
- **La personaliza el asistente** (`evento_web`) o se edita a mano. La plantilla es la base
  y el ejemplo de la estructura (programa con enlaces, anterior/siguiente, datos en vivo).

## El diseño

Estilo «dark editorial» (fondo casi negro, bordes de 1px, tipografía grande, un solo acento,
mono para fechas y metadatos), sin dependencias de afuera: anda aunque la sala no tenga
internet. El sistema vive en el programa (`assets/web/evento/sitio.css`, servido en
`/evento/_/sitio.css`), así que mejorarlo mejora todos los eventos; lo propio de cada uno
(acento, fondo, lo que quiera pisar) va en su `Web/estilo.css`.

La plantilla: barra de navegación (con menú de celular), hero con la palabra de marca de
fondo, cuenta regresiva en cajas, marquesina con las secciones, «01 / En vivo» (reproductor
con pestañas YouTube / Twitch / Facebook y botones), «02 / Programa» (lista tipográfica con
hora prevista y duración), «03 / Cómo seguirlo» (A/B/C), «04 / En números», «05 / Preguntas»
(acordeón accesible), cierre y pie. Cada sección: hero con su número, Ahora / Sigue / Lleva,
el vivo, el programa y anterior/siguiente. El cierre: «Gracias», números, la grabación y lo
que pasó. Con JSON-LD (schema.org Event), Open Graph y salto al contenido.

Las páginas que siguen tal cual las escribió la plantilla (su huella queda en
`sitio.json` → `plantillas`) se actualizan solas cuando cambian la plantilla o el guion;
las editadas no se tocan.

## La API del evento (para el HTML)

Toda página carga `<script src="/evento/_/vivo.js" defer></script>`:

- `BAEvento.on(fn)`: llama a `fn(estado)` al cargar y cada vez que el evento cambia.
  `BAEvento.off(fn)`, `BAEvento.estado()`, `BAEvento.ahora()` (hora de la sala, ms),
  `BAEvento.duracion(seg)`.
- El evento `evento` de `window` (`e.detail` = el estado).
- Atributos `data-vivo`: `estado`, `seccion`, `siguiente`, `cuenta`, `transcurrido`,
  `agenda` (una `<ol>`: marca `hecha`/`actual` y se rehace si cambia el guion).
- `GET /api/evento`: el mismo estado en JSON (sin caché). El marco lo pide cada 2 s y se lo
  pasa a la página por `postMessage`; una página abierta sola lo pide cada 3 s.

El estado (solo lo público: nunca el texto de los cues ni las notas del operador):

```json
{
  "activo": true, "nombre": "Entrevista a Pepito", "bajada": "", "color": "#ff6a2b",
  "fecha": "2026-10-15", "inicio": "19:00", "huso": "America/Argentina/Buenos_Aires",
  "inicioMs": 1792101600000, "arrancoMs": 0, "ahoraMs": 1792100000000,
  "estado": "proximo | en_vivo | pausa | terminado",
  "seccion": {"id": "b2", "nombre": "Preguntas", "pagina": "seccion-b2.html",
              "inicioPrevistoMs": 0, "duracionSeg": 900, "estado": "actual"},
  "siguiente": {…} ,
  "secciones": [{"id", "nombre", "duracionSeg", "pagina", "inicioPrevistoMs",
                 "estado": "hecha | actual | pendiente"}],
  "pagina": "seccion-b2.html", "barra": true, "version": 3
}
```

## Dónde verlo en vivo (YouTube, Twitch, Facebook)

`GET /api/evento/vivos` → `{activo, vivos: [{plataforma, nombre, url, embed, enVivo}]}` (también
va como `vivos` dentro de `/api/evento`). Salen solos de las cuentas conectadas
(`evento::armarVivos`, lo que sabe StudioViewModel):

- **YouTube**: el vivo que está transmitiendo la app (`watch?v=` y `/embed/<id>`); si no, el vivo
  del canal (`/channel/<id>/live` y `/embed/live_stream?channel=<id>`).
- **Twitch**: el canal de la cuenta (`twitch.tv/<canal>`, `player.twitch.tv/?channel=`; vivo.js
  le agrega el `parent` que Twitch exige).
- **Facebook**: el vivo que creó la app en la página (`facebook.com/<página>/videos/<id>/`, embebido
  con el plugin de video).

Por evento se fijan en `sitio.json` → `vivos {youtube, twitch, facebook}` (Guion → Página web →
«Dónde se ve en vivo», o `evento_web {"accion": "sitio", "vivos": {…}}`): un link o id de un vivo
programado de YouTube, otro canal… Lo fijado manda. En el HTML: `data-vivo="reproductor"`
(el vivo embebido; `data-plataforma` para elegir), `data-vivo="vivos"` (botones) y
`data-vivo="donde"` (se esconde si no hay dónde verlo). La plantilla trae las tres cosas.

## Cómo llega a la sala

La app (EventoViewModel::publicarWeb) le manda al gateway `POST /api/v1/evento-web`
`{activo, dir: <carpeta Web/>, estado}` al abrir el evento, cada vez que el guion avanza y
cada 10 s (por si la sala se reinició). El gateway sirve:

| Ruta | Qué |
|---|---|
| `/` | el marco del evento (assets/web/evento) si hay evento con sitio; si no, la sala de invitados |
| `/invitados/` | la sala de invitados |
| `/evento/<archivo>` | la carpeta Web/ del evento (sin salirse de ella) |
| `/evento/_/<archivo>` | lo del programa: `vivo.js`, `marco.js`, `marco.css` |
| `/api/evento` | el estado público |
| `/api/evento/vivos` | dónde verlo en vivo (YouTube, Twitch, Facebook) |

Un ensayo no se publica (la página queda en «próximamente»).

## La dirección del evento

`sitio.json` → `subdominio` (sale del nombre: «Entrevista_Pepito» → `entrevista-pepito`;
el DNS no acepta «_»). Con la API key de pinggy, «Usar esta dirección ahora» (o
`evento_web {"accion": "direccion"}`) cambia el subdominio de la cuenta y reinicia la sala.
Con `direccion_al_iniciar`, se hace solo al poner el evento en curso (si no se está
transmitiendo ni grabando). El plan Pro tiene un subdominio: la dirección es la del último
evento que la tomó.
