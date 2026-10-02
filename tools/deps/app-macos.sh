#!/usr/bin/env bash
# ============================================================================
# tools/deps/app-macos.sh — el bundle portable de macOS como .app (y el .dmg)
#
# La .app ES el bundle portable: el layout de siempre (bin/64bit, obs-plugins,
# data, Config, Recordings...) va entero en Contents/Resources/, y el launcher
# en Contents/MacOS/. El programa lo reconoce: si corre desde
# X.app/Contents/MacOS/, la raiz del layout es Contents/Resources (ver
# bundleRootFor en src/core/platform/Paths.cpp). En Resources y no directo en
# Contents/: ahi solo pueden ir las carpetas estandar de una .app, y cualquier
# otra cosa (bin/, LICENSE...) hace que codesign la rechace. Nada se instala: la .app se lleva en un disco
# igual que la carpeta de Linux o Windows.
#
# Firma AD-HOC: no hay cuenta de Apple Developer (y no la va a haber), asi que
# no se notariza. La primera vez que se abre en otra Mac hay que hacerlo con
# clic derecho -> Abrir (lo explica el "Leeme primero" del .dmg).
#
# Uso: tools/deps/app-macos.sh <release/> <salida.app> [<salida.dmg>]
# ============================================================================
set -euo pipefail

REL="$(cd "${1:?uso: $0 <release> <salida.app> [salida.dmg]}" && pwd)"
APP="${2:?falta la .app de salida}"
DMG="${3:-}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
log() { echo "[app-macos] $*" >&2; }
morir() { echo "[app-macos] ERROR: $*" >&2; exit 1; }

[ "$(uname -s)" = Darwin ] || morir "esto se corre en macOS"
[ -x "$REL/BroadcastAppliance" ] || morir "$REL no parece un bundle (falta BroadcastAppliance): make release-macos"
VER="$(awk '/^project\(/,/\)/' "$ROOT/CMakeLists.txt" | awk '/VERSION/{print $2; exit}')"

# --- La .app -------------------------------------------------------------------
rm -rf "$APP"
C="$APP/Contents"
R="$C/Resources" # la raiz del layout portable
mkdir -p "$C/MacOS"
log "copiando el bundle a $(basename "$APP")/Contents/Resources"
# ditto conserva enlaces, permisos y firmas (frameworks, la .app de LibreOffice).
ditto "$REL" "$R"
mv "$R/BroadcastAppliance" "$C/MacOS/BroadcastAppliance"
# El manifest de release/ se rehace abajo con el layout de la .app.
rm -f "$R/manifest.json"
cp "$ROOT/assets/icons/broadcast-appliance.icns" "$R/broadcast-appliance.icns"

cat >"$C/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>BroadcastAppliance</string>
    <key>CFBundleIdentifier</key>
    <string>com.legisstudio.broadcastappliance</string>
    <key>CFBundleName</key>
    <string>Broadcast Appliance</string>
    <key>CFBundleDisplayName</key>
    <string>Broadcast Appliance</string>
    <key>CFBundleIconFile</key>
    <string>broadcast-appliance</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>$VER</string>
    <key>CFBundleVersion</key>
    <string>$VER</string>
    <key>LSMinimumSystemVersion</key>
    <string>13.3</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <!-- El launcher no tiene ventana: el que aparece en el Dock (con el logo)
         es la interfaz, ba-app, que es otro proceso. -->
    <key>LSUIElement</key>
    <true/>
    <key>NSCameraUsageDescription</key>
    <string>Broadcast Appliance usa la cámara como fuente de video de la emisión.</string>
    <key>NSMicrophoneUsageDescription</key>
    <string>Broadcast Appliance usa el micrófono como fuente de audio de la emisión.</string>
    <key>NSLocalNetworkUsageDescription</key>
    <string>Broadcast Appliance recibe a los invitados y fuentes NDI por la red local, y busca los televisores (Chromecast).</string>
    <key>NSBonjourServices</key>
    <array>
        <string>_ndi._tcp</string>
        <string>_googlecast._tcp</string>
    </array>
</dict>
</plist>
PLIST
printf 'APPL????' >"$C/PkgInfo"

# --- Manifest de integridad y firma --------------------------------------------
# Todo lo de adentro ya viene firmado (make release-macos). El manifest (de
# Resources, la raiz del layout) se arma ANTES de sellar la .app: el sello la
# cubre entera. El launcher (Contents/MacOS) queda fuera del manifest, como en
# Linux y Windows, donde tampoco se verifica a si mismo.
"$ROOT/tools/deploy" --solo-manifest "$R" >/dev/null
# Con el certificado propio (tools/firma-local.sh) la identidad de la app no
# cambia entre compilaciones y macOS recuerda los permisos (cámara,
# micrófono); solo si firma SIN preguntar. Si no, ad-hoc.
FIRMA="-"
if security find-identity -p codesigning 2>/dev/null | grep -q '"BA Desarrollo local"'; then
    PRUEBA="$(mktemp)"
    cp /usr/bin/true "$PRUEBA"
    if timeout 15 codesign -f -s "BA Desarrollo local" "$PRUEBA" 2>/dev/null; then FIRMA="BA Desarrollo local"; fi
    rm -f "$PRUEBA"
fi
log "firmando la .app ($([ "$FIRMA" = "-" ] && echo ad-hoc || echo "$FIRMA"))"
codesign --force -s "$FIRMA" "$APP" || morir "no se pudo firmar la .app"
codesign --verify --strict "$APP" || morir "la firma de la .app no verifica"
log "listo: $APP ($(du -sh "$APP" | cut -f1))"

# --- El .dmg -------------------------------------------------------------------
[ -n "$DMG" ] || exit 0
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
ditto "$APP" "$STAGE/$(basename "$APP")"
ln -s /Applications "$STAGE/Aplicaciones"
cat >"$STAGE/Leeme primero.txt" <<'TXT'
BROADCAST APPLIANCE PARA macOS (Apple Silicon, macOS 13.3 o más nuevo)
======================================================================

1. Arrastrá "BroadcastAppliance" a Aplicaciones (o a una carpeta de tu disco
   externo: es portable, todo lo que usa y lo que graba queda adentro).
   No lo abras desde este disco (.dmg): ahí es de solo lectura.

2. La PRIMERA vez: clic derecho sobre BroadcastAppliance -> Abrir -> Abrir.
   El programa no está firmado con un certificado de Apple, así que un doble
   clic muestra "no se puede verificar el desarrollador". Después de abrirlo
   así una vez, se abre normalmente.
   (Alternativa por terminal:  xattr -dr com.apple.quarantine /Applications/BroadcastAppliance.app)

3. macOS pide permiso para la cámara, el micrófono y la red local: aceptalos.
   Para capturar la pantalla hay que darlo en Ajustes -> Privacidad y
   seguridad -> Grabación de pantalla, y volver a abrir el programa.

Las grabaciones y la configuración quedan dentro de la app (clic derecho ->
Mostrar contenido del paquete -> Contents -> Resources -> Recordings). El
manual completo está en Contents/Resources/LEEME.txt.
TXT
rm -f "$DMG"
log "armando $(basename "$DMG")"
hdiutil create -quiet -volname "Broadcast Appliance" -srcfolder "$STAGE" -fs HFS+ -format UDZO -ov "$DMG" ||
    morir "hdiutil no pudo armar el .dmg"
log "listo: $DMG ($(du -sh "$DMG" | cut -f1))"
