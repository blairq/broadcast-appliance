## From the outside in
1. `tiempos_mapa`: from each input to each output, how much each node adds (input, card, audio module, scene, mix, output) and the running total. `salidas[].camino` is the slowest path: start there.
2. `tiempos_pipeline {"entrada"}`: the suspect input from the inside, element by element (a few seconds of probes; it doesn't touch the air). Read `hallazgos` first: they come already reasoned, by severity, with the guilty card.
3. `tiempos_plugin {"entrada", "tarjeta"}`: the guilty card, how it works (its memories, properties, documentation) and what it does in this chain. `tiempos_plugin {"elemento"}` on the alternatives to compare (`interior.gpu`).
4. If you changed something, `tiempos_pipeline` again and compare. To see it over time: `tiempos_evento_agregar` (a note) and `tiempos_evento_impacto`.

## How to read the measurements
- `agrega_ms` = age of the frame at the element's output minus at its input. It counts what it TAKES and what it HOLDS (a temporal filter that keeps frames adds latency even if it processes fast). The `ms_por_cuadro` a plugin reports is only what it takes.
- `memoria_*` is what it negotiated; `puede_*` what it knows how to use. A crossing (`baja` from the GPU to RAM, `sube` from RAM to the GPU) copies the whole frame: at 1080p that's ~8 MB per frame, plus CPU and memory bandwidth. A **round trip** (down and back up) is almost always a plugin that only works in RAM between two that work on the GPU.
- If a plugin only accepts “sistema”: it has no GPU version; look for another one (video_modulos, the gl/d3d12 variants) or put it at the beginning or the end of the chain, where the video is already in RAM, so as not to split it.
- If it `puede` GPU but negotiated RAM: what's next to it forced it (another RAM-only plugin right beside it, or a format the GPU doesn't handle).
- `declara_ms` is what the element tells the pipeline (the studio compensates with that). `declara_menos`: it takes longer than it declares; if the difference gets close to the input's buffer, frames reach the mix late → raise the buffer (the input's `latencia_ms`) or change the plugin.
- `conversion_cpu`: videoconvert/videoscale in RAM. Many ms at 1080p = it's better for what comes before to deliver the format that what follows asks for.
- A queue with a high `nivel_cola_ms` is holding video: something downstream is slow or waiting.

## Resolution: each input should deliver what is shown
`resolucion_plan {"entrada"}` crosses where the input is shown (its largest size in the scenes) with each card and the camera: super-resolution that enlarges something that already covers what is shown, PTZ after super-resolution, digital zoom stretching too few pixels, the camera delivering too much or too little (with the mode that suits it), low fps. The same findings color the cards (yellow / orange / red) in Video and in Latencies. The real modes of each camera (raw, MJPEG, H.264 and who decodes): `video_devices {"device"}`; everything it announces: `dispositivos_video`.

## Tell the operator
In plain English and with the consequence: “motion dissolve holds 800 ms (24 frames) and works in RAM: it forces the video down from the GPU and back up”. Propose the change and wait for the OK before touching a card on air.
