#!/usr/bin/env python3
# ============================================================================
# tools/caidas/analizar.py — qué pasó en cada volcado, con nombres y líneas
#
#   tools/caidas/analizar.py --pc                 trae los nuevos de la PC de pruebas y los analiza
#   tools/caidas/analizar.py dist/caidas/…/X.dmp  uno (o varios, o una carpeta)
#
# Para cada volcado (Logs/dumps/A-<fecha>-caida.dmp y su .json, que escribe el
# launcher: src/launcher/Volcados.hpp):
#   1. Lee el minidump (hilos, módulos, memoria, la excepción).
#   2. Desenrolla la pila de cada hilo con las tablas .pdata de cada módulo
#      (como RtlVirtualUnwind): necesita el MISMO binario, que busca en el
#      almacén (dist/simbolos, lo llena tools/caidas/simbolos.py en cada build)
#      o, para los de Windows, en el servidor público de símbolos de Microsoft
#      (queda en dist/simbolos también).
#   3. Pone nombres y líneas con llvm-symbolizer y los .pdb (el del toolchain
#      de Windows, en el volumen de Docker del build).
#   4. Escribe el informe: qué fue, dónde, la pila del hilo que se cayó (o de
#      todos, si se colgó), y lo que venía diciendo el registro. Al lado del
#      volcado queda un .txt con lo mismo.
#
# Solo biblioteca estándar (y Docker para llvm-symbolizer).
# ============================================================================
import argparse
import bisect
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import urllib.request
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
ALMACEN = RAIZ / "dist" / "simbolos"
CAIDAS = RAIZ / "dist" / "caidas"
MS = "https://msdl.microsoft.com/download/symbols"
SYMBOLIZER = "/trabajo/ba/extern/toolchain/llvm/bin/llvm-symbolizer"
REGS = ["rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"]
CODIGOS = {0xC0000005: "violación de acceso", 0xC0000374: "corrupción del heap", 0xC0000409: "fail-fast / desborde de búfer",
           0xC00000FD: "desbordamiento de pila", 0xE06D7363: "excepción de C++ sin atrapar", 0xE0000BAB: "abort (o terminate)",
           0x80000003: "punto de interrupción", 0xC000001D: "instrucción inválida"}


# --- minidump --------------------------------------------------------------------------------

class Minidump:
    def __init__(self, ruta):
        self.d = Path(ruta).read_bytes()
        sig, _, n, rva = struct.unpack_from("<IIII", self.d, 0)
        if sig != 0x504D444D:
            raise ValueError("no es un minidump")
        self.mem = []  # (inicio, tam, offset en el archivo)
        self.hilos, self.modulos, self.excepcion = [], [], None
        streams = {}
        for i in range(n):
            t, tam, r = struct.unpack_from("<III", self.d, rva + 12 * i)
            streams.setdefault(t, (tam, r))
        if 5 in streams:
            _, r = streams[5]
            c = struct.unpack_from("<I", self.d, r)[0]
            for k in range(c):
                ini, tam, off = struct.unpack_from("<QII", self.d, r + 4 + 16 * k)
                self.mem.append((ini, tam, off))
        if 9 in streams:
            _, r = streams[9]
            c, base = struct.unpack_from("<QQ", self.d, r)
            off = base
            for k in range(c):
                ini, tam = struct.unpack_from("<QQ", self.d, r + 16 + 16 * k)
                self.mem.append((ini, tam, off))
                off += tam
        self.mem.sort()
        self._inis = [m[0] for m in self.mem]
        if 3 in streams:
            _, r = streams[3]
            c = struct.unpack_from("<I", self.d, r)[0]
            for k in range(c):
                o = r + 4 + 48 * k
                tid = struct.unpack_from("<I", self.d, o)[0]
                ctx_tam, ctx_rva = struct.unpack_from("<II", self.d, o + 40)
                self.hilos.append({"id": tid, "ctx": self.contexto(ctx_rva)})
        if 4 in streams:
            _, r = streams[4]
            c = struct.unpack_from("<I", self.d, r)[0]
            for k in range(c):
                o = r + 4 + 108 * k
                base, tam, _, ts, nrva = struct.unpack_from("<QIIII", self.d, o)
                cv_tam, cv_rva = struct.unpack_from("<II", self.d, o + 76)
                ln = struct.unpack_from("<I", self.d, nrva)[0]
                ruta = self.d[nrva + 4:nrva + 4 + ln].decode("utf-16-le", "replace")
                rsds = None
                if cv_tam >= 24 and self.d[cv_rva:cv_rva + 4] == b"RSDS":
                    g = self.d[cv_rva + 4:cv_rva + 20]
                    edad = struct.unpack_from("<I", self.d, cv_rva + 20)[0]
                    nom = self.d[cv_rva + 24:cv_rva + cv_tam].split(b"\0")[0].decode("utf-8", "replace")
                    a, b, cc = struct.unpack_from("<IHH", g)
                    rsds = ("%08X%04X%04X%s" % (a, b, cc, g[8:].hex().upper()), edad, nom.replace("\\", "/").split("/")[-1])
                self.modulos.append(Modulo(base, tam, ts, ruta, rsds))
        self.modulos.sort(key=lambda m: m.base)
        self._bases = [m.base for m in self.modulos]
        if 6 in streams:
            _, r = streams[6]
            tid = struct.unpack_from("<I", self.d, r)[0]
            code, _, _, addr = struct.unpack_from("<IIQQ", self.d, r + 8)
            ctx_tam, ctx_rva = struct.unpack_from("<II", self.d, r + 160)
            self.excepcion = {"hilo": tid, "codigo": code, "direccion": addr, "ctx": self.contexto(ctx_rva)}

    def contexto(self, rva):
        d = self.d
        if rva == 0 or rva + 0x100 > len(d):
            return None
        regs = {n: struct.unpack_from("<Q", d, rva + 0x78 + 8 * i)[0] for i, n in enumerate(REGS)}
        regs["rip"] = struct.unpack_from("<Q", d, rva + 0xF8)[0]
        return regs

    def leer(self, dir_, n):
        i = bisect.bisect_right(self._inis, dir_) - 1
        if i < 0:
            return None
        ini, tam, off = self.mem[i]
        if dir_ + n > ini + tam:
            return None
        o = off + (dir_ - ini)
        return self.d[o:o + n]

    def q(self, dir_):
        b = self.leer(dir_, 8)
        return None if b is None else struct.unpack("<Q", b)[0]

    def modulo(self, dir_):
        i = bisect.bisect_right(self._bases, dir_) - 1
        if i >= 0 and dir_ < self.modulos[i].base + self.modulos[i].tam:
            return self.modulos[i]
        return None


class Modulo:
    def __init__(self, base, tam, ts, ruta, rsds):
        self.base, self.tam, self.ts, self.ruta, self.rsds = base, tam, ts, ruta, rsds
        self.nombre = ruta.replace("\\", "/").split("/")[-1]
        self.pe = None      # PE (el binario, para desenrollar)
        self.pdb = None     # ruta del .pdb
        self.buscado = False

    @property
    def del_sistema(self):
        return self.ruta.lower().startswith(("c:\\windows", "\\systemroot", "\\\\?\\c:\\windows"))


# --- PE (para desenrollar) ------------------------------------------------------------------

class PE:
    def __init__(self, ruta):
        self.ruta = ruta
        d = self.d = Path(ruta).read_bytes()
        pe = struct.unpack_from("<I", d, 0x3C)[0]
        coff = pe + 4
        nsec = struct.unpack_from("<H", d, coff + 2)[0]
        tam_opt = struct.unpack_from("<H", d, coff + 16)[0]
        opt = coff + 20
        dirs = opt + 112
        self.secs = []
        for i in range(nsec):
            o = opt + tam_opt + 40 * i
            vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", d, o + 8)
            self.secs.append((va, max(vsize, rawsize), rawptr, rawsize))
        exc_rva, exc_tam = struct.unpack_from("<II", d, dirs + 3 * 8)
        self.pdata = []
        b = self.rva(exc_rva, exc_tam) if exc_rva else None
        if b:
            for k in range(len(b) // 12):
                self.pdata.append(struct.unpack_from("<III", b, 12 * k))
        self.pdata.sort()
        self._ini = [p[0] for p in self.pdata]
        self.exportes = []
        exp_rva, exp_tam = struct.unpack_from("<II", d, dirs)
        e = self.rva(exp_rva, 40) if exp_rva else None
        if e:
            n_nom, f_rva, n_rva, o_rva = struct.unpack_from("<IIII", e, 24)
            n_fun = struct.unpack_from("<I", e, 20)[0]
            funs = self.rva(f_rva, 4 * n_fun) or b""
            noms = self.rva(n_rva, 4 * n_nom) or b""
            ords = self.rva(o_rva, 2 * n_nom) or b""
            for k in range(min(n_nom, len(noms) // 4, len(ords) // 2)):
                nr = struct.unpack_from("<I", noms, 4 * k)[0]
                o = struct.unpack_from("<H", ords, 2 * k)[0]
                if 4 * o + 4 > len(funs):
                    continue
                f = struct.unpack_from("<I", funs, 4 * o)[0]
                nom = (self.rva(nr, 256) or b"").split(b"\0")[0].decode("ascii", "replace")
                self.exportes.append((f, nom))
        self.exportes.sort()
        self._exp = [x[0] for x in self.exportes]

    def rva(self, rva, n):
        for va, sz, raw, rawsize in self.secs:
            if va <= rva < va + sz:
                o = raw + (rva - va)
                if rva - va + n > rawsize:
                    return self.d[o:raw + rawsize] + b"\0" * (rva - va + n - rawsize)
                return self.d[o:o + n]
        return None

    def funcion(self, rva):
        i = bisect.bisect_right(self._ini, rva) - 1
        if i >= 0 and self.pdata[i][0] <= rva < self.pdata[i][1]:
            return self.pdata[i]
        return None

    def exporte(self, rva):
        i = bisect.bisect_right(self._exp, rva) - 1
        if i >= 0 and rva - self.exportes[i][0] < 0x10000:
            return self.exportes[i][1], rva - self.exportes[i][0]
        return None


# --- buscar binarios y .pdb ---------------------------------------------------------------

def bajar(url, destino):
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "Microsoft-Symbol-Server/10.0"})
        with urllib.request.urlopen(req, timeout=30) as r:
            datos = r.read()
        destino.parent.mkdir(parents=True, exist_ok=True)
        destino.write_bytes(datos)
        return True
    except Exception:
        return False


def buscar_simbolos(m, extras, microsoft):
    if m.buscado:
        return
    m.buscado = True
    clave = "%08X%x" % (m.ts, m.tam)
    cand = ALMACEN / m.nombre / clave / m.nombre
    if not cand.exists():
        for x in extras:
            p = Path(x) / m.nombre
            if p.exists():
                try:
                    pe = PE(p)
                    if struct.unpack_from("<I", pe.d, struct.unpack_from("<I", pe.d, 0x3C)[0] + 8)[0] == m.ts:
                        cand = p
                        break
                except Exception:
                    pass
    if not cand.exists() and microsoft and m.del_sistema:
        bajar("%s/%s/%s/%s" % (MS, m.nombre, clave, m.nombre), ALMACEN / m.nombre / clave / m.nombre)
    if cand.exists():
        try:
            m.pe = PE(cand)
        except Exception:
            m.pe = None
    if m.rsds:
        guid, edad, pdb = m.rsds
        p = ALMACEN / pdb / ("%s%X" % (guid, edad)) / pdb
        if not p.exists() and microsoft and m.del_sistema:
            bajar("%s/%s/%s%X/%s" % (MS, pdb, guid, edad, pdb), p)
        if p.exists():
            m.pdb = p


# --- desenrollar (x64, como RtlVirtualUnwind) ---------------------------------------------

def ranuras(op, info):
    return {0: 1, 1: 2 if info == 0 else 3, 2: 1, 3: 1, 4: 2, 5: 3, 6: 2, 7: 3, 8: 2, 9: 3, 10: 1}.get(op, 1)


def desenrollar_uno(md, pe, base, regs):
    """Un paso: devuelve True si pudo (regs queda con el marco anterior)."""
    rip = regs["rip"]
    rva = rip - base
    fn = pe.funcion(rva)
    if fn is None:  # hoja: la dirección de vuelta está arriba de la pila
        ret = md.q(regs["rsp"])
        if ret is None:
            return False
        regs["rip"], regs["rsp"] = ret, regs["rsp"] + 8
        return True
    desp = rva - fn[0]
    primero = True
    maquina = False
    while fn is not None:
        ui = pe.rva(fn[2], 4)
        if ui is None:
            return False
        flags, cuenta, fr = ui[0] >> 3, ui[2], ui[3]
        codigos = pe.rva(fn[2] + 4, 2 * cuenta + 12) or b""
        cod = [struct.unpack_from("<H", codigos, 2 * i)[0] for i in range(cuenta)]
        i = 0
        while i < cuenta:
            c = cod[i]
            off, op, info = c & 0xFF, (c >> 8) & 0xF, c >> 12
            n = ranuras(op, info)
            if primero and off > desp:  # todavía no se ejecutó (estamos en el prólogo)
                i += n
                continue
            if op == 0:  # PUSH_NONVOL
                v = md.q(regs["rsp"])
                if v is None:
                    return False
                regs[REGS[info]] = v
                regs["rsp"] += 8
            elif op == 1:  # ALLOC_LARGE
                if info == 0:
                    regs["rsp"] += cod[i + 1] * 8
                else:
                    regs["rsp"] += cod[i + 1] | (cod[i + 2] << 16)
            elif op == 2:  # ALLOC_SMALL
                regs["rsp"] += info * 8 + 8
            elif op == 3:  # SET_FPREG
                regs["rsp"] = regs[REGS[fr & 0xF]] - (fr >> 4) * 16
            elif op == 4:  # SAVE_NONVOL
                v = md.q(regs["rsp"] + cod[i + 1] * 8)
                if v is not None:
                    regs[REGS[info]] = v
            elif op == 5:  # SAVE_NONVOL_FAR
                v = md.q(regs["rsp"] + (cod[i + 1] | (cod[i + 2] << 16)))
                if v is not None:
                    regs[REGS[info]] = v
            elif op == 10:  # PUSH_MACHFRAME
                b = regs["rsp"] + (8 if info else 0)
                rip_, rsp_ = md.q(b), md.q(b + 24)
                if rip_ is None or rsp_ is None:
                    return False
                regs["rip"], regs["rsp"] = rip_, rsp_
                maquina = True
            i += n
        primero = False
        if flags & 0x4:  # CHAININFO: sigue con la función madre
            k = cuenta + (cuenta & 1)
            fn = struct.unpack_from("<III", codigos, 2 * k) if len(codigos) >= 2 * k + 12 else None
        else:
            fn = None
    if not maquina:
        ret = md.q(regs["rsp"])
        if ret is None:
            return False
        regs["rip"], regs["rsp"] = ret, regs["rsp"] + 8
    return True


def es_retorno(md, dir_):
    """¿`dir_` sigue a una instrucción CALL? (para escanear la pila cuando no se puede desenrollar)."""
    m = md.modulo(dir_)
    if m is None or m.pe is None:
        return False
    b = m.pe.rva(dir_ - m.base - 7, 7)
    if not b:
        return False
    return b[2] == 0xE8 or b[1] == 0xFF and (b[2] & 0x38) == 0x10 or b[5] == 0xFF and (b[6] & 0x38) == 0x10 or \
        b[4] == 0xFF and (b[5] & 0x38) == 0x10 or b[0] == 0xFF and (b[1] & 0x38) == 0x10


def pila(md, regs, extras, microsoft, maximo=80):
    regs = dict(regs)
    marcos = []
    escaneado = False
    for _ in range(maximo):
        rip = regs["rip"]
        if rip == 0:
            break
        marcos.append((rip, escaneado))
        m = md.modulo(rip)
        if m is not None:
            buscar_simbolos(m, extras, microsoft)
        antes = (regs["rip"], regs["rsp"])
        ok = m is not None and m.pe is not None and desenrollar_uno(md, m.pe, m.base, regs)
        if ok and regs["rsp"] > antes[1] and regs["rip"] != 0:
            continue
        # Sin tablas (o se perdió): se busca la próxima dirección de vuelta en la pila.
        escaneado = True
        rsp = antes[1] + (8 if ok else 0)
        for k in range(256):
            v = md.q(rsp + 8 * k)
            if v is None:
                return marcos
            mm = md.modulo(v)
            if mm is not None:
                buscar_simbolos(mm, extras, microsoft)
                if es_retorno(md, v):
                    regs["rip"], regs["rsp"] = v, rsp + 8 * k + 8
                    break
        else:
            break
    return marcos


# --- nombres ------------------------------------------------------------------------------

def simbolizar(md, direcciones):
    """{dir: [(funcion, archivo:linea)…]} con llvm-symbolizer (Docker) y los .pdb; si no, exportes."""
    res = {}
    tmp = Path(tempfile.mkdtemp(prefix="sim-", dir=ALMACEN))
    try:
        lineas = []
        for d in direcciones:
            m = md.modulo(d)
            if m is None or m.pe is None or m.pdb is None:
                continue
            carpeta = tmp / ("%08X%x" % (m.ts, m.tam))
            if not carpeta.exists():
                carpeta.mkdir()
                for src, nom in ((Path(m.pe.ruta), m.nombre), (Path(m.pdb), m.pdb.name)):
                    try:
                        os.link(src, carpeta / nom)
                    except OSError:
                        shutil.copyfile(src, carpeta / nom)
            lineas.append((d, "CODE /x/%s/%s 0x%x" % (carpeta.name, m.nombre, d - m.base)))
        if lineas:
            cmd = ["docker", "run", "--rm", "-i", "--platform", "linux/amd64", "-v", "ba-trabajo:/trabajo:ro",
                   "-v", "%s:/x:ro" % tmp, "ba-compilar", SYMBOLIZER, "--relative-address", "--inlines",
                   "--demangle", "--output-style=JSON"]
            try:
                out = subprocess.run(cmd, input="\n".join(l for _, l in lineas) + "\n", capture_output=True,
                                     text=True, timeout=600).stdout
                for (d, _), j in zip(lineas, out.splitlines()):
                    try:
                        sy = json.loads(j).get("Symbol", [])
                    except json.JSONDecodeError:
                        continue
                    marcos = []
                    for s in sy:
                        f = s.get("FunctionName") or ""
                        if not f or f == "??":
                            continue
                        arch = s.get("FileName") or ""
                        if arch:
                            arch = arch.replace("\\", "/")
                            for corte in ("/broadcast-appliance/", "/trabajo/ba/", "/ba/src/"):
                                if corte in arch:
                                    arch = arch.split(corte, 1)[1]
                            arch = "%s:%s" % (arch, s.get("Line", 0))
                        marcos.append((f, arch))
                    if marcos:
                        res[d] = marcos
            except (subprocess.SubprocessError, OSError) as e:
                print("(sin llvm-symbolizer: %s; van los nombres exportados)" % e, file=sys.stderr)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return res


def nombre(md, d, sim, retorno):
    m = md.modulo(d)
    if m is None:
        return "0x%x" % d
    q = d - 1 if retorno else d
    if q in sim:
        out = []
        for i, (f, arch) in enumerate(sim[q]):
            out.append(("%s!%s" % (m.nombre, f) if i == 0 else "    (en línea) %s" % f) + ("  [%s]" % arch if arch else ""))
        return "\n        ".join(out)
    if m.pe is not None:
        e = m.pe.exporte(d - m.base)
        if e:
            return "%s!%s+0x%x" % (m.nombre, e[0], e[1])
    return "%s+0x%x" % (m.nombre, d - m.base)


# --- el informe ---------------------------------------------------------------------------

def analizar(ruta, extras, microsoft, todos):
    ruta = Path(ruta)
    md = Minidump(ruta)
    info = {}
    j = ruta.with_suffix(".json")
    if j.exists():
        try:
            info = json.loads(j.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            pass
    out = ["=" * 78, "%s" % ruta.name]
    if info:
        out.append("%s de %s, %s — %s" % (info.get("tipo", "?"), info.get("componente", "?"), info.get("hora", "?"),
                                          info.get("causa", "")))
    exc = md.excepcion
    hilos_a_mostrar = []
    if exc:
        code = exc["codigo"]
        m = md.modulo(exc["direccion"])
        out.append("Excepción: 0x%08X (%s) en %s, hilo %d" % (code, CODIGOS.get(code, "?"),
                                                            ("%s+0x%x" % (m.nombre, exc["direccion"] - m.base)) if m else hex(exc["direccion"]),
                                                            exc["hilo"]))
        regs = exc["ctx"] or next((h["ctx"] for h in md.hilos if h["id"] == exc["hilo"]), None)
        if regs:
            hilos_a_mostrar.append(("el hilo que se cayó (%d)" % exc["hilo"], regs))
    if not exc or todos:
        for h in md.hilos:
            if h["ctx"] and (not exc or h["id"] != exc["hilo"]):
                hilos_a_mostrar.append(("hilo %d" % h["id"], h["ctx"]))
    pilas = [(t, pila(md, r, extras, microsoft)) for t, r in hilos_a_mostrar]
    dirs = set()
    for _, ms in pilas:
        for i, (d, _) in enumerate(ms):
            dirs.add(d - 1 if i > 0 else d)
    sim = simbolizar(md, sorted(dirs))
    vistas = {}
    for t, ms in pilas:
        texto = []
        for i, (d, esc) in enumerate(ms):
            texto.append("  #%-2d %s%s" % (i, nombre(md, d, sim, i > 0), "   (pila escaneada)" if esc else ""))
        clave = "\n".join(texto[:6])
        if clave in vistas and not exc:  # hilos iguales (esperando lo mismo): se agrupan
            vistas[clave][0].append(t)
            continue
        vistas[clave] = ([t], texto)
    for clave, (ts, texto) in vistas.items():
        out.append("")
        out.append("— %s%s:" % (ts[0], " (y %d más iguales)" % (len(ts) - 1) if len(ts) > 1 else ""))
        out.extend(texto)
    faltan = sorted({m.nombre for m in md.modulos if m.buscado and (m.pe is None or m.pdb is None) and not m.del_sistema})
    if faltan:
        out.append("")
        out.append("(sin símbolos: %s)" % ", ".join(faltan[:20]))
    if info.get("ultimas_lineas"):
        out.append("")
        out.append("Lo último que dijo el registro:")
        out.extend("  " + l[:220] for l in info["ultimas_lineas"][-25:])
    texto = "\n".join(out)
    try:
        ruta.with_suffix(".txt").write_text(texto + "\n", encoding="utf-8")
    except OSError:
        pass
    return texto


def traer_de_la_pc(host, carpetas):
    """Copia los volcados nuevos (y su .json) de la PC a dist/caidas/<host>/."""
    destino = CAIDAS / host
    destino.mkdir(parents=True, exist_ok=True)
    nuevos = []
    for c in carpetas:
        r = subprocess.run(["ssh", host, 'powershell -NoProfile -Command "Get-ChildItem \'%s\' -File -ErrorAction '
                            'SilentlyContinue | Where-Object { $_.Extension -in \'.dmp\',\'.json\' } | ForEach-Object '
                            '{ $_.Name + \'|\' + $_.Length }"' % c], capture_output=True, text=True, timeout=120)
        for linea in r.stdout.splitlines():
            if "|" not in linea:
                continue
            nom, tam = linea.strip().rsplit("|", 1)
            loc = destino / nom
            if loc.exists() and str(loc.stat().st_size) == tam:
                continue
            subprocess.run(["scp", "-q", "%s:%s/%s" % (host, c.replace("\\", "/"), nom), str(loc)], timeout=600)
            if nom.endswith(".dmp") and loc.exists():
                nuevos.append(loc)
    return sorted(nuevos)


def main():
    ap = argparse.ArgumentParser(description="qué pasó en cada volcado")
    ap.add_argument("rutas", nargs="*", help="volcados (.dmp) o carpetas")
    ap.add_argument("--pc", nargs="?", const="windows", help="traer de la PC (host de ssh; por defecto «windows»)")
    ap.add_argument("--carpeta-pc", action="append",
                    default=None, help="carpetas de volcados en la PC (por defecto E:\\BroadcastAppliance\\Logs\\dumps "
                                       "y C:\\ba-pruebas\\BroadcastAppliance\\Logs\\dumps)")
    ap.add_argument("--binarios", action="append", default=[], help="carpetas extra con los .dll/.exe (un bundle)")
    ap.add_argument("--sin-microsoft", action="store_true", help="no bajar binarios/símbolos de Windows")
    ap.add_argument("--todos-los-hilos", action="store_true")
    ap.add_argument("--todos", action="store_true", help="con --pc: analizar todos, no solo los nuevos")
    a = ap.parse_args()
    ALMACEN.mkdir(parents=True, exist_ok=True)
    rutas = []
    if a.pc:
        carpetas = a.carpeta_pc or [r"E:\BroadcastAppliance\Logs\dumps", r"C:\ba-pruebas\BroadcastAppliance\Logs\dumps"]
        nuevos = traer_de_la_pc(a.pc, carpetas)
        rutas += sorted((CAIDAS / a.pc).glob("*.dmp")) if a.todos else nuevos
        if not rutas:
            print("No hay volcados nuevos en %s." % a.pc)
    for r in a.rutas:
        p = Path(r)
        rutas += sorted(p.glob("*.dmp")) if p.is_dir() else [p]
    for r in rutas:
        try:
            print(analizar(r, a.binarios, not a.sin_microsoft, a.todos_los_hilos))
        except Exception as e:  # uno roto no frena a los demás
            print("%s: no se pudo leer (%s)" % (r, e))


if __name__ == "__main__":
    sys.exit(main())
