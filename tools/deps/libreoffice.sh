#!/usr/bin/env bash
# ============================================================================
# tools/deps/libreoffice.sh — LibreOffice headless, SOLO para convertir
#
# Proposito:
#   Un orador manda su presentacion en `.pptx` y eso tiene que volverse un
#   carrusel de imagenes. Un `.pptx` es un zip con XML: sacar las diapositivas
#   COMO SE VEN necesita un motor de maquetado, y el unico que lo hace bien es
#   LibreOffice. Se usa UNA sola cosa de el:
#
#       soffice --headless --convert-to pdf  archivo.pptx
#
#   De ahi a imagenes rinde Qt6::Pdf, que ya viaja con nosotros: no hace falta
#   ninguna dependencia mas para la segunda mitad (ver carrusel/Conversion.cpp).
#
# NO SE PUEDE QUEDAR SOLO CON IMPRESS, y conviene saberlo antes de esperar
# milagros: LibreOffice moderno fusiona el nucleo en `libmergedlo.so`, que sola
# pesa 85 MB. Sacando Writer (20 MB), Calc (19 MB) y el arte de la interfaz
# (38 MB, que headless no se usa) no alcanza para bajar mucho. Lo que SI baja
# es sacar los idiomas: el .msi de Windows trae 123 y nosotros convertimos
# archivos, no le mostramos menus a nadie. Medido: Linux 730 -> 421 MB,
# Windows 825 -> 470 MB.
#
# POR ESO ES OPCIONAL. No lo baja `make deps`: hay que pedirlo. Y el bundle lo
# incluye solo si esta (ver src/app/CMakeLists.txt). El que recibe PDF y no
# PowerPoint no carga 420 MB para nada.
#
# LA PODA SE VERIFICA. Despues de podar se convierte una diapositiva a PDF de
# verdad y, si no sale, esto corta. No es prolijidad: una version anterior
# borraba `share/config` entera y dejaba un LibreOffice que no abria NINGUN
# archivo, con el mensaje inutil «source file could not be loaded». En Windows
# no se puede convertir desde Linux, asi que ahi se verifica el arbol y se DICE
# que es lo que se verifico.
#
# Y DE PASO, LAS FUENTES. El tarball oficial trae un juego tipografico decente
# —Liberation, Carlito, Caladea, DejaVu, Noto— que el aparato no tenia: lo unico
# que viajaba era JetBrains Mono, una monoespaciada de programar. Se extraen
# aparte, a `extern/libreoffice/fonts`, porque SIRVEN AUNQUE NO SE USE
# LibreOffice: son para los graficos y los rotulos. (El paquete de la
# distribucion NO las trae: depende de las del sistema. El oficial si.)
#
# LAS DOS PLATAFORMAS. El bundle se arma para Linux y para Windows desde esta
# misma maquina (ver deps-win), asi que se bajan las dos y se extraen sin
# instalar nada: el .tar.gz de .deb y el .msi.
#
# Uso: tools/deps/libreoffice.sh [linux|win|todo]   (idempotente)
# ============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DL="$ROOT/extern/.downloads"
OUT="$ROOT/extern/libreoffice"
QUE="${1:-todo}"

VER="26.2.6"
BASE="https://download.documentfoundation.org/libreoffice/stable/$VER"
LINUX_TGZ="LibreOffice_${VER}_Linux_x86-64_deb.tar.gz"
LINUX_SHA="fd0e8f8f2408dd2e5b90286e60f3f97cf566ba441cd48cfc5bcc68067303e0bc"
WIN_MSI="LibreOffice_${VER}_Win_x86-64.msi"
WIN_SHA="f9877032fd908beb9c0ddf06df4af5c2e85f419c42e14876c4cce5aae5fb2660"

log() { printf '\033[36m[libreoffice]\033[0m %s\n' "$*"; }
morir() { printf '\033[31m[libreoffice] ERROR:\033[0m %s\n' "$*" >&2; exit 1; }

mkdir -p "$DL" "$OUT"

bajar() { # nombre url sha
    local f="$DL/$1"
    if [ -f "$f" ] && [ "$(sha256sum "$f" | cut -d' ' -f1)" = "$3" ]; then
        log "$1 ya estaba y el hash coincide"
        return
    fi
    log "bajando $1 (son cientos de MB, tarda)"
    # Sin barra de progreso cuando no hay terminal: en un log de CI son cientos
    # de renglones de numeros que tapan todo lo demas.
    local silencio=(); [ -t 2 ] || silencio=(--no-progress-meter)
    curl -fL --retry 3 "${silencio[@]}" -o "$f.parcial" "$2" || morir "no se pudo bajar $1"
    # Se verifica ANTES de mover: un archivo a medias con el nombre bueno se
    # toma por bueno la proxima vez y falla mucho despues, lejos de la causa.
    echo "$3  $f.parcial" | sha256sum -c --quiet - || morir "el hash de $1 no coincide"
    mv "$f.parcial" "$f"
}

# Lo que NO va. Medido sobre la instalacion de verdad, no estimado: la primera
# poda dejaba 563 MB porque miraba solo cuatro carpetas. Esta baja a la mitad.
#
# Se usa UNA cosa: `--headless --convert-to pdf` sobre un .pptx. Todo lo que no
# participa de eso sobra:
#   - Writer y Calc: no abrimos textos ni planillas.
#   - help/ y los creditos: headless no muestra ayuda a nadie (29 MB).
#   - python-core: es para escribir macros, no para convertir (38 MB).
#   - extensions/ y wizards/: asistentes y plantillas de la interfaz (76 MB).
#   - classes/: los filtros en Java, que convert-to no usa (8 MB).
#   - libmwaw: importar formatos de procesadores de texto viejos de Mac.
#   - share/fonts: se copian APARTE a extern/libreoffice/fonts (ver abajo), asi
#     que tenerlas dos veces es cargar 54 MB al pedo.
podar() {
    local dir="$1"
    # Writer y Calc: no abrimos .docx ni .xlsx, solo diapositivas.
    rm -f  "$dir/program/libswlo.so"  "$dir/program/libsclo.so"  2>/dev/null || true
    rm -f  "$dir/program/swlo.dll"    "$dir/program/sclo.dll"    2>/dev/null || true
    rm -f  "$dir/program/libmwaw"*    "$dir/program/mwaw"*       2>/dev/null || true
    rm -rf "$dir/share/gallery" "$dir/share/template" 2>/dev/null || true
    rm -rf "$dir/share/extensions" "$dir/share/wizards" "$dir/share/basic" 2>/dev/null || true
    rm -rf "$dir/share/fonts" 2>/dev/null || true
    rm -rf "$dir/help" "$dir/readmes" "$dir/share/readme" 2>/dev/null || true
    rm -rf "$dir/program/classes" 2>/dev/null || true
    rm -f  "$dir/CREDITS.fodt" 2>/dev/null || true
    # IDIOMAS. El .msi de Windows es el instalador multi-idioma: 123 juegos de
    # recursos (272 MB) y 124 Langpack (96 MB). Nosotros convertimos archivos,
    # no le mostramos menus a nadie. Se quedan español e ingles —el ingles
    # porque el tarball de Linux viene asi y sacarlo lo dejaria sin ninguno—.
    if [ -d "$dir/program/resource" ]; then
        local idioma
        for idioma in "$dir/program/resource"/*; do
            case "$(basename "$idioma")" in
                es|en*) ;;
                *) rm -rf "$idioma" ;;
            esac
        done
    fi
    local xcd
    for xcd in "$dir/share/registry"/Langpack-*.xcd \
               "$dir/share/registry/res"/fcfg_langpack_*.xcd \
               "$dir/share/registry/res"/registry_*.xcd; do
        [ -e "$xcd" ] || continue
        case "$(basename "$xcd")" in
            *_es.xcd|*_en-US.xcd|Langpack-es.xcd|Langpack-en-US.xcd) ;;
            *) rm -f "$xcd" ;;
        esac
    done
    # Los juegos de iconos (~70 MB) son para la ventana, que nunca se abre.
    # OJO: se borran los .zip, NO la carpeta. `share/config` entera se llevaba
    # puesto el soffice.cfg y entonces NO CARGA NINGUN ARCHIVO, con el mensaje
    # inutil «source file could not be loaded». Lo mismo pasa si falta
    # `program/python-core`: aunque no usemos Python, sin el no arranca el
    # registro de servicios. Los dos se aprendieron rompiendolo.
    rm -f  "$dir/share/config/images_"*.zip 2>/dev/null || true
}

# Convertir de verdad, que es lo unico que prueba que la poda no lo rompio.
# Sin esto un `rm -rf` de mas queda escondido hasta que un operador, en vivo,
# arrastra un .pptx y no pasa nada.
convierte() {
    local dir="$1"
    # El arbol de Windows PRIMERO: ahi el binario es soffice.exe y no se puede
    # correr desde aca, asi que se verifica que este armado y nada mas. Mirar
    # `soffice` antes daba por podado de mas un arbol que estaba perfecto.
    case "$dir" in
        *"/win")
            [ -f "$dir/program/soffice.exe" ] && [ -f "$dir/program/soffice.bin" ] &&
                [ -d "$dir/share/registry" ]
            return $?
            ;;
    esac
    local soffice="$dir/program/soffice"
    [ -x "$soffice" ] || return 1
    local t; t="$(mktemp -d)"
    cat > "$t/p.fodp" <<'FODP'
<?xml version="1.0" encoding="UTF-8"?>
<office:document xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
 xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0"
 xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0"
 xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0"
 office:version="1.3" office:mimetype="application/vnd.oasis.opendocument.presentation">
 <office:body><office:presentation>
  <draw:page draw:name="p1"><draw:frame svg:width="20cm" svg:height="4cm" svg:x="2cm" svg:y="6cm">
   <draw:text-box><text:p>prueba</text:p></draw:text-box></draw:frame></draw:page>
 </office:presentation></office:body></office:document>
FODP
    HOME="$t" "$soffice" --headless --norestore --convert-to pdf --outdir "$t" "$t/p.fodp" >/dev/null 2>&1 || true
    local ok=1; [ -s "$t/p.pdf" ] && ok=0
    rm -rf "$t"
    return $ok
}

verificar() {
    convierte "$1" || morir "podado de mas. Revisa podar()"
    # Se dice QUE se verifico. En Windows no se puede convertir desde aca y
    # decir «convierte a PDF» seria mentir sobre la unica prueba que importa.
    case "$1" in
        *"/win") log "verificado: el arbol esta completo (no se puede convertir desde Linux)" ;;
        *)       log "verificado: convierte a PDF" ;;
    esac
}

fuentes_aparte() { # dir
    local origen="$1/share/fonts/truetype"
    [ -d "$origen" ] || return 0
    mkdir -p "$OUT/fonts"
    # Solo las de ALFABETO LATINO, que es en lo que se transmite aca. Las de
    # arabe, hebreo, armenio, georgiano, lao y lisu son 13 de los 52 MB y no las
    # va a usar nadie haciendo un zocalo en castellano. (Si alguna vez hace
    # falta, estan en el tarball: es sacar una linea de aca.)
    find "$origen" -maxdepth 1 -iname '*.ttf' \
         ! -iname 'opens___*'    ! -iname '*symbol*' \
         ! -iname 'Noto*Arabic*' ! -iname 'Noto*Hebrew*'   ! -iname 'Noto*Armenian*' \
         ! -iname 'Noto*Georgian*' ! -iname 'Noto*Lao*'    ! -iname 'Noto*Lisu*' \
         ! -iname 'Noto*Kufi*'   ! -iname 'Amiri*'         ! -iname 'Scheherazade*' \
         ! -iname 'ReemKufi*'    ! -iname '*CLM*'          ! -iname 'DavidLibre*' \
         ! -iname 'DejaVuMath*' \
         -exec cp -n {} "$OUT/fonts/" \; 2>/dev/null || true
    log "fuentes: $(ls -1 "$OUT/fonts" 2>/dev/null | wc -l) archivos en extern/libreoffice/fonts"
}

hacer_linux() {
    # Si ya estaba, igual SE VUELVE A PODAR: la poda mejora con el tiempo —la
    # primera version dejaba 563 MB porque miraba cuatro carpetas— y sin esto
    # el que ya lo tenia no se beneficia nunca. `rm -rf` de lo que no esta no
    # hace nada, asi que correrlo de nuevo es gratis.
    if [ -x "$OUT/linux/program/soffice" ]; then
        log "linux ya estaba; repodando por si la poda mejoro"
        # Las fuentes PRIMERO: podar borra share/fonts, asi que al reves se
        # perderian las que todavia no se habian copiado.
        fuentes_aparte "$OUT/linux"
        podar "$OUT/linux"
        # Una poda vieja pudo haberlo dejado sin cargar archivos. Si no
        # convierte no hay nada que reparar a mano: se baja de nuevo.
        if convierte "$OUT/linux"; then
            log "linux: $(du -sh "$OUT/linux" | cut -f1) (verificado)"
            return
        fi
        log "el que estaba no convierte; lo bajo de nuevo"
        rm -rf "$OUT/linux"
    fi
    bajar "$LINUX_TGZ" "$BASE/deb/x86_64/$LINUX_TGZ" "$LINUX_SHA"
    local tmp; tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' RETURN
    log "extrayendo (sin instalar nada en el sistema)"
    tar xf "$DL/$LINUX_TGZ" -C "$tmp"
    # Los .deb se abren con `ar` + `tar`, que estan en cualquier Linux: no hace
    # falta dpkg ni permisos de root. Esto NO instala: extrae.
    mkdir -p "$tmp/x"
    local d
    while IFS= read -r d; do
        ( cd "$tmp/x" && ar p "$d" data.tar.xz 2>/dev/null | tar xJ 2>/dev/null ) || true
    done < <(find "$tmp" -name '*.deb' ! -name '*-sdk*' ! -name '*debug*')
    local raiz
    raiz="$(find "$tmp/x" -maxdepth 6 -type d -name program -path '*libreoffice*' | head -1)"
    [ -n "$raiz" ] || morir "no encontre program/ adentro de los .deb"
    raiz="$(dirname "$raiz")"
    rm -rf "$OUT/linux"
    mkdir -p "$OUT/linux"
    cp -a "$raiz/." "$OUT/linux/"
    fuentes_aparte "$OUT/linux"  # antes de podar: podar borra share/fonts
    podar "$OUT/linux"
    verificar "$OUT/linux"
    log "linux listo: $(du -sh "$OUT/linux" | cut -f1)"
}

hacer_win() {
    if [ -f "$OUT/win/program/soffice.exe" ]; then
        log "windows ya estaba; repodando"
        podar "$OUT/win"
        verificar "$OUT/win"
        log "windows: $(du -sh "$OUT/win" | cut -f1)"
        return
    fi
    bajar "$WIN_MSI" "$BASE/win/x86_64/$WIN_MSI" "$WIN_SHA"
    # SOLO msiextract. 7z «abre» el .msi pero escupe los flujos OLE crudos
    # (_0001.cfs y asi), sin nombres ni carpetas: el arbol program/ y share/
    # vive en las tablas Directory y File del instalador, y reconstruirlo es
    # justo lo que hace msitools. Esto se probo: con 7z quedan 19.496 archivos
    # sin un solo directorio.
    #
    # Y viene de extern/, no del sistema: aca no se instala nada a mano. Si no
    # esta, se lo baja solo (tools/deps/msitools.sh, 884 KB).
    local MSIEXTRACT="$ROOT/extern/msitools/msiextract"
    if [ ! -x "$MSIEXTRACT" ]; then
        log "falta msitools; lo bajo"
        "$ROOT/tools/deps/msitools.sh" || morir "no pude preparar msitools"
    fi
    local tmp; tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' RETURN
    log "extrayendo el .msi"
    ( cd "$tmp" && "$MSIEXTRACT" "$DL/$WIN_MSI" >/dev/null )
    local raiz
    raiz="$(find "$tmp" -maxdepth 6 -type d -name program | head -1)"
    [ -n "$raiz" ] || morir "no encontre program/ adentro del .msi"
    raiz="$(dirname "$raiz")"
    rm -rf "$OUT/win"
    mkdir -p "$OUT/win"
    cp -a "$raiz/." "$OUT/win/"
    podar "$OUT/win"
    verificar "$OUT/win"
    log "windows listo: $(du -sh "$OUT/win" | cut -f1)"
}

case "$QUE" in
    linux) hacer_linux ;;
    win)   hacer_win ;;
    todo)  hacer_linux; hacer_win ;;
    *)     morir "uso: $0 [linux|win|todo]" ;;
esac

log "listo. El bundle lo incluye solo si extern/libreoffice existe."
