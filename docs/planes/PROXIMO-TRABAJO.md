# Próximo trabajo

Lo pedido el 2026-10-23 se hizo el 2026-10-25 (siete áreas en paralelo, integradas y probadas en
la Mac). Queda lo de abajo.

## Hecho (2026-10-25)

- Riel: abierto o cerrado, los botones quedan en el mismo lugar.
- Terminal: clic en una opción numerada (Claude Code, Antigravity) manda su número; clic en la línea
  del cursor lo mueve (solo si el programa muestra el cursor real: Claude Code no).
- Gráficos animados: sin parpadeo en la vista previa; GIF y videos con alfa adentro; tortas y barras
  con datos que cambian en vivo; plugin como gráfico (ejemplo `ejemplo-grafico`).
- Nodo «Mezcla de GStreamer» (cualquier módulo de varias entradas; probado glvideomixer lado a lado).
- Grilla de personas (`ba_grilla`), con reparto entre grillas; el mosaico lo arma la sala.
- Atención hablada en el lobby de la Sala, «Voz del sistema» con «Voz del atendedor», alta
  automática en Personas, «entra solo» del padrón visible en Sala/Asamblea/Personas.
- Salidas → Pantallas: un solo catálogo de cosas que se pueden mostrar; monitores con orden físico,
  asignación recordada y espera del que no está.
- Dispositivos y equipos: un solo registro con nombres de la máquina; «Entradas de audio».
- Plugins: ejemplos (video con GStreamer, automatización, equipo de red), «Nuevo desde un ejemplo»,
  y el asistente ofrece hacer plugins (también elementos de GStreamer en Python).

## Pendiente

- **Probar a mano** (no se puede por MCP):
  - «Hablando» en la Sala y en la Asamblea con una persona real (celular), ver docs/funciones/ATENCION.md.
  - «Zócalo con cámara» con una cámara real: que el recuadro arranque junto con el zócalo, que la
    cámara vuelva a su lugar al ocultar, el recorte «cubrir» en GL y en D3D12, un TAKE con el gráfico.
  - La vista de las grillas (fijar, sacar, mover) en la interfaz; asignar un monitor de verdad.
- **Windows:** probado en la PC la mezcla con d3d12compositor, el gráfico de plugin con barras, los
  ids de monitores. Falta: tortas, VP9 con alfa, `test_ejemplos.py` de plugins con el Python de la PC.
  En el gráfico de barras los números salen sin negrita en Windows (en la Mac, con negrita).
- **Video en un gráfico puesto como capa DSK:** hoy solo en escenas.
- **Plantillas de assets/demo:** regenerarlas con tools/generar/plantillas.py para que traigan el
  «Zócalo con cámara».
