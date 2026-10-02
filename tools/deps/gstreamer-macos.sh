#!/bin/bash
# tools/deps/gstreamer-macos.sh — GStreamer vendoreado para macOS (Apple Silicon)
#
# El GStreamer.framework OFICIAL (gstreamer.freedesktop.org), relocalizable
# desde 1.22 (usa @rpath): se desarman los .pkg SIN instalarlos (pkgutil
# --expand-full) y se arma extern/gstreamer-macos/GStreamer.framework, que el
# bundle lleva en bin/64bit (como libobs.framework). En ejecución, la app y el
# módulo de video usan SOLO esos plugins (ba::platform::prepararGst).
#
# Qué entra: el núcleo, base, good, bad, ugly, libav, red, efectos, análisis,
# códecs (incluidos los GPL: la app ya es GPL por libobs; ojo con las patentes
# de los «restricted» si se distribuye). Qué NO: python, gtk y devtools (la app
# no los usa). Del paquete de desarrollo, solo las cabeceras y los .pc.
# Todo se reduce a arm64 (el bundle es de Apple Silicon): la mitad de tamaño.
#
# Uso: tools/deps/gstreamer-macos.sh <runtime.pkg> <devel.pkg> <salida>
set -euo pipefail
RT="$1"
DEV="$2"
OUT="$3"
log() { printf '\033[1;36m[gstreamer]\033[0m %s\n' "$*"; }

T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
log "desarmando $(basename "$RT")"
pkgutil --expand-full "$RT" "$T/rt"
log "desarmando $(basename "$DEV")"
pkgutil --expand-full "$DEV" "$T/dev"

FW="$OUT/GStreamer.framework"
rm -rf "$FW"
mkdir -p "$FW/Versions/1.0"
# El esqueleto del framework (symlinks Versions/Current, GStreamer, Headers…).
ditto "$T/rt"/osx-framework-*.pkg/Payload "$FW"
for p in "$T/rt"/*.pkg; do
    n="$(basename "$p")"
    case "$n" in
    osx-framework-*) continue ;;
    gstreamer-1.0-python-* | gstreamer-1.0-gtk-* | gstreamer-1.0-devtools-*) log "afuera: $n"; continue ;;
    esac
    [ -d "$p/Payload" ] && ditto "$p/Payload" "$FW/Versions/1.0"
done
# Del paquete de desarrollo: cabeceras y pkg-config (no las bibliotecas estáticas).
for p in "$T/dev"/*.pkg; do
    [ -d "$p/Payload" ] || continue
    [ -d "$p/Payload/include" ] && ditto "$p/Payload/include" "$FW/Versions/1.0/include"
    [ -d "$p/Payload/Headers" ] && ditto "$p/Payload/Headers" "$FW/Versions/1.0/Headers"
    [ -d "$p/Payload/lib/pkgconfig" ] && ditto "$p/Payload/lib/pkgconfig" "$FW/Versions/1.0/lib/pkgconfig"
done
[ -e "$FW/Versions/1.0/Headers" ] || ln -s include/gstreamer-1.0 "$FW/Versions/1.0/Headers"

# Los de edición (ges, nle) dependen de gst-validate (devtools, que no entra), y
# la biblioteca «paraguas» del framework (GStreamer) también: se enlaza cada
# biblioteca por separado, así que no hace falta.
rm -f "$FW/Versions/1.0/lib/gstreamer-1.0/libgstges.dylib" "$FW/Versions/1.0/lib/gstreamer-1.0/libgstnle.dylib" \
    "$FW/Versions/1.0/lib/libges-1.0.0.dylib" "$FW/Versions/1.0/lib/libges-1.0.dylib" "$FW/Versions/1.0/bin/ges-launch-1.0" \
    "$FW/Versions/1.0/GStreamer" "$FW/Versions/1.0/lib/GStreamer" "$FW/GStreamer"
# Solo arm64.
log "reduciendo a arm64"
find "$FW/Versions/1.0" -type f \( -name '*.dylib' -o -name '*.so' -o -perm -u+x \) -print0 |
    while IFS= read -r -d '' f; do
        if lipo -info "$f" 2>/dev/null | grep -q "Architectures in the fat file"; then
            lipo -thin arm64 "$f" -output "$f.arm64" 2>/dev/null && mv "$f.arm64" "$f"
        fi
    done
log "listo: $FW ($(du -sh "$FW" | cut -f1), $(ls "$FW/Versions/1.0/lib/gstreamer-1.0" | wc -l | tr -d ' ') plugins)"
