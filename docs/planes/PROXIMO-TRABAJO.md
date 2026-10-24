# Próximo trabajo

Lista de lo pedido el 2026-10-23 y dejado para después de la renovación de la cuota (domingo
2026-10-25, 14 h). Todo lo hecho hasta acá está en master (último: 6bdd4c9f).

## Errores y arreglos chicos

- **Vista previa del gráfico animado:** parpadea mucho al darle play.
- **Riel (barra de navegación):** cerrado, los botones tienen un espaciado; al abrirlo aparecen los
  títulos de grupo y todos cambian de posición vertical. Que queden en el mismo lugar
  (`RielNavegacion.qml`).
- **Build de Windows:** el zip de `dist/` es de 84e98a84; rearmarlo con lo último y actualizar la PC.
- **Clicks en Claude Code y Antigravity:** no piden ratón a la terminal (opencode sí, y ya anda).
  Propuesto, sin respuesta: un clic en una opción numerada («❯ 1. Yes») manda ese número; un clic en
  la línea donde se escribe mueve el cursor con flechas.

## Video

- **Nodo «Mezcla de GStreamer»:** un nodo del estudio como el Compositor, pero con cualquier módulo
  de varias entradas (`compositor`, `d3d12compositor`, `glvideomixer`, `glmosaic`, `smpte`,
  `shapewipe`, `glstereomix`). Sus entradas salen de las plantillas de pads del módulo
  (`sink_%u`, `sink1`/`sink2`, `video_sink`/`mask_sink`); el resultado se publica como fuente con
  nombre; en el catálogo, «+ Agregar como mezcla». Hoy el único nodo de varias entradas es el
  compositor del motor (`GrafoVideo` «compositor», `VideoGrafos` traer/`configCompositor`).
  Probar en Mac y en la PC (D3D12).
- **Grilla de personas dinámica:** una fuente que crece con la gente conectada. Por instancia:
  - origen: sala, asamblea o las dos;
  - grupo de reparto opcional: las grillas del mismo grupo se reparten la gente sin repetir, en modo
    «llenar en orden» (concentra) o «parejo» (distribuye);
  - tope por grilla con un cuadro «+N»;
  - lugares estables (entrar alguien no hace saltar a todos) con transición;
  - orden (llegada, quién habla, prioridad) y filtros.
  La misma fuente en dos escenas es la misma imagen: por eso una instancia por lugar, coordinadas
  por el grupo. **Primero decidir el video por persona:** hoy la sala entrega pocos (2 al aire,
  4 de lobby, 6 miniaturas de 320×180); o un video por persona, o el mosaico armado en la sala.
- **Gráficos animados:**
  - GIF animados y videos con canal alfa (WebM VP9 con alfa, ProRes 4444, secuencias PNG);
  - o animar un ítem de la escena con la línea de tiempo del gráfico (quizá más simple);
  - gráficos de torta y de barras animados, con datos;
  - un plugin como gráfico animado, para que el asistente programe uno a pedido.

## Sala, asamblea y personas

- **Atención hablada a quien se conecta:** en Sala y en Asamblea, «Los atiende el asistente» y,
  activado, «Hablando»: la voz de la persona pasa a texto, el asistente contesta y la respuesta le
  llega con voz. Las piezas existen (recepción del lobby, voz a texto, voz del sistema).
  - La sección «Voz del operador» pasa a «Voz del sistema», con pestañas «Voz del operador» y
    «Voz del atendedor» (la voz con la que atiende).
  - Decidir quién atiende (el rol de recepción o el asistente de automatización, no el del evento)
    y cuándo termina (al admitir, o sigue en la sala).
- **Alta automática en Personas:** registrar a todo el que dio su nombre y tiene cara reconocible;
  con la atención activada corre el reconocimiento siempre, sin depender de que el asistente se
  acuerde. Por evento, quién entra solo si se lo reconoce. Ya existen `rostro_autoalta` y la lista
  de auto-aceptar del evento: falta la opción visible en Sala, Asamblea y Personas.

## Organización de secciones

- **Salidas → Pantallas:** «Pantallas conectadas» (monitores de la máquina, ordenados como están)
  y «Pantallas de red» (TV, cast), eligiendo qué va en cada una: todo lo que hoy se saca a ventana
  (paneles del tablero, multiview, escenas, lo de fondo del tablero). **Un solo catálogo de «cosas
  que se pueden mostrar»** que usen el tablero, las ventanas y las pantallas: nada de copias. Se
  recuerda por monitor; si uno no está, se espera (como después de un corte de luz).
- **Entradas y dispositivos:** «Entradas de video» y «Entradas de audio», y los dispositivos con
  nombre en **«Equipos» ampliada** (no una sección nueva al lado: Equipos ya busca todo en la red,
  la máquina y el USB). Ahí se les pone nombre y se configuran; las entradas los muestran con ese
  nombre. Los nombres son de la máquina, no del evento.

## Asistentes y plugins

- **Plugins:** un buen ejemplo en la sección Plugins, y que el asistente del evento sepa dónde está
  y cómo hacer uno (ayuda MCP, prompt). Lo mismo con los de GStreamer: cuando le piden algo de video
  tiene que darse cuenta de que puede hacer un elemento (tarjeta `baplugin`).
