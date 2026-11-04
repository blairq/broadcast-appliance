#!/bin/sh
# tools/deps/patchelf-misma-fecha.sh — patchelf sobre un archivo del bundle sin cambiarle la fecha
#
# Lo usan los install(CODE) que fijan el RPATH de lo instalado (cmake/BaPortableBundle.cmake,
# src/pinggy, src/modelo-llama). cmake --install decide si copia un archivo por su FECHA: si el
# instalado no tiene la del original, lo vuelve a copiar. patchelf la cambiaba, así que cada
# instalación incremental volvía a copiar y a parchear esas bibliotecas, y el bundle «cambiaba»
# siempre (strip, chequeo, manifest y .tar.gz de nuevo). El resultado de patchelf es siempre el
# mismo para la misma entrada: con la fecha de cuando se instaló, la próxima ve todo al día.
#
# Uso: patchelf-misma-fecha.sh <patchelf> <argumentos...> <archivo>   (el archivo, al final)
set -u
pe="$1"; shift
for f in "$@"; do :; done   # el último argumento
[ -f "$f" ] || exec "$pe" "$@"
ref="$f.fecha-$$"
touch -r "$f" "$ref"
"$pe" "$@"; rc=$?
touch -r "$ref" "$f"
rm -f "$ref"
exit $rc
