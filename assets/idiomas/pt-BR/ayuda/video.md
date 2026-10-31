Um só grafo para todo o vídeo do estúdio, como um pipeline do GStreamer. Cada entrada (câmera, rede, virtual) é um subgrafo: `<entrada>.entrada` -> cartões -> `<entrada>.escena` (o que as cenas veem). Os cartões de imagem rodam na cadeia do GStreamer de cada entrada; os que o motor ainda não roda (saída, tomada, compositor) dizem isso no seu estado. Todas as entradas andam com o relógio de vídeo do estúdio (`status`: canvas e fps).

## Como trabalhar (sempre nesta ordem)
1. **Olhar**: `video_estado` (todas, com seu diagnóstico) e `video_estado {"entrada": ...}` antes de mexer em qualquer coisa: tamanho, fps, formato, cor (BT.601/709, faixa), se chega entrelaçada, relógio, buffer de rede. `video_grafo` mostra os cartões.
2. **Montar** com `video_pipeline` (UMA chamada por entrada, valida tudo e não muda nada se algo falhar). As ferramentas avulsas (`video_tarjeta_agregar`, `_ajustar`, `_quitar`, `video_cable`) são para retoques.
3. **Verificar**: `video_estado {"entrada": ...}` de novo (o `diagnostico` tem que ficar sem erros) e `foto_fuente {"name": ...}` para ver a imagem.

## Receitas
**A mesma correção de cor em várias câmeras** (mudando uma vez, muda em todas), em UMA chamada:
`video_pipeline {"entradas": ["Sony", "Pelco", "Panasonic"], "tarjetas": [{"tipo": "gst", "elemento": "videobalance", "propiedades": {"contrast": 1.1, "saturation": 0.9}, "compartida": "Color camaras"}], "local_por_entrada": {"Pelco": {"propiedades": {"hue": 0.04}}}}`. O controlador fica como `estudio.color-camaras`. `modo: "agregar"` se a entrada já tem cartões que precisam ser conservados (por padrão os substitui). Depois, ajustar o comum: `video_tarjeta_ajustar` sobre qualquer um dos controlados (vai para o controlador) ou sobre o controlador. `video_controlador` para fazê-lo a partir de um cartão que já existe.

**Corrigir uma câmera** (SEMPRE primeiro GStreamer): cartão `gst` com `videobalance` (brilho, contraste, saturação, matiz) e `gamma`; uma dominante (verde, magenta): `hue` pequeno (±0.02 a ±0.08). Só se o GStreamer não der conta: o cartão `color` próprio.

**Entrelaçada / mal declarada** (`video_estado` diz): `{"tipo": "normalizar", "params": {"desentrelazar": "yadif"}}` (desentrelaça na fonte, onde estão os campos); cores deslocadas em HD que declara 601: `{"matriz_declarada": "601", "matriz_real": "709"}`. Os quadros por segundo diferentes do canvas a entrada converte sozinha (repete ou pula quadros): não é preciso cartão.

**Mosaico / picture-in-picture / lado a lado**: `video_mosaico {"escena", "entradas": [...], "disposicion": "mosaico|pip|lado", "nombre"}` em UMA chamada (pega cada entrada com seus cartões e oculta as avulsas nessa cena: o mosaico continua recebendo-as). Para retocar: `video_tarjeta_ajustar` sobre o compositor com `capas.in_N` {x, y, ancho, alto 0..1, opacidad, orden}.

**Unir câmeras: panorâmica ou 360** (tudo na GPU): `video_panoramica {"camaras": [...], "preset": "fila"|"360", "campo", "escena", "nombre"}` em UMA chamada. Une até 8 câmeras (um stream cada; a primeira é a lente 1) lado a lado no seu canvas e corrige a deformação de cada uma e como se encaixam. `preset: "fila"`: câmeras comuns de `campo` graus distribuídas com 10% de sobreposição; sai a **faixa** (panorâmica cilíndrica) do tamanho da soma das resoluções (4 de 1080p = 7680x1080; máximo 16384 de largura). `preset: "360"`: duas olho de peixe de 190° costas com costas; sai a **vista**, uma câmera comum que é um PTZ virtual (`yaw`, `pitch`, `fov` = pan, tilt, zoom). Uma 360 que manda tudo num SÓ stream (equiretangular ou os dois círculos juntos): cartão `vista360` na sua cadeia (`modo`).
Calibrar manualmente com `video_tarjeta_ajustar` sobre o cartão (`estudio.panoramica`), olhando com `foto_fuente`:
1. `ver: "entrada"`: o bruto com os círculos das olho de peixe e os limites de cada câmera; mover `lenteN_x`, `lenteN_y`, `lenteN_radio` até os círculos se encaixarem.
2. `ver: "tira"` (ou `"panorama"`) com `guia: "esfera"` (paralelos e meridianos a cada 15°): como cada uma se encaixa (`lenteN_yaw`, `_pitch`, `_roll`) e seu `_campo`, até o horizonte ficar reto e as emendas não aparecerem (`mezcla`: a largura da união, em graus).
3. Uma guia `rectangulo` ou `circulo` (`guia_x`, `guia_y`, `guia_ancho`, `guia_alto` em frações da saída) em cima de algo que o seja (uma porta, um relógio): a deformação (`lenteN_k1`, `_k2`; barril < 0 < almofada) até coincidir. `lenteN_tipo`: `ojo` (olho de peixe) ou `normal`.
Os parâmetros, um por um: `video_tarjeta_ayuda {"tipo": "panoramica"}`. Na interface: página Vídeo, o cartão, com atalhos para cada passo.

**Streams de rede** (SRT, RTMP, RTSP): `video_entrada_red {"nombre", "uri", "escena", "latencia_ms"}` (nunca `network_input_add` para isso: essa não se sincroniza). Cada quadro sai na sua hora de captura + latência (um buffer próprio). Mais latência = mais tolerância à rede e mais atraso.

**Sincronizar entradas**: `video_sincronia {"a": ..., "b": ..., "aplicar": true}` mede quanto uma atrasa em relação à outra comparando o que se vê (precisa de algo em comum que mude: a mesma imagem, movimento, uma palma) e corrige; medir de novo deve dar ~0. Manualmente: `video_desfase {"entrada", "ms"}`.
- O RTSP traz a hora de captura de quem envia (RTCP/NTP): com o mesmo relógio se alinha sozinho.
- SRT e RTMP não a trazem: mede-se com `video_sincronia`.
- Câmeras locais: só podem ser atrasadas (retardo); para alinhar com uma de rede, atrasa-se a local ou adianta-se a de rede (o desfase dela aceita negativos).

**Relógios**: `video_reloj` sem nada diz o relógio do estúdio e como está em cada entrada de rede. `{"tipo": "ntp", "servidor": ...}` (todos contra o mesmo NTP), `{"tipo": "ptp", "dominio": 0}` (IEEE 1588), `{"tipo": "red", "servidor": ip, "puerto": 8554}` (o relógio de OUTRO aparelho), `{"publicar_puerto": 8554}` (publicar o deste para que outros o usem), `{"latencia_ms": ...}` (o buffer de todas as de rede).

**Mixagem do GStreamer** (qualquer módulo de várias entradas: `compositor`, `glvideomixer`, `d3d12compositor`, `glmosaic`, `smpte`, `shapewipe`, `glstereomix`; `video_modulos {"soloMezclas": true}` lista os deste sistema): `video_tarjeta_agregar {"tipo": "mezcla", "elemento": "smpte", "params": {"propiedades": {"type": "bar-wipe-lr"}, "nombre": "Cortina"}}`. Suas portas são as do módulo (`sink_0`, `sink_1`… sob demanda, sempre uma livre; `sink1`/`sink2` no smpte; `video_sink`/`mask_sink` no shapewipe): `video_cable` de cada entrada para sua porta. As propriedades de cada entrada (xpos, ypos, width, height, alpha, zorder…): `video_filtros {"id", "puerto"}` as lista e `video_tarjeta_ajustar {"id", "params": {"pads": {"sink_1": {"xpos": 960}}}}` as muda. Sai como fonte com `nombre` (`video_salida_a_escena`). Roda na GPU se o módulo puder (D3D12 no Windows, GL no Mac/Linux).

**Módulos do GStreamer**: `video_modulos` (catálogo documentado; `buscar`, `categoria`, `elemento` com suas propriedades). Só os `tarjeta: true` vão numa entrada; os `mezcla: true`, como Mixagem do GStreamer. Um módulo em 4:2:0 ou com latência é avisado pelo diagnóstico. As propriedades são passadas com seu nome do GStreamer (os enum pelo nick).

**"Está tudo bem?" (revisar tudo)**: `video_estado` sem nada (todas as entradas com seu diagnóstico) -> aplicar as correções de gravidade `aviso` ou `error` (cada uma traz a chamada exata; as `info` são só para contar) -> se foram mexidos desfases ou relógios, `video_sincronia` sem aplicar entre as que têm que coincidir (deve dar ~0) -> `status` (fps, quadros perdidos, CPU) -> `mixer_list` (que áudio trazem: as entradas ocultas num mosaico continuam soando nessa cena se trazem áudio). Um “chegaram atrasados” com “recém-conectada” é a rajada do início: não aumente o buffer, olhe de novo em 10 s.

**Ver a imagem**: `imagen_preguntar` sobre uma cena ou fonte, com uma pergunta concreta (“há algo esticado ou preto?”, “que hora marcam os relógios de cada quadro?”). Serve para enquadramento, pretos, esticados e ler texto; NÃO para comparar brilho ou cor fina entre quadros: para isso existem os números (`video_grafo`, valores efetivos).

**Rostos**: o cartão `rostros` é colocado por Pessoas sozinho ao usar `entrada:<camara>`; `marcar` desenha os rostos.

**O que nenhum módulo faz** (um efeito próprio, um filtro incomum, uma análise: contar, detectar movimento, ler algo da imagem): NÃO diga que não dá. Programa-se em Python como cartão de plugin (`baplugin`), com numpy ou com GStreamer dentro (cadeias e elementos próprios escritos em Python): `help {"topic": "plugins"}`, seção «Video», e o molde `ejemplo-video-gst`.

## Bom saber
- Os cartões `gst` adicionam 1 quadro de latência (não travam o vídeo). Se duas entradas são comparadas e só uma tem `gst`, isso é um quadro de diferença: leve em conta ou meça com `video_sincronia`.
- `video_grafo_limpiar {"entrada"}` deixa uma entrada como veio.
- Entradas virtuais de teste (barras, cartela de zonas, bola): `video_entrada_virtual {"nombre", "preset", "escena"}`.