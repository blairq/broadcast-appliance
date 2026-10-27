Vienen adentro del programa y estan PRIMERAS en el PATH de la consola, de los plugins y de todo lo que lanza el sistema. **No bajes ni instales nada** (ni brew, apt, pip, winget, ni un ffmpeg de internet): ya estan.

| Herramienta | Para que | Variable |
|---|---|---|
| `ffprobe` | Mirar un archivo: streams, codecs, duracion, `start_time` de audio y video | `BA_FFPROBE` |
| `ffmpeg` (9.0.2, con x264/x265) | Convertir, recortar, corregir desfase, sacar o cambiar el audio, normalizar volumen | `BA_FFMPEG` |
| `yt-dlp` (con `deno`) | Bajar un video de YouTube y de casi cualquier pagina (usa el ffmpeg incluido para unir audio y video) | `BA_YTDLP` |
| `gst-launch-1.0`, `gst-discoverer-1.0`, `gst-inspect-1.0` | Lo mismo con GStreamer (los mismos modulos que el grafo de video) | `BA_GST_LAUNCH` |

La carpeta es `BA_HERRAMIENTAS`. Si en los datos hay una carpeta `Herramientas/`, lo que tenga va ANTES (p. ej. un yt-dlp mas nuevo si un sitio cambio y el incluido ya no anda: solo eso, y avisandole al operador).

## Reglas
- Nunca escribas encima del original: el resultado va a `Media/` (asi aparece en `media_find`) o a donde te digan.
- Archivos largos: corre en segundo plano y mira el progreso; no trabes la consola minutos sin decir nada.
- En vivo, ffmpeg compite por la CPU con la emision: `-threads 2` y, si la maquina esta cargada (`system_hardware`), pregunta antes (`operator_ask`).

## Recetas
- **Mirar**: `ffprobe -v error -show_entries stream=index,codec_type,codec_name,start_time,duration -of compact "in.mp4"`.
- **Audio atrasado o adelantado** (desfase fijo, medido a ojo o con un aplauso/claqueta): adelantar el audio 0,3 s -> `ffmpeg -i in.mp4 -itsoffset -0.3 -i in.mp4 -map 0:v -map 1:a -c copy out.mp4` (atrasarlo: `+0.3`). Sin recodificar, es instantaneo.
- **Desfase que crece** (el audio se va corriendo de a poco): el reloj de audio esta mal; `-af "aresample=async=1000"` o estirarlo con `atempo` (duracion_video/duracion_audio).
- **Formato que OBS/GStreamer no lee bien**: `ffmpeg -i in.ext -c:v libx264 -crf 18 -preset veryfast -c:a aac -b:a 192k out.mp4`.
- **Recortar** sin recodificar: `ffmpeg -ss 00:01:00 -to 00:02:30 -i in.mp4 -c copy out.mp4`.
- **Volumen parejo** (para aire): `-af loudnorm=I=-23:TP=-1:LRA=7` (EBU R128).
- **Sacar el audio**: `ffmpeg -i in.mp4 -vn -c:a pcm_s16le out.wav`.
- **Video de una pagina**: `yt-dlp -f "bv*[height<=1080]+ba/b" --merge-output-format mp4 -o "<Media>/%(title)s.%(ext)s" URL`. Solo audio: `-x --audio-format mp3`. Un tramo: `--download-sections "*1:00-2:30"`.
- **Con GStreamer**: `gst-discoverer-1.0 in.mp4` (lo mismo que ffprobe); `gst-launch-1.0` para probar una cadena antes de llevarla al grafo.

Despues: `media_find` para encontrarlo y usarlo en una escena.