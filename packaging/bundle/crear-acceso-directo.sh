#!/usr/bin/env bash
# ============================================================================
# crear-acceso-directo.sh — pone Broadcast Appliance en el menú de esta máquina
#
# El programa es portable: vive en un disco que se enchufa en distintas
# computadoras, así que el acceso directo no puede venir hecho (la ruta cambia
# en cada una). Este script lo arma con la ruta donde está parado.
#
#   ./crear-acceso-directo.sh          # lo crea para el usuario actual
#   ./crear-acceso-directo.sh --quitar # lo saca
#
# No necesita permisos de administrador: escribe en la carpeta del usuario.
# Cuando se desenchufa el disco, el acceso queda pero no arranca; volver a
# correrlo desde el disco nuevo lo deja apuntando bien.
# ============================================================================
set -euo pipefail

RAIZ="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APLICACIONES="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
ARCHIVO="$APLICACIONES/broadcast-appliance.desktop"
EJECUTABLE="$RAIZ/BroadcastAppliance"
ICONO="$RAIZ/Recursos/broadcast-appliance.png"

if [ "${1:-}" = "--quitar" ]; then
  if [ -f "$ARCHIVO" ]; then
    rm -f "$ARCHIVO"
    echo "Acceso directo quitado."
  else
    echo "No había acceso directo que quitar."
  fi
  command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$APLICACIONES" || true
  exit 0
fi

if [ ! -x "$EJECUTABLE" ]; then
  echo "No encuentro el programa en $EJECUTABLE." >&2
  echo "Corré este script desde la carpeta donde está BroadcastAppliance." >&2
  exit 1
fi

mkdir -p "$APLICACIONES"
cat > "$ARCHIVO" <<EOF
[Desktop Entry]
Type=Application
Version=1.0
Name=Broadcast Appliance
GenericName=Consola de transmisión
Comment=Transmitir, grabar y atender la sala de invitados
Exec=$EJECUTABLE
Path=$RAIZ
Icon=$ICONO
Terminal=false
Categories=AudioVideo;Video;
Keywords=transmision;streaming;obs;grabacion;conferencia;
StartupNotify=true
EOF
chmod +x "$ARCHIVO"

command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$APLICACIONES" || true

echo "Listo: «Broadcast Appliance» ya está en el menú de aplicaciones."
echo "Apunta a $EJECUTABLE"
