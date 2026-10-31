# Plan: tema propio del evento en todas las páginas web (relevamiento 2026-10-31)

Pedido: que a cada página web del sistema se le pueda aplicar un tema propio del evento.
**Todas** (aclaración del usuario, 2026-10-31): también las que ve la gente de afuera para
conectarse, votar en la asamblea (`/sesion/`) o hacer consultas anónimas (`/publico/`), el tótem
(`/registro/`) y los invitados (`/invitados/`). Esas llevan el tema **completo** por omisión (fondo,
acento, tipografía, logo, nombre): son la cara del evento. Las consolas de operador también lo
toman; ahí el alcance se puede elegir (por omisión acento y logo, para no romper el neumorfismo).
Solo quedan fuera los colores con significado: tally, aire/armado, vúmetros, sí/no de la votación.

## Lo que hay hoy

- **Tema global de las consolas (neuro)**: tokens en `assets/web/comun/neuro/temas.css`
  (`neuro-oscuro`, `neuro-claro`), catálogo `src/app/control/TemasWeb.hpp`, guardado en
  `AppConfig::temaWeb` (del aparato, no del evento). La app lo manda con `POST /api/v1/tema`
  (`StudioViewModel.cpp` ~3991), el gateway lo tiene en memoria y lo publica en
  `GET /comun/tema.json` (`SignalingServer.cpp` ~567-585, enganchado en ~802); `neuro.js` lo
  aplica con `<html data-tema>` (copia en localStorage, `?tema=` lo fuerza).
- **Identidad del sitio del evento**: `SitioWeb` (`EventoWeb.hpp`) en `Eventos/<x>/Web/sitio.json`
  con `color`/`fondo`, escritos **una sola vez** en `Web/estilo.css` al generar el sitio (cambiarlo
  después no reescribe el CSS). `marco.js` pone `--color` pero `marco.css` no lo usa.
- `evento.json` (`Evento::aJson/deJson`) **pierde claves desconocidas**: el tema no puede ir ahí.
- La app no sirve páginas: todo lo sirve el **gateway** (y por el túnel, **ba-web** con microcaché).
  Los gráficos al aire son QML, no HTML.

## Páginas (todas en `assets/web/`, servidas por el gateway)

| URL | Mira | Familia de estilos | CSP | Cambio |
|---|---|---|---|---|
| `/`, `/invitados/` (guest, ayuda.html) | invitado/público | guest (`--bg --accent…`, 46 colores fijos, degradado naranja) | no | M |
| `/evento/…` (marco, sitio, plantillas, `Eventos/<x>/Web/*`) | público | sitio (`--bg --accent --vivo…`) | no | S-M + migrar sitios generados |
| `/sesion/` | integrante | sala (sin `--acento`: `#ff6a2b` fijo) | sí | M |
| `/presidencia/`, `/control/`, `/botonera/`, `/rotulos/`, `/correccion/` | operador | neuro | parte | S |
| `/registro/`, `/registro-atiende/` | público / operador | sala | sí | M |
| `/publico/` | público en la butaca | sala | no | S-M |
| `/prompter/…` | orador | sala | no | S (texto y fondo no se tocan) |
| `/board/` | moderador | guest | no | S-M |
| `/cast/` | pantalla | sin variables | no | S |
| `/pantalla/?tipo=mensaje\|reloj\|qr`, `/pantalla/votacion/` | sala | sin variables | no | M |
| `/pantalla/carrusel/` | sala | (iframes) | no | – |
| `/pantalla/tally/` | cámara | **no tematizar** (seguridad) | no | – |

Hay tres dialectos de variables (neuro, guest, sala) y cuatro naranjas distintos. No se tematizan:
tally, `--aire/--armado/--ok/--aviso`, vúmetros, colores de votación, paleta de bloques.

## Diseño propuesto

- **Modelo** `ba::evento::TemaEvento` (C++ puro, con `resto` para claves desconocidas y test) en
  `Eventos/<x>/tema.json` + recursos en `Eventos/<x>/Tema/` (logo, fuentes). Si no existe, se arma
  desde `Web/sitio.json`. Tokens: `base`, `modo`, `colores{acento, acentoTexto, fondo, superficie,
  texto, textoSuave}`, `tipografia{texto, titulos, archivos}`, `forma{radio}`, `marca{nombre, logo,
  logoClaro, favicon}`, `alcance{publico, control: nada|acento|todo}` (por omisión en las consolas
  solo el acento: el neumorfismo depende de su fondo).
- **App**: `EventoViewModel` lo manda al abrir/cambiar de evento, aparte de `publicarWeb` (que no
  publica sin sitio) y reintenta si el gateway se reinicia. MCP `evento_tema` y panel en la app.
  El evento que manda es el abierto (el mismo que publica el sitio y el idioma).
- **Gateway**: genera `/comun/tema.css` con `--ev-*` y puentes a cada dialecto (`--acento`,
  `--accent`, `--color`, `--bg`/`--fondo`…), valores ya calculados (contraste WCAG para
  `acentoTexto`; nada de `color-mix`, por los Chromecast/TV viejos), selector más específico que
  `:root`; `/comun/tema.json` con la marca; logo y fuentes con `serveStatic` desde `Tema/`
  (agregar `.woff2` a `contentTypeFor`; SVG con `CSP: sandbox`; tope de peso).
- **JS común** `/comun/tema.js` (externo, sirve con la CSP): `data-tema`, `<link>` versionado,
  `[data-tema-logo]`, favicon, `theme-color`, copia en localStorage; `neuro.js` lo reusa.
- **ba-web**: `location` con caché para `/comun/tema.css` y `/comun/tema.json`.
- **Sitio**: `<link>` a `tema.css` entre `sitio.css` y `estilo.css` (el del usuario sigue pisando);
  la plantilla `estilo.css` deja de escribir color/fondo; migrar los sitios ya generados.
- **Opcional**: gráficos QML (`acento`) y `qr::Estilo` con el tema como valor por defecto.

## Riesgos

CSP (nada en línea, fuentes solo propias: sin internet), contraste, caché (`?v=<hash>`), el gateway
olvida el tema al reiniciar, fuentes que tienen que ceder ante la del perfil de idioma (CJK, RTL),
SVG con scripts, rutas dentro de `Tema/`, cuatro servidores estáticos parecidos (que `/comun/tema*`
quede solo en `SignalingServer`).

## Estado (2026-10-31): hecho

- **Modelo** `ba::evento::TemaEvento` (`src/app/evento/TemaEvento.*`, test `test_tema_evento`):
  `Eventos/<x>/tema.json` (lo desconocido se conserva, también adentro), recursos en `Tema/`
  copiados con huella en el nombre (`logo-<hash>.svg`), firma y tope de peso (logo 2 MB, fuente
  4 MB; SVG con `<script>`/`<foreignObject>` no entra), `limpiarRecursos`, `migrarSitio`. Sin
  tema.json pero con sitio hay tema: `paraPublicar` pone el acento y el fondo de `sitio.json` donde
  el tema no dice nada (un solo color que manda, sin copiarlo).
- **Cuentas** en el núcleo, `ba::tema` (`src/core/tema/TemaWeb.*`, test `test_tema_web`): colores,
  contraste WCAG, lo que falta mezclado, `acentoTexto` por contraste, avisos con código, nombres
  de fuente y de archivo saneados, la hoja. La usan el gateway (la hoja) y la app (los avisos del
  panel).
- **Gateway**: `POST /api/v1/tema-evento` {tema, carpeta}; `GET /comun/tema.css` (siempre 200; con
  `?v=<versión>` se guarda un día), `GET /comun/tema.json` → `{tema, evento: {v, css, nombre, modo,
  acento, acentoTexto, fondo, texto, logo, logoClaro, favicon, alcance} | null}`,
  `GET /comun/tema/<archivo>` (solo nombres planos de imagen o fuente, tope 4 MB, SVG con
  `CSP: sandbox`). `.woff2/.woff/.ttf/.otf` en `contentTypeFor`. ba-web: microcaché de
  tema.css (por URL entera) y tema.json. Test en `test_gateway_http` (12d).
- **La hoja**: cada página se anota en `<html data-ev="publico|control|acento">`; los tokens
  `--ev-*` y los puentes a cada dialecto van bajo `html[data-ev~="…"]` (0,1,1: le gana a `:root` y
  a `[data-tema=…]` de neuro sin depender del orden) y solo en las familias con alcance distinto
  de «nada». Nivel «acento»: `--acento --acento-texto --foco --accent --accent-2 --accent-ink
  --color`; «todo»: además fondo, superficies, texto, bordes, radio, letra (y `body`/`h1-h3` con la
  fuente del evento). `--hundido` no se puentea (sombra en neuro, color en la sala).
- **`/comun/tema.js`**: `<link>` versionado, `data-tema-evento`, `img[data-tema-logo]`,
  `[data-tema-nombre]`, favicon, `theme-color` (con «todo»), copia en localStorage, cada 15 s y al
  volver a la pestaña; evento `ba-tema-datos`. `neuro.js` ya no pide tema.json: escucha ese evento.
- **App**: `TemaEventoViewModel` (Studio.temaEvento) publica al abrir/cambiar de evento, al
  cambiar el tema o el sitio y cada vez que la sala vuelve; panel `PanelTemaEvento.qml` en
  Servicios → Aspecto de las páginas web; en Guion → Página web el acento es el del tema. MCP
  `evento_tema` (ver | cambiar | logo | fuente | quitar | restablecer | migrar_sitio) en
  `McpTemaTools.cpp`. Ayuda en `assets/idiomas/es/ayuda/evento.md`.
- **Páginas**: publico (tema completo): `/invitados/` y ayuda, `/sesion/` (con `--acento` nuevo en
  vez de `#ff6a2b`), `/publico/`, `/registro/` (logo grande), `/pantalla/`, `/pantalla/votacion/`,
  `/cast/` (el cartel de espera; el video no), el marco y las plantillas del sitio (tema.css entre
  sitio.css y estilo.css; estilo.css ya no fija color/fondo y su bloque `:root, html[data-ev]` le
  gana al tema). control (acento y logo): `/control/`, `/botonera/`, `/presidencia/`, `/rotulos/`,
  `/correccion/`, `/registro-atiende/`, `/board/`. acento siempre: `/prompter/`. Nada: tally,
  carrusel (sus marcos ya lo traen).

## Estado (2026-10-31, segunda tanda): hecho

- **Tema claro**: lo que las páginas del público tenían fijo para fondo oscuro (resaltes
  `rgba(255,255,255,…)`, campos y velos negros, la sombra, el borde del selector de idioma) pasa a
  variables con su valor de siempre en `:root` (`--tinte`, `--tinte-fuerte`, `--campo`, `--velo`,
  `--sombra`, `--borde-fuerte`): `/invitados/`, `/sesion/`, `/publico/`, `/pantalla/votacion/`, el
  sitio. La hoja las puentea en el nivel «todo» con `--ev-*` que `resolver` calcula según el modo
  (claro: el color del texto con poca opacidad, sombra 0,14; oscuro: lo de antes). Quedan fijos a
  propósito: lo que va encima de video (rótulos de los recuadros, el PIP) y los carteles de la
  votación (colores con significado).
- **Gráficos y QR con el tema por omisión**: `graficos::Catalogo::valoresPorDefecto` da a un
  parámetro de color `acento` (o que lo nombra con `"tema": "acento"`) el acento del evento abierto;
  el estilo de fábrica de los QR toma `ba::tema::coloresQr` (oscuro sobre claro con contraste ≥ 4,5:
  el acento, si no el texto, si no negro; el fondo del evento solo si es claro) y su logo al centro
  (`imagenComoDataUrl`; en la dirección al gateway viaja `logoEvento=1` y él pone el suyo). Lo pone
  `TemaEventoViewModel::ponerPorOmision` al abrir o cambiar el tema. Lo guardado o pasado a mano
  sigue mandando.
- **Logo**: tampoco entra un SVG con atributos de evento (`onload=`…). Probado contra el modelo
  (PNG, WebP, SVG con script/foreignObject/javascript:/onload, RIFF que no es WebP, tope de 2 MB, la
  huella) y el gateway (PNG/WebP con su tipo y nosniff, SVG con sandbox, > 4 MB, GIF y nombres que
  no son de `Tema/` no salen; el QR con el logo del evento).
- **Ayuda y prompt** del tema en en/fr/ru/pt-BR/zh-CN (la misma sección que la castellana).

## Falta

- Probar en un navegador con la app en `--test` (y en un Chromecast/TV viejo): `curl
  http://127.0.0.1:<puerto>/comun/tema.css`, cambiar el acento con `evento_tema` y ver que las
  páginas cambien solas en ≤ 15 s; un tema claro de punta a punta en `/invitados/` y `/sesion/`
  (capturas), y un zócalo y un QR nuevos con el acento y el logo del evento.
- Los sitios ya generados no cambian solos: `evento_tema migrar_sitio` (o el botón «Pasar el
  sitio al tema»).
- El logo SVG dentro de un QR que dibuja la app (QtSvg y `<image>` con un SVG en data URL): mirarlo
  en la pestaña QR; si QtSvg no lo pinta, rasterizarlo a PNG al ponerlo.
- Las cadenas nuevas (`ajustes`, `emisor`, `motor`) a los catálogos (`tools/i18n/extraer.py` y la
  traducción por lotes); `docs/mcp.json` no cambió (ninguna herramienta nueva).
