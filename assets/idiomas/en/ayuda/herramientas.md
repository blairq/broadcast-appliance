They come inside the program and are FIRST in the PATH of the console, the plugins and everything the system launches. **Don't download or install anything** (no brew, apt, pip, winget, or an ffmpeg from the internet): they're already there.

| Tool | What for | Variable |
|---|---|---|
| `ffprobe` | Look at a file: streams, codecs, duration, audio and video `start_time` | `BA_FFPROBE` |
| `ffmpeg` (9.0.2, with x264/x265) | Convert, trim, fix offset, remove or replace the audio, normalize volume | `BA_FFMPEG` |
| `yt-dlp` (with `deno`) | Download a video from YouTube and almost any page (uses the included ffmpeg to join audio and video) | `BA_YTDLP` |
| `gst-launch-1.0`, `gst-discoverer-1.0`, `gst-inspect-1.0` | The same with GStreamer (the same modules as the video graph) | `BA_GST_LAUNCH` |

The folder is `BA_HERRAMIENTAS`. If there is a `Herramientas/` folder in the data, whatever it holds goes FIRST (e.g. a newer yt-dlp if a site changed and the included one no longer works: only that, and telling the operator).

## Rules
- Never write over the original: the result goes to `Media/` (so it shows up in `media_find`) or wherever you're told.
- Long files: run in the background and watch the progress; don't lock up the console for minutes without saying anything.
- Live, ffmpeg competes with the broadcast for the CPU: `-threads 2` and, if the machine is loaded (`system_hardware`), ask first (`operator_ask`).

## Recipes
- **Look**: `ffprobe -v error -show_entries stream=index,codec_type,codec_name,start_time,duration -of compact "in.mp4"`.
- **Audio late or early** (fixed offset, measured by eye or with a clap/clapperboard): move the audio 0.3 s earlier -> `ffmpeg -i in.mp4 -itsoffset -0.3 -i in.mp4 -map 0:v -map 1:a -c copy out.mp4` (later: `+0.3`). No re-encoding, it's instant.
- **Growing offset** (the audio drifts little by little): the audio clock is off; `-af "aresample=async=1000"` or stretch it with `atempo` (video_duration/audio_duration).
- **A format OBS/GStreamer doesn't read well**: `ffmpeg -i in.ext -c:v libx264 -crf 18 -preset veryfast -c:a aac -b:a 192k out.mp4`.
- **Trim** without re-encoding: `ffmpeg -ss 00:01:00 -to 00:02:30 -i in.mp4 -c copy out.mp4`.
- **Even volume** (for air): `-af loudnorm=I=-23:TP=-1:LRA=7` (EBU R128).
- **Extract the audio**: `ffmpeg -i in.mp4 -vn -c:a pcm_s16le out.wav`.
- **Video from a page**: `yt-dlp -f "bv*[height<=1080]+ba/b" --merge-output-format mp4 -o "<Media>/%(title)s.%(ext)s" URL`. Audio only: `-x --audio-format mp3`. A section: `--download-sections "*1:00-2:30"`.
- **With GStreamer**: `gst-discoverer-1.0 in.mp4` (the same as ffprobe); `gst-launch-1.0` to test a chain before taking it to the graph.

Afterwards: `media_find` to find it and use it in a scene.
