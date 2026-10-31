Ils sont fournis avec le programme et sont EN TÊTE du PATH de la console, des plugins et de tout ce que lance le système. **Ne téléchargez et n'installez rien** (ni brew, apt, pip, winget, ni un ffmpeg d'internet) : ils sont déjà là.

| Outil | Pour quoi faire | Variable |
|---|---|---|
| `ffprobe` | Examiner un fichier : flux, codecs, durée, `start_time` de l'audio et de la vidéo | `BA_FFPROBE` |
| `ffmpeg` (9.0.2, avec x264/x265) | Convertir, couper, corriger un décalage, retirer ou changer l'audio, normaliser le volume | `BA_FFMPEG` |
| `yt-dlp` (avec `deno`) | Télécharger une vidéo de YouTube et de presque n'importe quelle page (utilise le ffmpeg inclus pour joindre audio et vidéo) | `BA_YTDLP` |
| `gst-launch-1.0`, `gst-discoverer-1.0`, `gst-inspect-1.0` | La même chose avec GStreamer (les mêmes modules que le graphe vidéo) | `BA_GST_LAUNCH` |

Le dossier est `BA_HERRAMIENTAS`. S'il y a un dossier `Herramientas/` dans les données, ce qu'il contient passe AVANT (par ex. un yt-dlp plus récent si un site a changé et que celui inclus ne marche plus : seulement cela, et en prévenant l'opérateur).

## Règles
- N'écrivez jamais par-dessus l'original : le résultat va dans `Media/` (ainsi il apparaît dans `media_find`) ou là où on vous le dit.
- Fichiers longs : lancez en arrière-plan et suivez la progression ; ne bloquez pas la console pendant des minutes sans rien dire.
- En direct, ffmpeg dispute le CPU à la diffusion : `-threads 2` et, si la machine est chargée (`system_hardware`), demandez avant (`operator_ask`).

## Recettes
- **Examiner** : `ffprobe -v error -show_entries stream=index,codec_type,codec_name,start_time,duration -of compact "in.mp4"`.
- **Audio en retard ou en avance** (décalage fixe, mesuré à l'œil ou avec un applaudissement/clap) : avancer l'audio de 0,3 s -> `ffmpeg -i in.mp4 -itsoffset -0.3 -i in.mp4 -map 0:v -map 1:a -c copy out.mp4` (le retarder : `+0.3`). Sans réencodage, c'est instantané.
- **Décalage qui grandit** (l'audio dérive petit à petit) : l'horloge audio est fausse ; `-af "aresample=async=1000"` ou l'étirer avec `atempo` (duree_video/duree_audio).
- **Format qu'OBS/GStreamer lit mal** : `ffmpeg -i in.ext -c:v libx264 -crf 18 -preset veryfast -c:a aac -b:a 192k out.mp4`.
- **Couper** sans réencoder : `ffmpeg -ss 00:01:00 -to 00:02:30 -i in.mp4 -c copy out.mp4`.
- **Volume homogène** (pour l'antenne) : `-af loudnorm=I=-23:TP=-1:LRA=7` (EBU R128).
- **Extraire l'audio** : `ffmpeg -i in.mp4 -vn -c:a pcm_s16le out.wav`.
- **Vidéo d'une page** : `yt-dlp -f "bv*[height<=1080]+ba/b" --merge-output-format mp4 -o "<Media>/%(title)s.%(ext)s" URL`. Audio seul : `-x --audio-format mp3`. Un extrait : `--download-sections "*1:00-2:30"`.
- **Avec GStreamer** : `gst-discoverer-1.0 in.mp4` (la même chose que ffprobe) ; `gst-launch-1.0` pour tester une chaîne avant de la mettre dans le graphe.

Ensuite : `media_find` pour le retrouver et l'utiliser dans une scène.
