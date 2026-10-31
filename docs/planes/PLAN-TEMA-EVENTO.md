# Plan: tema propio del evento en todas las páginas web (relevamiento 2026-10-31)

Pedido: que a cada página web del sistema se le pueda aplicar un tema propio del evento.

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
