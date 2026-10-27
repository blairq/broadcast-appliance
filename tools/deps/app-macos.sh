#!/usr/bin/env bash
# ============================================================================
# tools/deps/app-macos.sh — el bundle portable de macOS como .app (y el .dmg)
#
# La .app ES el bundle portable: el layout de siempre (bin/64bit, gst-plugins,
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
# clic derecho -> Abrir (lo explican el "Leeme primero" y el "Read me first" del .dmg).
#
# Uso: tools/deps/app-macos.sh [--rapida] <release/> <salida.app> [<salida.dmg>]
#
# --rapida (make app-macos-rapida, para probar mientras se programa): si la .app
# ya existe, copia solo lo que cambió (rsync) en vez de borrarla y copiar 1,7 GB,
# y el manifest reusa los hashes de lo que no cambió (cache en build/).
# ============================================================================
set -euo pipefail
RAPIDA=0
if [ "${1:-}" = --rapida ]; then RAPIDA=1; shift; fi

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
C="$APP/Contents"
R="$C/Resources" # la raiz del layout portable
if [ "$RAPIDA" = 1 ] && [ -d "$R" ] && [ -x "$C/MacOS/BroadcastAppliance" ]; then
    log "actualizando $(basename "$APP") (solo lo que cambió)"
    # -a conserva enlaces, permisos y fechas; las firmas van adentro de cada archivo.
    rsync -a --delete --exclude /manifest.json --exclude /BroadcastAppliance \
        --exclude /broadcast-appliance.icns "$REL/" "$R/"
    rsync -a "$REL/BroadcastAppliance" "$C/MacOS/BroadcastAppliance"
    # Lo que se acaba de firmar se copia igual: firmar le devuelve al archivo su
    # fecha (tools/deps/firmar-macos.sh) y, si el tamaño no cambió, rsync no lo vería.
    if [ -n "${BA_FIRMADOS:-}" ] && [ -f "$BA_FIRMADOS" ]; then
        REL_ARG="${1%/}"
        while IFS= read -r p; do
            rel="${p#"$REL"/}"
            rel="${rel#"$REL_ARG"/}"
            [ -e "$REL/$rel" ] || continue
            if [ "$rel" = BroadcastAppliance ]; then
                ditto "$REL/$rel" "$C/MacOS/BroadcastAppliance"
            else
                rm -rf "$R/$rel"
                ditto "$REL/$rel" "$R/$rel"
            fi
        done <"$BA_FIRMADOS"
    fi
else
    RAPIDA=0
    rm -rf "$APP"
    mkdir -p "$C/MacOS"
    log "copiando el bundle a $(basename "$APP")/Contents/Resources"
    # ditto conserva enlaces, permisos y firmas (frameworks).
    ditto "$REL" "$R"
    mv "$R/BroadcastAppliance" "$C/MacOS/BroadcastAppliance"
fi
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
    <string>io.github.blairq.broadcastappliance</string>
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
    <!-- Las «Reacciones» de macOS 14 (corazones, fuegos artificiales con gestos de la mano)
         vienen prendidas en cada app que abre la cámara: un detector de objetos por cuadro
         (~1/4 de núcleo) y efectos que podrían salir al aire. Arrancan apagadas. -->
    <key>NSCameraReactionEffectGesturesEnabledDefault</key>
    <false/>
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
# La cache de hashes se usa siempre (el ctime cambia con cualquier escritura, así
# que no puede quedar un hash viejo); con la .app recién copiada no sirve de nada.
mkdir -p "$ROOT/build"
BA_MANIFEST_CACHE="$ROOT/build/manifest-app-macos.txt" "$ROOT/tools/deploy" --solo-manifest "$R" >/dev/null
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

1. Arrastrá "BroadcastAppliance" a Aplicaciones o, mejor, a una carpeta
   propia (por ejemplo en un disco externo): es portable. Al lado de la app
   se crea la carpeta "BroadcastAppliance Datos" con la configuración, las
   grabaciones y los modelos: para llevarla a otra Mac, llevá las dos cosas.
   No lo abras desde este disco (.dmg): ahí es de solo lectura.

2. La PRIMERA vez: clic derecho sobre BroadcastAppliance -> Abrir -> Abrir.
   El programa no está firmado con un certificado de Apple, así que un doble
   clic muestra "no se puede verificar el desarrollador". Después de abrirlo
   así una vez, se abre normalmente.
   (Alternativa por terminal:  xattr -dr com.apple.quarantine /Applications/BroadcastAppliance.app)

3. macOS pide permiso para la cámara, el micrófono y la red local: aceptalos.
   Para capturar la pantalla hay que darlo en Ajustes -> Privacidad y
   seguridad -> Grabación de pantalla, y volver a abrir el programa.

Las grabaciones y la configuración quedan en "BroadcastAppliance Datos", al
lado de la app (no adentro: así la app sigue sellada y macOS no la vuelve a
verificar ni a pedir los permisos). Las instrucciones completas están en
Contents/Resources/LEEME.txt (en inglés: Contents/Resources/README.txt).
TXT
cat >"$STAGE/Read me first.txt" <<'TXT'
BROADCAST APPLIANCE FOR macOS (Apple Silicon, macOS 13.3 or newer)
==================================================================

1. Drag "BroadcastAppliance" to Applications or, better, to a folder of your
   own (on an external disk, for example): it's portable. A folder called
   "BroadcastAppliance Datos" is created next to the app with the settings,
   the recordings and the models: to take it to another Mac, take both.
   Don't open it from this disk (.dmg): it's read-only.

2. The FIRST time: right-click BroadcastAppliance -> Open -> Open.
   The program isn't signed with an Apple certificate, so a double-click
   shows "the developer cannot be verified". Once you've opened it this way,
   it opens normally from then on.
   (Or from a terminal:  xattr -dr com.apple.quarantine /Applications/BroadcastAppliance.app)

3. macOS asks for permission to use the camera, the microphone and the local
   network: allow them. To capture the screen, grant it in System Settings ->
   Privacy & Security -> Screen Recording, and open the program again.

Recordings and settings are kept in "BroadcastAppliance Datos", next to the
app (not inside it: that way the app stays sealed and macOS doesn't verify it
again or ask for the permissions again). The full instructions are in
Contents/Resources/README.txt (in Spanish: Contents/Resources/LEEME.txt).
TXT
rm -f "$DMG"
log "armando $(basename "$DMG")"
hdiutil create -quiet -volname "Broadcast Appliance" -srcfolder "$STAGE" -fs HFS+ -format UDZO -ov "$DMG" ||
    morir "hdiutil no pudo armar el .dmg"
log "listo: $DMG ($(du -sh "$DMG" | cut -f1))"
