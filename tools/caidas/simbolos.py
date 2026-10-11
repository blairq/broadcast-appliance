#!/usr/bin/env python3
# ============================================================================
# tools/caidas/simbolos.py — el almacén de símbolos de cada build (Windows)
#
# Para leer la pila de un volcado hacen falta, de cada módulo, el MISMO
# binario (para desenrollar la pila: sus tablas .pdata) y su .pdb (los
# nombres y las líneas). El volcado no trae el commit: trae, por módulo, la
# marca de tiempo y el tamaño del binario, y el GUID+edad de su .pdb. Por eso
# se guardan con esas claves, como el servidor de símbolos de Microsoft:
#
#   <almacén>/<archivo.dll>/<TIMESTAMP><SIZEOFIMAGE>/<archivo.dll>
#   <almacén>/<archivo.pdb>/<GUID><EDAD>/<archivo.pdb>
#
# Lo que ya está no se vuelve a copiar (GStreamer y compañía son los mismos
# en cada build): crece solo lo nuestro. tools/caidas/analizar.py lo usa.
#
# Uso (lo llama tools/ci/compilar-en-docker.sh después de armar el .zip):
#   simbolos.py guardar --release release-win --buscar build/win extern/gstreamer-win-pdb \
#               --destino /salida/simbolos [--commit SHA]
# Solo biblioteca estándar.
# ============================================================================
import argparse
import json
import os
import shutil
import struct
import sys
import time
from pathlib import Path


def leer_pe(ruta):
    """(timestamp, sizeofimage, rsds) de un PE; rsds = (guid_str, edad, nombre_pdb) o None."""
    with open(ruta, "rb") as f:
        d = f.read()
    if d[:2] != b"MZ":
        return None
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    if d[pe:pe + 4] != b"PE\0\0":
        return None
    coff = pe + 4
    nsec = struct.unpack_from("<H", d, coff + 2)[0]
    ts = struct.unpack_from("<I", d, coff + 4)[0]
    tam_opt = struct.unpack_from("<H", d, coff + 16)[0]
    opt = coff + 20
    magic = struct.unpack_from("<H", d, opt)[0]
    if magic == 0x20B:
        size_img = struct.unpack_from("<I", d, opt + 56)[0]
        dirs = opt + 112
    else:
        size_img = struct.unpack_from("<I", d, opt + 56)[0]
        dirs = opt + 96
    secs = []
    s0 = opt + tam_opt
    for i in range(nsec):
        o = s0 + 40 * i
        vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", d, o + 8)
        secs.append((va, max(vsize, rawsize), rawptr))

    def rva_a_off(rva):
        for va, sz, raw in secs:
            if va <= rva < va + sz:
                return raw + (rva - va)
        return None

    rsds = None
    dbg_rva, dbg_tam = struct.unpack_from("<II", d, dirs + 6 * 8)
    off = rva_a_off(dbg_rva) if dbg_rva else None
    if off is not None:
        for k in range(dbg_tam // 28):
            e = off + 28 * k
            tipo = struct.unpack_from("<I", d, e + 12)[0]
            tam, _, ptr = struct.unpack_from("<III", d, e + 16)
            if tipo == 2 and d[ptr:ptr + 4] == b"RSDS":
                g = d[ptr + 4:ptr + 20]
                edad = struct.unpack_from("<I", d, ptr + 20)[0]
                nombre = d[ptr + 24:ptr + tam].split(b"\0")[0].decode("utf-8", "replace")
                rsds = (guid_str(g), edad, nombre.replace("\\", "/").split("/")[-1])
    return ts, size_img, rsds


def guid_str(g):
    a, b, c = struct.unpack_from("<IHH", g, 0)
    return "%08X%04X%04X%s" % (a, b, c, g[8:].hex().upper())


def guid_de_pdb(ruta):
    """El GUID del .pdb (stream 1 del MSF 7.0), para no guardar uno que no corresponde."""
    try:
        with open(ruta, "rb") as f:
            cab = f.read(56)
            if not cab.startswith(b"Microsoft C/C++ MSF 7.00"):
                return None
            bloque, _, _, dir_bytes, _, mapa = struct.unpack_from("<IIIIII", cab, 32)
            f.seek(mapa * bloque)
            n_dir = (dir_bytes + bloque - 1) // bloque
            bloques_dir = struct.unpack("<%dI" % n_dir, f.read(4 * n_dir))
            dir_ = b""
            for b in bloques_dir:
                f.seek(b * bloque)
                dir_ += f.read(bloque)
            n = struct.unpack_from("<I", dir_, 0)[0]
            tams = struct.unpack_from("<%dI" % n, dir_, 4)
            pos = 4 + 4 * n
            pos += 4 * ((tams[0] + bloque - 1) // bloque if tams[0] != 0xFFFFFFFF else 0)
            b1 = struct.unpack_from("<I", dir_, pos)[0]
            f.seek(b1 * bloque)
            info = f.read(28)
            return guid_str(info[12:28])
    except (OSError, struct.error, IndexError):
        return None


def copiar(src, dst):
    if dst.exists() and dst.stat().st_size == src.stat().st_size:
        return False
    dst.parent.mkdir(parents=True, exist_ok=True)
    tmp = dst.with_suffix(dst.suffix + ".tmp")
    shutil.copyfile(src, tmp)
    os.replace(tmp, dst)
    return True


def guardar(a):
    destino = Path(a.destino)
    pdbs = {}
    for raiz in a.buscar:
        for p in Path(raiz).rglob("*.pdb"):
            if "CMakeFiles" in p.parts:
                continue
            pdbs.setdefault(p.name.lower(), []).append(p)
    nuevos_bin = nuevos_pdb = modulos = sin_pdb = 0
    for p in Path(a.release).rglob("*"):
        if p.suffix.lower() not in (".exe", ".dll", ".clap", ".pyd") or not p.is_file():
            continue
        info = leer_pe(p)
        if info is None:
            continue
        ts, tam, rsds = info
        modulos += 1
        if copiar(p, destino / p.name / ("%08X%x" % (ts, tam)) / p.name):
            nuevos_bin += 1
        if rsds is None:
            sin_pdb += 1
            continue
        guid, edad, nombre = rsds
        candidatos = pdbs.get(nombre.lower(), [])
        elegido = next((c for c in candidatos if guid_de_pdb(c) == guid), None)
        if elegido is None:
            sin_pdb += 1
            continue
        if copiar(elegido, destino / nombre / ("%s%X" % (guid, edad)) / nombre):
            nuevos_pdb += 1
    with open(destino / "compilaciones.jsonl", "a") as f:
        f.write(json.dumps({"commit": a.commit, "plataforma": "windows", "hora": time.strftime("%Y-%m-%dT%H:%M:%S"),
                            "modulos": modulos, "sin_pdb": sin_pdb, "binarios_nuevos": nuevos_bin,
                            "pdb_nuevos": nuevos_pdb}) + "\n")
    print("[simbolos] %d módulos: %d binarios y %d .pdb nuevos en %s (%d sin .pdb)" %
          (modulos, nuevos_bin, nuevos_pdb, destino, sin_pdb))


def main():
    ap = argparse.ArgumentParser(description="almacén de símbolos de un build de Windows")
    sub = ap.add_subparsers(dest="cmd", required=True)
    g = sub.add_parser("guardar")
    g.add_argument("--release", required=True, help="el bundle armado (release-win)")
    g.add_argument("--buscar", nargs="+", required=True, help="carpetas donde están los .pdb")
    g.add_argument("--destino", required=True)
    g.add_argument("--commit", default="")
    a = ap.parse_args()
    if a.cmd == "guardar":
        guardar(a)


if __name__ == "__main__":
    sys.exit(main())
