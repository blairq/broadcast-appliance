#!/bin/bash
# tools/firma-local.sh — un certificado de firma PROPIO, para que macOS deje de
# pedir los permisos (cámara, micrófono) en cada compilación.
#
# POR QUÉ: la .app se firma ad-hoc (no hay cuenta de Apple Developer). Con una
# firma ad-hoc, la identidad de la app para macOS es el HASH del binario
# («designated => cdhash H"…"»), que cambia en cada compilación: macOS la toma
# por una app nueva y vuelve a preguntar por la cámara y el micrófono.
# Firmada con un certificado (aunque sea autofirmado) la identidad pasa a ser
# «este id de app + este certificado», que no cambia: el permiso se recuerda.
#
# Crea UNA vez el certificado «BA Desarrollo local» (válido 10 años) en el
# llavero de inicio de sesión. Las firmas (cmake/BaMacBundle.cmake,
# tools/deps/app-macos.sh) lo usan solo si existe; si no, siguen ad-hoc.
# No toca nada del sistema ni hace falta ser administrador. Para sacarlo:
#   security delete-identity -c "BA Desarrollo local"
#
# Uso: tools/firma-local.sh        (no hace nada si ya existe)
set -euo pipefail

NOMBRE="BA Desarrollo local"
LLAVERO="$HOME/Library/Keychains/login.keychain-db"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

if security find-identity -p codesigning "$LLAVERO" 2>/dev/null | grep -q "\"$NOMBRE\""; then
    echo "el certificado «$NOMBRE» ya está"
else
cat >"$TMP/cert.cnf" <<EOF
[req]
distinguished_name = dn
x509_extensions = ext
prompt = no
[dn]
CN = $NOMBRE
[ext]
basicConstraints = critical, CA:FALSE
keyUsage = critical, digitalSignature
extendedKeyUsage = critical, codeSigning
EOF
openssl req -x509 -newkey rsa:2048 -nodes -days 3650 -config "$TMP/cert.cnf" \
    -keyout "$TMP/clave.pem" -out "$TMP/cert.pem" 2>/dev/null
# El .p12 con el formato viejo (el que entiende `security` de macOS).
CLAVE="ba-$RANDOM$RANDOM"
openssl pkcs12 -export -legacy -inkey "$TMP/clave.pem" -in "$TMP/cert.pem" -name "$NOMBRE" \
    -out "$TMP/cert.p12" -passout "pass:$CLAVE" 2>/dev/null ||
    openssl pkcs12 -export -inkey "$TMP/clave.pem" -in "$TMP/cert.pem" -name "$NOMBRE" \
        -out "$TMP/cert.p12" -passout "pass:$CLAVE"
# -T: codesign puede usar la clave sin preguntar.
security import "$TMP/cert.p12" -k "$LLAVERO" -P "$CLAVE" -T /usr/bin/codesign >/dev/null
echo "certificado «$NOMBRE» creado"
fi

# Que codesign use la clave sin el cartel «codesign quiere usar la clave…»
# (pide UNA vez la contraseña de inicio de sesión de la Mac).
if [ -t 0 ]; then
    echo "Para que codesign use la clave sin preguntar, escribí la contraseña de inicio de sesión de la Mac:"
    security set-key-partition-list -S apple-tool:,apple:,codesign: -s -l "$NOMBRE" "$LLAVERO" >/dev/null
fi

# Prueba: firmar una copia de un binario cualquiera.
cp /usr/bin/true "$TMP/prueba"
if timeout 20 codesign -f -s "$NOMBRE" "$TMP/prueba" 2>"$TMP/err" && codesign -v "$TMP/prueba"; then
    echo "listo: «$NOMBRE». Las próximas compilaciones de la .app se firman con él."
    echo "La primera vez macOS pide los permisos una vez más; después los recuerda."
else
    echo "el certificado está, pero codesign todavía no lo puede usar sin preguntar:" >&2
    cat "$TMP/err" >&2
    echo "Correlo en una terminal (tools/firma-local.sh) para poner la contraseña una vez." >&2
    exit 1
fi
