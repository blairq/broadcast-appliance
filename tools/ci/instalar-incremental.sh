#!/usr/bin/env bash
# ============================================================================
# tools/ci/instalar-incremental.sh — `cmake --install` del bundle sin borrarlo antes
#
# Antes cada release-win/release hacía `rm -rf` y reinstalaba todo (2,7 GB en
# Windows, ~22 s en Docker sin nada que compilar). cmake --install ya copia solo
# lo que cambió («Up-to-date» para el resto) si la carpeta sigue ahí; lo único
# que faltaba es sacar lo que ya NO se instala (una regla vieja, un archivo
# renombrado). Eso sale de comparar el install_manifest de esta instalación con
# el de la anterior: lo que estaba y ya no está, se borra. Lo que nunca estuvo
# en un install_manifest (el manifest.json, el qt.conf que escribe un script de
# instalación) no se toca: lo regenera quien lo escribió.
#
# SIN_CACHE=1: de cero, como antes (lo usa `make publicar`).
#
# Uso: tools/ci/instalar-incremental.sh <build> <prefijo> [componente]
# ============================================================================
set -euo pipefail
B="${1:?uso: $0 <build> <prefijo> [componente]}"
P="${2:?uso: $0 <build> <prefijo> [componente]}"
C="${3:-bundle}"
M="$B/install_manifest_$C.txt"
ANT="$B/install_manifest_$C.anterior"
log() { echo "[instalar] $*"; }

if [ "${SIN_CACHE:-0}" != 0 ] || [ ! -d "$P" ] || [ ! -f "$M" ]; then
    [ "${SIN_CACHE:-0}" != 0 ] && log "SIN_CACHE=1: $P de cero"
    [ -d "$P" ] && [ ! -f "$M" ] && log "no hay install_manifest anterior: $P de cero"
    rm -rf "$P"
    rm -f "$ANT"
else
    cp -f "$M" "$ANT"
fi

# Las 10.000 líneas «Up-to-date» no dicen nada: se cuentan; las copias se muestran.
cmake --install "$B" --prefix "$P" --component "$C" | awk '
    /^-- Up-to-date: / { al_dia++; next }
    /^-- Installing: / { copiados++; if (copiados <= 40) print; else if (copiados == 41) print "-- (...)"; next }
    { print }
    END { printf "[instalar] %d copiados, %d al día\n", copiados, al_dia }'

if [ -f "$ANT" ]; then
    # Lo que se instalaba y ya no: fuera (solo dentro del prefijo).
    abs="$(cd "$P" && pwd)"
    borrados=0
    while IFS= read -r f; do
        case "$f" in "$abs"/* | "$P"/*) ;; *) continue ;; esac
        if [ -e "$f" ] || [ -L "$f" ]; then
            rm -f "$f"
            log "ya no se instala: ${f#"$abs"/}"
            borrados=$((borrados + 1))
        fi
    done < <(LC_ALL=C comm -23 <(LC_ALL=C sort -u "$ANT") <(LC_ALL=C sort -u "$M"))
    # Las carpetas que quedaron vacías POR lo borrado, también (si no, una regla que
    # se fue —LibreOffice: 502 carpetas— deja su árbol vacío en el bundle). Se sube
    # desde la carpeta de cada borrado mientras esté vacía, pero nunca se toca una de
    # primer nivel: el esqueleto del bundle (Config/, Logs/...) se instala vacío a
    # propósito.
    carpetas=0
    while IFS= read -r f; do
        case "$f" in "$abs"/* | "$P"/*) ;; *) continue ;; esac
        d="$(dirname "$f")"
        while [ "$(dirname "$d")" != "$abs" ] && [ "$d" != "$abs" ] && [ -d "$d" ] && [ -z "$(ls -A "$d")" ]; do
            rmdir "$d" && carpetas=$((carpetas + 1))
            d="$(dirname "$d")"
        done
    done < <(LC_ALL=C comm -23 <(LC_ALL=C sort -u "$ANT") <(LC_ALL=C sort -u "$M"))
    log "$borrados archivos que ya no se instalan, borrados ($carpetas carpetas que quedaron vacías)"
    rm -f "$ANT"
fi
