#!/usr/bin/env bash
# ============================================================================
# tools/ci/capa-docker.sh — ADENTRO del contenedor de tools/ci/compilar-en-docker.sh:
# arma /trabajo/ba (la base o una capa encima), trae el commit y corre la receta
#
# En el volumen ba-trabajo:
#   /trabajo/base             el árbol de master YA COMPILADO (Linux en build/, Windows en
#                             build/win, extern/ entero). Solo lo escribe `make docker-base`.
#   /trabajo/base/.base-id    de qué commit es (cambia en cada `make docker-base`).
#   /trabajo/capas/<capa>/    una por worktree y plataforma: upper/ (lo que la rama cambió:
#                             fuentes, objetos recompilados, ejecutables) y work/ (de overlayfs).
#   /trabajo/ba               el punto de montaje (cada contenedor monta lo suyo: dos a la
#                             vez no se ven entre sí). Siempre la MISMA ruta: los caminos que
#                             guardan CMake y ninja, y las claves de ccache (ba-ccache), son
#                             los mismos en la base y en todas las capas.
#
# Una capa es overlayfs: la base abajo, de solo lectura, y lo que escribe la corrida arriba.
# Copia solo lo que se sobreescribe: `git checkout` del commit de la rama cambia los archivos
# que difieren de master (con fecha nueva) y ninja recompila exactamente eso. La capa queda
# entre corridas (la segunda vez de la misma rama, solo lo nuevo). Si la base cambió desde que
# se armó la capa, la capa se tira (sus objetos eran contra la base vieja).
#
# Variables: MODO (base|capa), CAPA, SHA y las de la receta (tools/ci/receta-docker.sh).
# ============================================================================
set -euo pipefail
export T0=$(date +%s)
git config --global --add safe.directory "*"
mkdir -p /trabajo/capas

# Una sola vez: el volumen viejo tenía un único clon en /trabajo/ba (con el build del
# último commit que se compiló): pasa a ser la base, sin copiar nada (mismo disco).
if [ ! -d /trabajo/base ] && [ -d /trabajo/ba/.git ] && ! mountpoint -q /trabajo/ba; then
    echo "[capa] el clon de /trabajo/ba pasa a ser la base (/trabajo/base)"
    mv /trabajo/ba /trabajo/base
    echo "legado-$(git -C /trabajo/base rev-parse HEAD)-$(date +%s)" >/trabajo/base/.base-id
fi
if [ ! -d /trabajo/base/.git ]; then
    echo "[capa] no hay base: un clon vacío (la primera corrida compila todo; después, make docker-base)"
    rm -rf /trabajo/base
    git clone -q /repo.git /trabajo/base
    echo "vacia-$(date +%s)" >/trabajo/base/.base-id
fi
mkdir -p /trabajo/ba
BASE_ID="$(cat /trabajo/base/.base-id 2>/dev/null || echo sin-id)"

if [ "$MODO" = base ]; then
    # La base se rehace en su lugar: las capas eran contra la vieja (compilar-en-docker.sh
    # esperó a que no corra ninguna).
    echo "[capa] base: se tiran las capas ($(ls /trabajo/capas | wc -l)) y se compila $SHA en /trabajo/base"
    find /trabajo/capas -mindepth 1 -maxdepth 1 -exec rm -rf {} +
    mount --bind /trabajo/base /trabajo/ba
else
    C="/trabajo/capas/$CAPA"
    if [ -d "$C" ] && [ "$(cat "$C/base-id" 2>/dev/null)" != "$BASE_ID" ]; then
        echo "[capa] $CAPA: era contra otra base ($(cat "$C/base-id" 2>/dev/null || echo ?)): se tira"
        rm -rf "$C"
    fi
    if [ -d "$C" ]; then
        echo "[capa] $CAPA: sigue ($(du -sh "$C/upper" 2>/dev/null | cut -f1) sobre la base $BASE_ID)"
    else
        echo "[capa] $CAPA: nueva sobre la base $BASE_ID"
        mkdir -p "$C/upper" "$C/work"
        echo "$BASE_ID" >"$C/base-id"
    fi
    date +%s >"$C/usada"
    mount -t overlay overlay -o "lowerdir=/trabajo/base,upperdir=$C/upper,workdir=$C/work" /trabajo/ba
fi

ccache -z >/dev/null 2>&1 || true
cd /trabajo/ba
git fetch -q /repo.git "$SHA"
git checkout -q -f "$SHA"
etapa_rc=0
bash tools/ci/receta-docker.sh || etapa_rc=$?
cd /
if [ "$MODO" = base ]; then
    # 3 = solo fallaron tests bajo Wine (el .zip se armó): la base sirve igual.
    if [ "$etapa_rc" = 0 ] || [ "$etapa_rc" = 3 ]; then
        echo "$SHA-$(date +%s)" >/trabajo/base/.base-id
        echo "[capa] base lista: $(cat /trabajo/base/.base-id)"
    else
        echo "[capa] la base no terminó bien (rc=$etapa_rc): queda marcada vieja; las capas se rearman" >&2
        echo "rota-$(date +%s)" >/trabajo/base/.base-id
    fi
fi
umount /trabajo/ba 2>/dev/null || true
exit "$etapa_rc"
