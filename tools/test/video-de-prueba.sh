#!/usr/bin/env bash
# ============================================================================
# video-de-prueba.sh — prepara un video real para los invitados emulados
#
# POR QUÉ. Chrome, con `--use-fake-device-for-media-stream`, manda un patrón
# sintético: casi todo plano, que el codificador resuelve con cuatro bits. Con
# eso la prueba de carga miente en lo que más importa: el ancho de banda es
# ridículamente bajo y el codificador del invitado no trabaja. Con un video de
# verdad —una persona hablando frente a la cámara— los números se parecen a un
# evento.
#
# QUÉ BAJA. Una secuencia de la colección de prueba de Xiph (media.xiph.org),
# que se publica justamente para probar códecs y sistemas de video. La que se
# usa es de videoconferencia: una persona sentada frente a la cámara.
#
# El .y4m que queda pesa ~100 MB (es video CRUDO: es lo único que Chrome sabe
# leer como cámara falsa), así que NO va al repositorio: se genera acá cuando
# hace falta.
#
#   tools/test/video-de-prueba.sh              # lo deja listo
#   tools/test/video-de-prueba.sh --seg 20     # más largo (pesa el doble)
#   tools/test/video-de-prueba.sh --alto 720   # 1280x720 en vez de 640x360
#
# Después:
#   tools/test/carga-sala.py --invitados 8 --video <la ruta que imprime>
# ============================================================================
set -euo pipefail

FUENTE="https://media.xiph.org/video/derf/webm/Johnny_1280x720_60.webm"
DESTINO="${BA_VIDEO_DIR:-${TMPDIR:-/tmp}/ba-video-prueba}"
SEGUNDOS=10
ALTO=360

while [ $# -gt 0 ]; do
  case "$1" in
    --seg)   SEGUNDOS="$2"; shift 2 ;;
    --alto)  ALTO="$2"; shift 2 ;;
    --dir)   DESTINO="$2"; shift 2 ;;
    -h|--help) sed -n '2,28p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "argumento no reconocido: $1" >&2; exit 2 ;;
  esac
done

command -v ffmpeg >/dev/null || { echo "Falta ffmpeg." >&2; exit 1; }

mkdir -p "$DESTINO"
ORIGEN="$DESTINO/origen.webm"
ANCHO=$(( ALTO * 16 / 9 ))
# El ancho tiene que ser par (yuv420 comparte cromas de a dos píxeles).
ANCHO=$(( ANCHO - ANCHO % 2 ))
SALIDA="$DESTINO/camara-${ANCHO}x${ALTO}-${SEGUNDOS}s.y4m"

if [ -s "$SALIDA" ]; then
  echo "$SALIDA"
  exit 0
fi

if [ ! -s "$ORIGEN" ]; then
  echo "Bajando la secuencia de prueba (una vez)…" >&2
  curl -fsSL -o "$ORIGEN.parcial" "$FUENTE"
  mv "$ORIGEN.parcial" "$ORIGEN"
fi

echo "Convirtiendo a ${ANCHO}x${ALTO}, ${SEGUNDOS}s, 30 fps (video crudo, va a pesar)…" >&2
ffmpeg -v error -y -i "$ORIGEN" -t "$SEGUNDOS" \
  -vf "scale=${ANCHO}:${ALTO},fps=30" -pix_fmt yuv420p "$SALIDA"

# Un audio de acompañamiento: Chrome no puede leer el video y el audio del
# mismo archivo, y su tono por defecto es un pitido constante que el códec de
# voz resuelve con nada. Esto varía en el tiempo, que es lo que hace trabajar
# a Opus de forma parecida a una voz.
AUDIO="$DESTINO/voz.wav"
if [ ! -s "$AUDIO" ]; then
  ffmpeg -v error -y \
    -f lavfi -i "sine=frequency=180:duration=$SEGUNDOS" \
    -f lavfi -i "sine=frequency=1100:duration=$SEGUNDOS" \
    -filter_complex "[0][1]amix=inputs=2,tremolo=f=3.5:d=0.9,aformat=sample_rates=48000:channel_layouts=mono" \
    "$AUDIO"
fi

echo "$SALIDA"
