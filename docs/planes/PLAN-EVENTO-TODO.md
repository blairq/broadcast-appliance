# Plan: cada evento tiene todo lo suyo

Pedido (2026-10-13): gente que no conocía el sistema no sabía por dónde empezar; un
asistente (Haiku) no entendió «arrancar de cero» y se puso a modificar las escenas
existentes; y no estaba claro si cambiar de evento cambia la distribución de la
asamblea, los medios o la página web. Decisión del usuario: **cada evento es una
carpeta completa** (escenas, mezclador, medios, gráficos, plugins, asamblea, orden del
día, votaciones, página web, guion, padrón) y la palabra «configuración» desaparece de
la interfaz. Al crear un evento: **desde cero, copiando el actual, desde uno anterior o
desde una plantilla**.

## Cómo es hoy

- `<datos>/Configuraciones/<nombre>/` tiene TODO (Config/, Media/, Graficos/, Plugins/,
  Fotos/, Miniaturas/, Eventos/, Recordings/, Shares/, Exportado/, Extensiones/).
  Cambiar de configuración reinicia la app.
- Un **evento** vive ADENTRO de una configuración (`Eventos/<evento>/`): solo su guion,
  padrón, página web, discursos, medios de los cues y las fotos de Config/ al empezar y
  al cerrar. «+ Evento» crea un guion vacío y hereda escenas, mezclador, medios y
  asamblea de la configuración. Orden del día, quórum y votaciones son de la
  configuración y se arrastran al evento siguiente.
- La máquina (no cambia con nada): Modelos/, Herramientas/, Agentes/, Credenciales/,
  Logs/, Runtime/, Cache/.

## Cómo queda

**Un evento = una carpeta `Configuraciones/<evento>/`** (la de hoy, que ya tiene todo).
No se mueven carpetas: cambia lo que significan y cómo se muestran.

- Cada carpeta tiene **un** evento propio: su guion, padrón, web y discursos en
  `Eventos/<evento>/` como hoy (el «en curso»), más la jornada automática.
- **Abrir otro evento** = pasar a su carpeta (reinicia la app, como hoy al cambiar de
  configuración; avisando si hay algo al aire o grabando).
- **Evento nuevo** (diálogo único en la página del evento y por MCP):
  - **desde cero**: carpeta de fábrica (escenas base, sin medios ni asamblea);
  - **copiando el actual**: ajustes, escenas, mezclador, medios, gráficos, plugins y
    asamblea; SIN historial (grabaciones, bitácora, votaciones, eventos anteriores);
  - **desde uno anterior**: igual, a partir de otra carpeta, o de la foto de Config/
    con que se hizo un evento cerrado;
  - **desde plantilla**: las demos del programa y las carpetas marcadas como plantilla.
  Siempre se pide el nombre; nunca se toca el evento abierto.
- **Histórico**: lista los eventos de TODAS las carpetas (no solo los de la abierta).

## Interfaz

- La barra lateral: **Evento** (lo que hoy es «Guion»: guion, padrón, público, orden del
  día, web) **antes que Sala**, con el nombre del evento abierto arriba y «Evento nuevo…»
  / «Abrir otro evento…».
- Configuración › Sistema › Configuraciones pasa a llamarse **Eventos** (la misma lista:
  abrir, duplicar, renombrar, borrar, marcar como plantilla).
- Los textos que dicen «configuración» para esto pasan a decir «evento».

## Asistente (MCP)

- `evento_nuevo {nombre, desde: "cero"|"actual"|"<evento>"|"plantilla:<nombre>"}` y
  `evento_cambiar {nombre}` (sobre configuracion_crear/cambiar, que quedan como alias).
- `evento_abrir` deja de crear carpetas: abre el guion del evento en curso.
- En sus instrucciones: «evento nuevo / arrancar de cero» = `evento_nuevo`, nunca editar
  las escenas del abierto; preguntar desde dónde si no lo dijeron.

## Migración (decidido: separar cada uno)

Hoy una configuración puede tener varios eventos adentro (en la Mac: «Principal» tiene
«Debate La Grande Armée…», «Prueba Mac 1012», «Prueba web (borrar)» y las jornadas
automáticas). **Cada evento pasa a su propia carpeta**, copiando los ajustes, escenas y
medios de la carpeta donde estaba (sin grabaciones ni el historial de los otros). La
carpeta original se queda con el evento en curso y las jornadas automáticas. Una vez,
al arrancar, con copia de seguridad antes de tocar nada.

Abrir otro evento **reinicia la app** (decidido): avisa y no deja si hay algo al aire o
grabando.

## Fases

- **E1** — Evento nuevo (4 opciones) y abrir otro evento, por la interfaz y el MCP; copiar
  sin historial; instrucciones del asistente; barra lateral y nombres.
- **E2** — Histórico de todas las carpetas; plantillas marcadas; migración.
- **E3** — Medios: los de los cues (Eventos/<e>/Medios) y los de la página Medios en un
  solo lugar.

## Estado (2026-10-13)

- **E1 hecho**: evento nuevo (cero, copia, otro, plantilla, foto de uno cerrado) sin
  historial y con su evento en curso; abrir otro (reinicia; no con algo al aire);
  página del evento con el evento abierto arriba; barra lateral «Evento» antes que Sala;
  Configuración › Sistema › Eventos con plantillas; MCP eventos_listar / evento_nuevo /
  evento_cambiar; el asistente sabe qué es «arrancar de cero».
- **Migración hecha** (Configuraciones::separarEventos, test_eventos_separar): una vez,
  con respaldo en <datos>/Respaldos/eventos-antes-de-separar-*. En la Mac: «Debate: La
  Grande Armée y la derrota» pasó a su carpeta; «Principal» quedó con «Prueba Mac 1012».
- **Histórico de todos los eventos** y cuánto ocupa cada uno (medido en otro hilo; las
  grabaciones aparte). Los registros de otro evento se nombran «@<evento>/<carpeta>».
- **E3 hecho**: los cues de música y video usan los medios del evento (Media/, la página
  Medios); si no está, Medios/ de la carpeta del guion (los guiones de antes).
  evento_revisar busca en los dos.
