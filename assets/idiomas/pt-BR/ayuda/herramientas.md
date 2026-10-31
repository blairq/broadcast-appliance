Vêm dentro do programa e estão em PRIMEIRO lugar no PATH do console, dos plugins e de tudo o que o sistema lança. **Não baixe nem instale nada** (nem brew, apt, pip, winget, nem um ffmpeg da internet): já estão aí.

| Ferramenta | Para quê | Variável |
|---|---|---|
| `ffprobe` | Olhar um arquivo: streams, codecs, duração, `start_time` de áudio e vídeo | `BA_FFPROBE` |
| `ffmpeg` (9.0.2, com x264/x265) | Converter, recortar, corrigir defasagem, tirar ou trocar o áudio, normalizar volume | `BA_FFMPEG` |
| `yt-dlp` (com `deno`) | Baixar um vídeo do YouTube e de quase qualquer página (usa o ffmpeg incluído para juntar áudio e vídeo) | `BA_YTDLP` |
| `gst-launch-1.0`, `gst-discoverer-1.0`, `gst-inspect-1.0` | O mesmo com GStreamer (os mesmos módulos do grafo de vídeo) | `BA_GST_LAUNCH` |

A pasta é `BA_HERRAMIENTAS`. Se nos dados houver uma pasta `Herramientas/`, o que ela tiver vem ANTES (p. ex. um yt-dlp mais novo se um site mudou e o incluído já não funciona: só isso, e avisando o operador).

## Regras
- Nunca escreva por cima do original: o resultado vai para `Media/` (assim aparece em `media_find`) ou para onde disserem.
- Arquivos longos: rode em segundo plano e acompanhe o progresso; não trave o console por minutos sem dizer nada.
- Ao vivo, o ffmpeg disputa a CPU com a transmissão: `-threads 2` e, se a máquina estiver carregada (`system_hardware`), pergunte antes (`operator_ask`).

## Receitas
- **Olhar**: `ffprobe -v error -show_entries stream=index,codec_type,codec_name,start_time,duration -of compact "in.mp4"`.
- **Áudio atrasado ou adiantado** (defasagem fixa, medida a olho ou com uma palma/claquete): adiantar o áudio 0,3 s -> `ffmpeg -i in.mp4 -itsoffset -0.3 -i in.mp4 -map 0:v -map 1:a -c copy out.mp4` (atrasá-lo: `+0.3`). Sem recodificar, é instantâneo.
- **Defasagem que cresce** (o áudio vai se deslocando aos poucos): o relógio do áudio está errado; `-af "aresample=async=1000"` ou esticá-lo com `atempo` (duração_vídeo/duração_áudio).
- **Formato que o OBS/GStreamer não lê bem**: `ffmpeg -i in.ext -c:v libx264 -crf 18 -preset veryfast -c:a aac -b:a 192k out.mp4`.
- **Recortar** sem recodificar: `ffmpeg -ss 00:01:00 -to 00:02:30 -i in.mp4 -c copy out.mp4`.
- **Volume uniforme** (para o ar): `-af loudnorm=I=-23:TP=-1:LRA=7` (EBU R128).
- **Extrair o áudio**: `ffmpeg -i in.mp4 -vn -c:a pcm_s16le out.wav`.
- **Vídeo de uma página**: `yt-dlp -f "bv*[height<=1080]+ba/b" --merge-output-format mp4 -o "<Media>/%(title)s.%(ext)s" URL`. Só áudio: `-x --audio-format mp3`. Um trecho: `--download-sections "*1:00-2:30"`.
- **Com GStreamer**: `gst-discoverer-1.0 in.mp4` (o mesmo que o ffprobe); `gst-launch-1.0` para testar uma cadeia antes de levá-la ao grafo.

Depois: `media_find` para encontrá-lo e usá-lo numa cena.