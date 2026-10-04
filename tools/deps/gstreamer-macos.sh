#!/bin/bash
# tools/deps/gstreamer-macos.sh — GStreamer vendoreado para macOS (Apple Silicon)
#
# El GStreamer.framework OFICIAL (gstreamer.freedesktop.org), relocalizable
# desde 1.22 (usa @rpath): se desarman los .pkg SIN instalarlos (pkgutil
# --expand-full) y se arma extern/gstreamer-macos/GStreamer.framework, que el
# bundle lleva en bin/64bit (como libobs.framework). En ejecución, la app y el
# módulo de video usan SOLO esos plugins (ba::platform::prepararGst).
#
# Qué entra: TODO (núcleo, base, good, bad, ugly, libav, red, efectos,
# análisis, códecs —incluidos los GPL: la app ya es GPL por libobs; ojo con las
# patentes de los «restricted» si se distribuye—, gtk, devtools, edición y los
# .typelib de la introspección). Lo único que se saca del paquete de Python son
# sus piezas compiladas para Python 3.9 (el del sistema de Apple): el de los
# plugins es el 3.12 y sus bindings se compilan aparte (gst-python-mac.sh).
# Del paquete de desarrollo, solo las cabeceras y los .pc.
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

# La biblioteca «paraguas» del framework (Versions/1.0/GStreamer: solo reexporta
# las demás): el programa enlaza cada biblioteca por separado, y como binario
# principal del framework su firma exige sellar el framework entero como
# paquete (no verifica suelta).
rm -f "$FW/Versions/1.0/GStreamer" "$FW/Versions/1.0/lib/GStreamer" "$FW/GStreamer"
# Las piezas de Python 3.9 (site-packages y el plugin libgstpython enlazado a
# ese Python): las reemplazan las compiladas para el 3.12 (gst-python-mac.sh).
rm -rf "$FW"/Versions/1.0/lib/python3.* "$FW/Versions/1.0/lib/gstreamer-1.0/libgstpython.dylib"
# Solo arm64.
log "reduciendo a arm64"
find "$FW/Versions/1.0" -type f \( -name '*.dylib' -o -name '*.so' -o -perm -u+x \) -print0 |
    while IFS= read -r -d '' f; do
        if lipo -info "$f" 2>/dev/null | grep -q "Architectures in the fat file"; then
            lipo -thin arm64 "$f" -output "$f.arm64" 2>/dev/null && mv "$f.arm64" "$f"
        fi
    done
# Firma ad-hoc de lo que quedó sin firma (Apple Silicon no carga código sin
# firma; make app-macos vuelve a sellar la .app entera).
find "$FW/Versions/1.0" -type f \( -name '*.dylib' -o -name '*.so' -o -perm -u+x -o -name GStreamer \) -print0 |
    while IFS= read -r -d '' f; do
        file -b "$f" | grep -q Mach-O || continue
        codesign -v "$f" 2>/dev/null || codesign -f -s - "$f" 2>/dev/null || true
    done
log "listo: $FW ($(du -sh "$FW" | cut -f1), $(ls "$FW/Versions/1.0/lib/gstreamer-1.0" | wc -l | tr -d ' ') plugins)"
