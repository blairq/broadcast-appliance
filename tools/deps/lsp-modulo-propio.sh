#!/usr/bin/env bash
# ============================================================================
# tools/deps/lsp-modulo-propio.sh — suma nuestro módulo LSP al árbol de LSP
#
# third_party/lsp-plugins-matrix-mixer es un módulo de LSP escrito con sus
# mismas convenciones (para poder proponérselo al proyecto). Para compilarlo
# junto con el resto, este script lo deja en el árbol de la fuente oficial de
# LSP ANTES de `make config`, igual que estaría si fuera uno de los suyos:
#
#   - copia el módulo a modules/lsp-plugins-matrix-mixer;
#   - lo declara en modules.mk y lo agrega a plugins.mk;
#   - en lsp-plugins-shared: la base de sus identificadores LADSPA y su
#     desarrollador (developers.h/.cpp).
#
# Es idempotente. Lo llaman tools/deps/lsp-macos.sh y tools/deps/lsp-win.sh.
#
# Uso: tools/deps/lsp-modulo-propio.sh <raíz de la fuente de LSP>
#      tools/deps/lsp-modulo-propio.sh --huella   (hash del módulo: si cambia,
#                                                  hay que recompilar LSP)
# ============================================================================
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
MOD="$ROOT/third_party/lsp-plugins-matrix-mixer"
if [ "${1:-}" = --huella ]; then
    python3 - "$MOD" "$0" <<'PYEOF'
import hashlib, os, sys
h = hashlib.sha256()
for raiz in (sys.argv[1],):
    for d, _, fs in sorted(os.walk(raiz)):
        for f in sorted(fs):
            p = os.path.join(d, f)
            h.update(os.path.relpath(p, raiz).encode()); h.update(open(p, "rb").read())
h.update(open(sys.argv[2], "rb").read())
print(h.hexdigest()[:12])
PYEOF
    exit 0
fi
LSP="${1:?uso: $0 <raiz de la fuente de LSP>}"
log() { echo "[lsp-modulo-propio] $*" >&2; }

[ -f "$LSP/modules.mk" ] && [ -f "$LSP/plugins.mk" ] || { log "no es la raiz de la fuente de LSP: $LSP"; exit 1; }
[ -d "$MOD" ] || { log "falta $MOD"; exit 1; }

# El módulo
rm -rf "$LSP/modules/lsp-plugins-matrix-mixer"
mkdir -p "$LSP/modules"
cp -R "$MOD" "$LSP/modules/lsp-plugins-matrix-mixer"
VER="$(awk -F'= *' '/^ARTIFACT_VERSION/{print $2; exit}' "$MOD/project.mk")"

# modules.mk y plugins.mk: en orden alfabético (después del Matcher) y con la
# misma alineación que los módulos oficiales, tal cual lo propone el parche de
# docs/lsp-matrix-mixer/.
python3 - "$LSP" "$VER" <<'PYEOF2'
import sys
lsp, ver = sys.argv[1], sys.argv[2]
p = f"{lsp}/modules.mk"
s = open(p).read()
if "LSP_PLUGINS_MATRIX_MIXER_NAME" not in s:
    campos = [("VERSION", ver), ("NAME", "lsp-plugins-matrix-mixer"), ("TYPE", "plug"),
              ("URL_RO", "https://github.com/lsp-plugins/$(LSP_PLUGINS_MATRIX_MIXER_NAME).git"),
              ("URL_RW", "git@github.com:lsp-plugins/$(LSP_PLUGINS_MATRIX_MIXER_NAME).git")]
    bloque = "".join(f"{('LSP_PLUGINS_MATRIX_MIXER_' + k).ljust(40)}:= {v}\n" for k, v in campos) + "\n"
    i = s.index("LSP_PLUGINS_MB_CLIPPER_VERSION")
    s = s[:i] + bloque + s[i:]
    open(p, "w").write(s)
p = f"{lsp}/plugins.mk"
s = open(p).read()
if "LSP_PLUGINS_MATRIX_MIXER" not in s:
    ancla = "  LSP_PLUGINS_MATCHER \\\n"
    if ancla not in s:
        sys.exit("[lsp-modulo-propio] no pude sumarlo a plugins.mk")
    s = s.replace(ancla, ancla + "  LSP_PLUGINS_MATRIX_MIXER \\\n", 1)
    open(p, "w").write(s)
PYEOF2

# lsp-plugins-shared: identificadores LADSPA y desarrollador
SH="$LSP/modules/lsp-plugins-shared"
H="$SH/include/lsp-plug.in/shared/meta/developers.h"
C="$(find "$SH/src" -name developers.cpp | head -1)"
[ -f "$H" ] && [ -f "$C" ] || { log "no encuentro developers.h/.cpp en lsp-plugins-shared"; exit 1; }
if ! grep -q "LSP_LADSPA_MATRIX_MIXER_BASE" "$H"; then
    sed -i.orig 's/^\(#define LSP_LADSPA_DEESSER_BASE .*\)$/\1\
#define LSP_LADSPA_MATRIX_MIXER_BASE        (LSP_LADSPA_BASE + 300)/' "$H"
    sed -i.orig 's/^\(        extern const meta::person_t         s_tronci;\)$/\1\
        extern const meta::person_t         j_ortega;/' "$H"
    rm -f "$H.orig"
    grep -q "LSP_LADSPA_MATRIX_MIXER_BASE" "$H" && grep -q "j_ortega" "$H" ||
        { log "no pude parchar developers.h"; exit 1; }
fi
if ! grep -q "j_ortega" "$C"; then
    python3 - "$C" <<'PYEOF'
import sys
p = sys.argv[1]
s = open(p).read()
marca = "        const meta::person_t s_tronci ="
i = s.index(marca)
j = s.index("};", i) + 2
nuevo = '''

        const meta::person_t j_ortega =
        {
            "j_ortega",
            "BlairQ",
            "Jorge Ortega",
            "ortegajorge@gmail.com",
            "https://github.com/blairq"
        };'''
open(p, "w").write(s[:j] + nuevo + s[j:])
PYEOF
fi
log "lsp-plugins-matrix-mixer $VER en el arbol de LSP"
