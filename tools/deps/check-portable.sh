#!/usr/bin/env bash
# ============================================================================
# tools/deps/check-portable.sh <bundle> — ¿el bundle corre en otra maquina?
#
# Recorre cada ELF del bundle (ejecutables, .so, plugins) y cada biblioteca que
# pide (DT_NEEDED). Cada una tiene que estar DENTRO del bundle o en la base del
# sistema (tools/deps/linux-baseline.txt). Informa ademas la version minima de
# glibc que exige el bundle (depende de donde se compilo).
#
# Sale con 1 si algo depende de una biblioteca de terceros del sistema.
# ============================================================================
set -euo pipefail

BUNDLE="${1:?uso: check-portable.sh <carpeta del bundle>}"
HERE="$(cd "$(dirname "$0")" && pwd)"
BASELINE="$HERE/linux-baseline.txt"

declare -A allowed=()
while read -r l; do
  l="${l%%#*}"; l="${l// /}"
  [ -n "$l" ] && allowed["$l"]=1
done <"$BASELINE"

# Como el cargador dinamico: una dependencia del bundle tiene que estar en un
# directorio del RUNPATH/RPATH del PROPIO archivo ($ORIGIN = su carpeta). El
# RUNPATH no se hereda: un plugin de GStreamer busca FFmpeg con el suyo, no con el de ba-app.
# (LD_LIBRARY_PATH no cuenta: el launcher lo borra.)
resolvable() { # <elf> <soname>
  local elf="$1" need="$2" dir rp d
  dir="$(cd "$(dirname "$elf")" && pwd)"
  # Un NEEDED con ruta ($ORIGIN/../lib/libpython3.12.so.1.0): esa ruta, tal cual.
  case "$need" in
  */*) d="${need//\$ORIGIN/$dir}"; d="${d//\$\{ORIGIN\}/$dir}"; [ -e "$d" ] && return 0; return 1 ;;
  esac
  rp=$(readelf -d "$elf" 2>/dev/null | sed -n 's/.*(\(RUNPATH\|RPATH\)).*\[\(.*\)\]/\2/p' | head -1)
  IFS=':' read -ra dirs <<<"$rp"
  for d in "${dirs[@]}"; do
    d="${d//\$ORIGIN/$dir}"
    d="${d//\$\{ORIGIN\}/$dir}"
    [ -e "$d/$need" ] && return 0
  done
  return 1
}

bad=0
glibc_max="2.0"
checked=0
while IFS= read -r f; do
  file -b "$f" | grep -q '^ELF' || continue
  checked=$((checked + 1))
  while read -r need; do
    [ -z "$need" ] && continue
    [ -n "${allowed[$need]:-}" ] && continue
    if ! resolvable "$f" "$need"; then
      echo "NO PORTABLE: ${f#$BUNDLE/} necesita $need y no la encuentra en el bundle (su RUNPATH) ni es de la base del sistema"
      bad=1
    fi
  done < <(readelf -d "$f" 2>/dev/null | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p')
  v=$(objdump -T "$f" 2>/dev/null | grep -o 'GLIBC_[0-9.]*' | sed 's/GLIBC_//' | sort -V | tail -1 || true)
  if [ -n "$v" ] && [ "$(printf '%s\n%s\n' "$glibc_max" "$v" | sort -V | tail -1)" = "$v" ]; then glibc_max="$v"; fi
done < <(find "$BUNDLE" \( -path "$BUNDLE/Config" -o -path "$BUNDLE/Credenciales" -o -path "$BUNDLE/Agentes" \
                          -o -path "$BUNDLE/Cache" -o -path "$BUNDLE/Runtime" -o -path "$BUNDLE/Logs" \
                          -o -path "$BUNDLE/Recordings" -o -path "$BUNDLE/Shares" -o -path "$BUNDLE/Media" \
                          -o -path "$BUNDLE/Respaldos" -o -path "$BUNDLE/.actualizacion-*" \) -prune -o \
           -type f \( -perm -u+x -o -name '*.so' -o -name '*.so.*' -o -name '*.clap' \) -print)
# (Las carpetas de datos no son el programa: ahi hay agentes y plugins que bajo
# la persona, con sus propias dependencias.)

echo "ELF revisados: $checked; glibc minima: $glibc_max"
if [ "$bad" -ne 0 ]; then
  echo "El bundle depende de bibliotecas del sistema que no son de la base (ver arriba)."
  exit 1
fi
echo "OK: el bundle solo usa la base del sistema (tools/deps/linux-baseline.txt)."
