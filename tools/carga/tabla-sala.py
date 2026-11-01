#!/usr/bin/env python3
"""tools/carga/tabla-sala.py — la tabla de una «Prueba de sala» por escalones

Lee los resumen.json de medir-sala.py (una carpeta por escalón: <dir>/10, <dir>/20…) y
escribe una tabla en Markdown. Uso: tabla-sala.py <dir> [<dir>…]  (una por versión)
"""
import json, sys
from pathlib import Path


def g(d, *ks):
    for k in ks:
        if not isinstance(d, dict):
            return None
        d = d.get(k)
    return d


def fila(version, r):
    app, gw = g(r, "procesos", "ba-app"), g(r, "procesos", "ba-gateway")
    grilla = r.get("grilla") or []
    vis = sum(x.get("muestra", 0) for x in grilla if isinstance(x, dict))
    mas = sum((x.get("mas") or 0) for x in grilla if isinstance(x, dict))
    red = r.get("red_mbps") or {}
    falt = g(r, "sala", "retornos_faltantes_max_10s") or {}
    return [version, r["participantes"], ("CORTE: " + r["corte"]["motivo"]) if r.get("corte") else "ok",
            r.get("todos_con_medios_s"), r.get("estable_s"),
            g(app, "cpu_pct_regimen", "p50"), g(app, "cpu_pct_regimen", "p95"), g(app, "hilos", "max"),
            g(app, "rss_mb_regimen", "p50"), r.get("footprint_app_mb_max"),
            g(gw, "cpu_pct_regimen", "p50"), g(gw, "hilos", "max"), g(gw, "rss_mb_regimen", "p50"),
            g(r, "cpu_maquina_pct", "p50"), g(r, "cpu_maquina_pct", "picos"), g(r, "gpu", "dispositivo_pct", "p50"),
            g(r, "memoria", "disponible_mb_min"), g(r, "carga", "regimen_max"),
            round(g(red, "ba-gateway", "entra") or 0, 1), round(g(red, "ba-gateway", "sale") or 0, 1),
            f"{vis}+{mas}", falt.get("max"), g(r, "registro_app", "se_demoro"), g(r, "registro_app", "candado_tomado"),
            g(app, "cambios_contexto_s", "p50"), g(gw, "cambios_contexto_s", "p50"),
            g(r, "conectar", "entrar_a_offer_ms", "p95"), g(r, "conectar", "offer_a_medios_ms", "p95"), g(r, "hinchada_union_ms", "p95")]


CAB = ["versión", "N", "guardia", "todos con medios (s)", "estable (s)", "app CPU p50 %", "app CPU p95 %", "app hilos", "app RSS MB",
       "app footprint MB", "gw CPU p50 %", "gw hilos", "gw RSS MB", "máquina CPU p50 %", "picos >80 %", "GPU p50 %",
       "mem. disponible mín MB", "carga máx", "gw entra Mbps", "gw sale Mbps", "grilla (se ven+N)", "retornos faltantes máx/10 s",
       "«se demoró»", "candado tomado", "app csw/s", "gw csw/s", "entrar→offer p95 ms", "offer→medios p95 ms", "hinchada unirse p95 ms"]


def main():
    filas = []
    for d in sys.argv[1:]:
        for sub in sorted(Path(d).iterdir(), key=lambda p: int(p.name) if p.name.isdigit() else 0):
            if sub.is_dir() and (sub / "resumen.json").exists():
                filas.append(fila(Path(d).name, json.loads((sub / "resumen.json").read_text())))
    print("| " + " | ".join(CAB) + " |")
    print("|" + "---|" * len(CAB))
    for f in filas:
        print("| " + " | ".join("" if x is None else str(x) for x in f) + " |")


if __name__ == "__main__":
    main()
