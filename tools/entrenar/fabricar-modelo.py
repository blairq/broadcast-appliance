#!/usr/bin/env python3
"""Fabricar el modelo reactivo de punta a punta: bajar, entrenar, medir, cuantizar, empaquetar.

UNA corrida reproducible y retomable. Cada etapa deja sus archivos en la carpeta
de trabajo y, si ya estan, se saltea (--rehacer ETAPA para forzarla). Todo lo
que define el resultado queda en manifiesto.json: base y revision exacta,
firma del corpus, commit del repo, parametros, tiempos y medidas.

ETAPAS
  inspeccionar  lee SOLO los config y la tarjeta de la base y decide: tamano
              (de ahi el modo de entrenar), atencion lineal, cabeza MTP,
              vision. Si el repo es solo GGUF (los -MTP-GGUF de unsloth) se
              entrena la base que declara.
  descargar   la base de Hugging Face, con la revision fijada (--revision).
  corpus      make corpus-entrenamiento; se guarda una copia y su firma.
  entrenar    entrenar-rapido.py. Elige solo: las dos placas en paralelo si el
              modelo entra en una (cada una hace la mitad del lote), o el
              modelo repartido entre las dos si no (lo decide inspeccionar).
  medir       examen, curva del banco (5 puntos del dial de derivar) y test de
              margen (AUM) sobre todas las semillas.
  exportar    fusionado (bf16); si la base tiene cabeza MTP, se la entrena para
              nuestro modelo (entrenar-mtp.py) y va al GGUF; MLX q4, GGUF bf16 y
              GGUF Q4_K_M calibrado con
              NUESTRO corpus (imatrix: el examen no entra). Ademas el q4 de MLX
              simulado se mide en el banco: cuanto le cuesta cuantizar.
  empaquetar  carpetas con forma de repo de Hugging Face (tarjeta del modelo
              con las medidas, licencia, archivos). NO sube nada: imprime el
              comando para hacerlo.

    tools/entrenar/fabricar-modelo.py --base Qwen/Qwen3.5-4B --nombre reactivo-q35-4b
    tools/entrenar/fabricar-modelo.py ... --hasta medir        # parar antes
    tools/entrenar/fabricar-modelo.py ... --rehacer entrenar   # rehacer desde ahi

NUNCA toca el examen ni el banco de orientacion: solo los lee para medir.
"""
import argparse
import datetime
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
PY = RAIZ / ".venv-entrenar/bin/python"
PY_MLX = RAIZ / ".venv-mlx/bin/python"
E = Path(__file__).resolve().parent
LLAMA = RAIZ / "extern/llamacpp"
CONV = RAIZ / "extern/llamacpp-convertir"
ETAPAS = ["inspeccionar", "descargar", "corpus", "entrenar", "medir", "exportar", "empaquetar"]

# COMO SE ENTRENA CADA BASE en dos RTX 3090 (24 GB) se DECIDE leyendo su config
# (inspeccionar_base, abajo). Medido el 30/09/2026 con entrenar-rapido.py:
#   Qwen3.5-2B  4,3 GB de pesos  paralelo   0,87 s/paso, 15 GB por placa
#   Qwen3.5-4B  8,8 GB           repartido  2,0 s/paso, 16,9 + 21,1 GB
#   Qwen3.5-9B  19 GB            repartido + recalcular (sin medir todavia)
# El limite sale de eso: en paralelo cada placa carga el modelo entero y el
# prefijo de ~3.000 tokens con gradiente; hasta ~5 GB de pesos entra.
PARALELO_HASTA_GB = 5.5
REPARTIDO_HASTA_GB = 12.0


def log(*a):
    print("\033[36m[fabricar]\033[0m", *a, flush=True)


def correr(cmd, env=None, salida=None):
    log("$", " ".join(str(c) for c in cmd)[:220])
    ent = dict(os.environ, **(env or {}))
    if salida:
        with open(salida, "w") as f:
            r = subprocess.run([str(c) for c in cmd], cwd=RAIZ, env=ent, stdout=f, stderr=subprocess.STDOUT)
    else:
        r = subprocess.run([str(c) for c in cmd], cwd=RAIZ, env=ent)
    if r.returncode:
        sys.exit(f"fallo: {' '.join(str(c) for c in cmd)[:200]}" + (f" (ver {salida})" if salida else ""))


def firma(carpeta):
    h = hashlib.sha256()
    for f in sorted(Path(carpeta).glob("*.jsonl")):
        h.update(f.name.encode())
        h.update(f.read_bytes())
    return h.hexdigest()[:16]


def leer_metricas(texto):
    m = {}
    for clave in ("cobertura", "precision", "derivado", "acierto"):
        x = re.search(rf"^{clave}: (\d+)/(\d+)", texto, re.M)
        if x:
            m[clave] = [int(x.group(1)), int(x.group(2))]
    return m


class Fabrica:
    def __init__(self, args):
        self.a = args
        self.dir = Path(args.trabajo).expanduser() / args.nombre
        self.dir.mkdir(parents=True, exist_ok=True)
        self.man_f = self.dir / "manifiesto.json"
        self.man = json.loads(self.man_f.read_text()) if self.man_f.exists() else {
            "nombre": args.nombre, "base": args.base, "creado": datetime.datetime.now().isoformat(timespec="seconds")}
        self.adaptador = self.dir / "adaptador"
        self.corpus = self.dir / "corpus"

    def guardar(self):
        self.man_f.write_text(json.dumps(self.man, ensure_ascii=False, indent=1))

    def hecho(self, etapa, marca):
        if self.a.rehacer and ETAPAS.index(etapa) >= ETAPAS.index(self.a.rehacer):
            return False
        return Path(marca).exists()

    def etapa(self, nombre, fn, marca):
        if self.hecho(nombre, marca):
            log(f"{nombre}: ya estaba")
            return
        t = datetime.datetime.now()
        log(f"=== {nombre} ===")
        fn()
        self.man.setdefault("tiempos_min", {})[nombre] = round((datetime.datetime.now() - t).total_seconds() / 60, 1)
        self.guardar()

    # --- etapas -----------------------------------------------------------
    def inspeccionar(self):
        """Que base es y como se entrena: leyendo SOLO sus config y su tarjeta.

        Los repos que son solo GGUF (p. ej. unsloth/Qwen3.5-4B-MTP-GGUF) no se
        entrenan: se toma la base que declaran (base_model) y se entrena esa."""
        hf = RAIZ / ".venv-entrenar/bin/hf"
        def bajar(repo, *pat):
            cmd = [str(hf), "download", repo] + [x for p in pat for x in ("--include", p)]
            if self.a.revision and repo == self.a.base:
                cmd += ["--revision", self.a.revision]
            r = subprocess.run(cmd, cwd=RAIZ, capture_output=True, text=True)
            if r.returncode:
                sys.exit(f"no se pudo leer {repo}: {r.stderr[-300:]}")
            return Path(r.stdout.strip().splitlines()[-1].split("=", 1)[-1].strip())
        repo = self.a.base
        ruta = bajar(repo, "*.json", "README.md")
        if not (ruta / "config.json").exists():
            tarjeta = (ruta / "README.md").read_text(errors="ignore") if (ruta / "README.md").exists() else ""
            m = re.search(r"^base_model:\s*\n?\s*-?\s*([\w.\-]+/[\w.\-]+)", tarjeta, re.M)
            if not m:
                sys.exit(f"{repo} no tiene config.json ni declara base_model: no se puede entrenar")
            log(f"{repo} es solo para servir (sin config): se entrena su base {m.group(1)}")
            self.man["repo_pedido"] = repo
            self.a.base = repo = m.group(1)
            self.man["base"] = repo
            ruta = bajar(repo, "*.json", "README.md")
        cfg = json.loads((ruta / "config.json").read_text())
        txt = cfg.get("text_config", cfg)
        idx = ruta / "model.safetensors.index.json"
        gb = (json.loads(idx.read_text()).get("metadata", {}).get("total_size", 0) / 1e9) if idx.exists() else 0
        if not gb:  # sin indice: una estimacion por las dimensiones (bf16)
            H, L, I, Vv = (txt.get(k, 0) for k in ("hidden_size", "num_hidden_layers", "intermediate_size", "vocab_size"))
            gb = 2 * (Vv * H + L * (4 * H * H + 3 * H * I)) / 1e9
        modo = "paralelo" if gb <= PARALELO_HASTA_GB else "repartir"
        extra = ["--recalcular"] if gb > REPARTIDO_HASTA_GB else []
        self.man["arquitectura"] = {
            "tipo": cfg.get("model_type"), "pesos_gb": round(gb, 1), "capas": txt.get("num_hidden_layers"),
            "atencion_lineal": "linear_attention" in (txt.get("layer_types") or []),
            "mtp": txt.get("mtp_num_hidden_layers", 0), "vision": "vision_config" in cfg,
            "modo": modo, "extra": extra}
        log("base:", self.man["arquitectura"])
        (self.dir / "inspeccionado").write_text(repo)

    def descargar(self):
        cmd = [RAIZ / ".venv-entrenar/bin/hf", "download", self.a.base]
        if self.a.revision:
            cmd += ["--revision", self.a.revision]
        r = subprocess.run([str(c) for c in cmd], cwd=RAIZ, capture_output=True, text=True)
        if r.returncode:
            sys.exit(f"no se pudo bajar {self.a.base}: {r.stderr[-400:]}")
        # `hf download` termina con la ruta, a veces como «path=/...»
        ruta = Path(r.stdout.strip().splitlines()[-1].split("=", 1)[-1].strip())
        self.man["base_ruta"] = str(ruta)
        self.man["base_revision"] = ruta.name
        lic = ruta / "LICENSE"
        self.man["base_licencia"] = ("apache-2.0" if lic.exists() and "Apache License" in lic.read_text()[:400]
                                     else "ver la base")
        (self.dir / "descargado").write_text(ruta.name)

    def hacer_corpus(self):
        env = {"BA_ETIQUETAS": "0"}
        correr(["make", "corpus-entrenamiento"], env=env, salida=self.dir / "corpus.log")
        if self.corpus.exists():
            shutil.rmtree(self.corpus)
        shutil.copytree(RAIZ / "datos/entrenamiento", self.corpus)
        self.man["corpus_firma"] = firma(self.corpus)
        sys.path.insert(0, str(RAIZ / "tools"))
        import lib_prompt as lp
        self.man["perfil"] = {"nombre": lp.PERFIL, "categorias": lp.REACTIVAS, "entradas": lp.ENTRADAS,
                              "herramientas": lp.herramientas_reactivas(), "prompt": lp.huella()["prompt"]}
        self.man["corpus_ejemplos"] = sum(1 for _ in open(self.corpus / "entrenar.jsonl"))
        self.man["repo_commit"] = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=RAIZ,
                                                 capture_output=True, text=True).stdout.strip()

    def entrenar(self):
        cfg = self.man["arquitectura"]
        comun = ["--base", self.a.base, "--datos", self.corpus, "--salida", self.adaptador,
                 "--epocas", str(self.a.epocas), "--eval-cada", "100", "--semilla", str(self.a.semilla)]
        comun += cfg["extra"]
        env = {"PYTORCH_CUDA_ALLOC_CONF": "expandable_segments:True", "BA_ETIQUETAS": "0"}
        if self.adaptador.exists():
            shutil.rmtree(self.adaptador)
        if cfg["modo"] == "repartir":
            correr([PY, E / "entrenar-rapido.py", "--repartir"] + comun, env=env, salida=self.dir / "entrenar.log")
        else:
            correr([RAIZ / ".venv-entrenar/bin/torchrun", "--nproc_per_node", "2", E / "entrenar-rapido.py"] + comun,
                   env=env, salida=self.dir / "entrenar.log")
        self.man["entrenamiento"] = {"modo": cfg["modo"], "epocas": self.a.epocas, "semilla": self.a.semilla,
                                     "lote": 16, "lr": 2e-4, "lora_r": 32,
                                     "fin": (self.dir / "entrenar.log").read_text().strip().splitlines()[-1]}

    def medir(self, modelo=None, etiqueta="bf16", banco_curva=True, margen=True):
        modelo = modelo or self.adaptador
        md = self.dir / "medidas"
        md.mkdir(exist_ok=True)
        env = {"BA_ETIQUETAS": "0"}
        res = {}
        log(f"examen ({etiqueta})")
        out = subprocess.run([str(PY), str(E / "evaluar.py"), "--modelo", str(modelo),
                              "--salida-casos", str(md / f"{etiqueta}-examen.jsonl")],
                             cwd=RAIZ, env=dict(os.environ, CUDA_VISIBLE_DEVICES="0", **env),
                             capture_output=True, text=True).stdout
        res["examen"] = leer_metricas(out)
        # el test de margen corre en la otra placa mientras tanto
        p_margen = None
        if margen and etiqueta == "bf16":
            cps = sorted(self.adaptador.glob("checkpoint-*"))
            cmd = [str(PY), str(E / "margen-frases.py")]
            for c in cps + [self.adaptador]:
                cmd += ["--modelo", str(c)]
            cmd += ["--salida", str(md / "margen.json")]
            p_margen = subprocess.Popen(cmd, cwd=RAIZ, env=dict(os.environ, CUDA_VISIBLE_DEVICES="1", **env),
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        res["banco"] = {}
        for s in (["-4", "-2", "0", "2", "4"] if banco_curva else ["0"]):
            log(f"banco ({etiqueta}) sesgo {s}")
            cmd = [str(PY), str(E / "evaluar.py"), "--modelo", str(modelo), "--casos",
                   str(RAIZ / "datos/orientacion.jsonl"), "--sesgo", s]
            if s == "0":
                cmd += ["--salida-casos", str(md / f"{etiqueta}-banco.jsonl")]
            out = subprocess.run(cmd, cwd=RAIZ, env=dict(os.environ, CUDA_VISIBLE_DEVICES="0", **env),
                                 capture_output=True, text=True).stdout
            res["banco"][s] = leer_metricas(out)
        if p_margen:
            out = p_margen.communicate()[0]
            x = re.search(r"margen negativo.*?: (\d+) · margen < 2: (\d+)", out)
            res["margen"] = {"negativo": int(x.group(1)), "menor_a_2": int(x.group(2))} if x else out[-300:]
        self.man.setdefault("medidas", {})[etiqueta] = res
        self.guardar()
        if etiqueta == "bf16":
            (self.dir / "medido").write_text("ok")

    def exportar(self):
        ex = self.dir / "export"
        ex.mkdir(exist_ok=True)
        # fusionado + MLX q4 (exportar-mlx.py hace las dos cosas)
        if not PY_MLX.exists():
            correr([E / "venv-mlx.sh"])
        correr([PY, E / "exportar-mlx.py", "--modelo", self.adaptador, "--salida", ex / "modelo",
                "--bits", "4", "--grupo", "64"], env={"CUDA_VISIBLE_DEVICES": "0"}, salida=self.dir / "exportar-mlx.log")
        hf, mlx = ex / "modelo-hf", ex / "modelo-q4"
        # GGUF: convertir a bf16 y cuantizar calibrando con nuestro corpus
        if not (CONV / "convert_hf_to_gguf.py").exists():
            correr([RAIZ / "tools/deps/llamacpp-convertir.sh"])
        if not (LLAMA / "llama-quantize").exists():
            correr([RAIZ / "tools/deps/llamacpp.sh"])
        gg = ex / "gguf"
        gg.mkdir(exist_ok=True)
        cfg = json.loads((hf / "config.json").read_text())
        hf_conv = ex / "modelo-hf-conv"  # el conversor quiere el model_type de transformers
        if hf_conv.exists():
            shutil.rmtree(hf_conv)
        shutil.copytree(hf, hf_conv)
        if cfg.get("model_type") == "qwen3_5":
            cfg["model_type"] = "qwen3_5_text"
            (hf_conv / "config.json").write_text(json.dumps(cfg, indent=2))
        # LA CABEZA MTP. El config de Qwen3.5 declara una capa de multi-token
        # prediction y el conversor la espera (blk.N), pero transformers la
        # descarta al cargar y el modelo fusionado se guarda sin ella:
        # llama.cpp no lo carga. Se copia la ORIGINAL de la base (no la
        # entrenamos): completa el GGUF y deja disponible la decodificacion
        # especulativa (--spec-type draft-mtp).
        if cfg.get("mtp_num_hidden_layers"):
            from safetensors import safe_open
            from safetensors.torch import save_file
            # la cabeza ENTRENADA para nuestro modelo (entrenar-mtp.py), o la de
            # la base si se pidio --sin-mtp
            mtp_f = ex / "mtp.safetensors"
            if not self.a.sin_mtp:
                cmd = [PY, E / "entrenar-mtp.py", "--modelo", hf, "--base-ruta", self.man["base_ruta"],
                       "--datos", self.corpus, "--salida", mtp_f]
                if self.man["arquitectura"]["modo"] == "repartir":
                    cmd.append("--repartir")
                correr(cmd, salida=self.dir / "mtp.log")
                self.man["mtp"] = json.loads(mtp_f.with_suffix(".json").read_text())
                fuentes = [mtp_f]
            else:
                fuentes = sorted(Path(self.man["base_ruta"]).glob("*.safetensors"))
            mtp = {}
            for f in fuentes:
                with safe_open(str(f), "pt") as st:
                    for k in st.keys():
                        if k.startswith("mtp."):
                            mtp[k] = st.get_tensor(k)
            if mtp:
                save_file(mtp, str(hf_conv / "model-mtp.safetensors"), metadata={"format": "pt"})
                idx = hf_conv / "model.safetensors.index.json"
                if idx.exists():
                    ix = json.loads(idx.read_text())
                    ix["weight_map"].update({k: "model-mtp.safetensors" for k in mtp})
                    idx.write_text(json.dumps(ix, indent=2))
                log(f"cabeza MTP ({'entrenada' if not self.a.sin_mtp else 'de la base'}): {len(mtp)} tensores")
        bf16 = gg / f"{self.a.nombre}-bf16.gguf"
        correr([PY, CONV / "convert_hf_to_gguf.py", hf_conv, "--outtype", "bf16", "--outfile", bf16],
               env={"PYTHONPATH": str(CONV / "gguf-py")}, salida=self.dir / "gguf.log")
        shutil.rmtree(hf_conv)
        calib = gg / "calibracion.txt"
        filas = [json.loads(l) for l in open(self.corpus / "entrenar.jsonl")]
        vistas, textos = set(), []
        for f in filas:  # una por herramienta y despues variado: el examen NO entra
            if f["herramienta"] in vistas and len(textos) > 150:
                continue
            vistas.add(f["herramienta"])
            textos.append(f["prompt"] + f["completion"])
        calib.write_text("\n\n".join(textos), encoding="utf-8")
        env = {"LD_LIBRARY_PATH": str(LLAMA)}
        correr([LLAMA / "llama-imatrix", "-m", bf16, "-f", calib, "-o", gg / "imatrix.dat", "--chunks", "64",
                "-t", str(os.cpu_count() or 8)], env=env, salida=self.dir / "imatrix.log")
        q4 = gg / f"{self.a.nombre}-Q4_K_M.gguf"
        correr([LLAMA / "llama-quantize", "--imatrix", gg / "imatrix.dat", bf16, q4, "Q4_K_M"],
               env=env, salida=self.dir / "quantize.log")
        self.man["exportado"] = {"mlx_q4": str(mlx), "gguf_q4": str(q4), "gguf_bf16": str(bf16),
                                 "tam_gb": {"mlx_q4": round(sum(f.stat().st_size for f in mlx.rglob("*") if f.is_file()) / 2**30, 2),
                                            "gguf_q4": round(q4.stat().st_size / 2**30, 2)}}
        # cuanto cuesta el q4 de MLX: sus pesos exactos, medidos en el banco
        sim = ex / "modelo-q4-simulado"
        correr([PY_MLX, E / "simular-q4-mlx.py", "--hf", hf, "--salida", sim], salida=self.dir / "simular.log")
        self.medir(modelo=sim, etiqueta="mlx-q4", banco_curva=self.a.curva_q4, margen=False)
        shutil.rmtree(sim)
        (self.dir / "exportado").write_text("ok")

    def empaquetar(self):
        rep = self.dir / "repos"
        rep.mkdir(exist_ok=True)
        ex = self.man["exportado"]
        med = self.man.get("medidas", {})

        def fila(et, s="0"):
            b = med.get(et, {}).get("banco", {}).get(s, {})
            x = med.get(et, {}).get("examen", {})
            pct = lambda v: f"{100*v[0]/v[1]:.1f}%" if v else "-"
            return (f"| {et} | {pct(b.get('cobertura'))} | {pct(b.get('precision'))} | {pct(b.get('derivado'))} "
                    f"| {pct(x.get('acierto'))} |")

        def tarjeta(tipo, archivos):
            base = self.a.base
            return f"""---
license: {self.man.get('base_licencia', 'apache-2.0')}
base_model: {base}
language: [es]
pipeline_tag: text-generation
tags: [tool-selection, broadcast, {tipo}, qwen3.5]
---
# {self.a.nombre} ({tipo})

Modelo chico que, por cada pedido del operador de un estudio de TV en vivo,
elige la herramienta del aparato que lo resuelve (las del perfil, abajo) o
contesta `derivar` para pasarlo al asistente grande. Contesta SOLO el nombre.

- Base: `{base}` (revision `{self.man.get('base_revision', '?')}`), ajuste LoRA r=32
  fusionado. Solo texto.
- Perfil `{self.man.get('perfil', {}).get('nombre')}`: {len(self.man.get('perfil', {}).get('herramientas', []))} herramientas
  de las categorias {', '.join(self.man.get('perfil', {}).get('categorias', []))}.
- Corpus: firma `{self.man.get('corpus_firma')}`, {self.man.get('corpus_ejemplos')} ejemplos;
  repo en `{self.man.get('repo_commit')}`.
- El prompt tiene que ser EXACTAMENTE el de entrenamiento (fichas de las
  herramientas): ver `huella-prompt.json`. Otro prompt, otro modelo.

## Medidas (banco de orientacion, 1.423 frases, sesgo 0)

| version | cobertura | precision | derivado | examen |
|---|---|---|---|---|
{fila('bf16')}
{fila('mlx-q4')}

cobertura = de lo suyo, cuanto atendio solo; precision = de lo que atendio,
cuanto acerto; derivado = de lo ajeno, cuanto paso al grande.

{"## Cabeza MTP" + chr(10) + chr(10) + f"Entrenada para este modelo: aceptacion {100*self.man['mtp']['aceptacion_despues']:.1f}% (la original {100*self.man['mtp']['aceptacion_antes']:.1f}%). llama.cpp: `--spec-type draft-mtp`." + chr(10) if self.man.get("mtp") else ""}
## Archivos

{archivos}
"""
        # MLX q4
        d = rep / f"{self.a.nombre}-mlx-q4"
        if d.exists():
            shutil.rmtree(d)
        shutil.copytree(ex["mlx_q4"], d)
        (d / "README.md").write_text(tarjeta("mlx", "Cargar con `mlx_lm.load(...)`; 4 bits, grupos de 64."))
        # GGUF
        g = rep / f"{self.a.nombre}-GGUF"
        if g.exists():
            shutil.rmtree(g)
        g.mkdir()
        shutil.copy(ex["gguf_q4"], g)
        hp = Path(ex["mlx_q4"]) / "huella-prompt.json"
        for dd in (d, g):
            if hp.exists():
                shutil.copy(hp, dd)
            lic = Path(self.man.get("base_ruta", "")) / "LICENSE"
            if lic.exists():
                shutil.copy(lic, dd / "LICENSE")
            (dd / ".gitattributes").write_text("*.safetensors filter=lfs diff=lfs merge=lfs -text\n"
                                               "*.gguf filter=lfs diff=lfs merge=lfs -text\n")
        (g / "README.md").write_text(tarjeta("gguf", f"`{Path(ex['gguf_q4']).name}`: Q4_K_M calibrado con el corpus"
                                                      " (imatrix). llama.cpp b11255 o posterior."))
        self.man["repos"] = [str(d), str(g)]
        (self.dir / "empaquetado").write_text("ok")
        log("listo para subir (NO se subio nada):")
        for dd in (d, g):
            log(f"  .venv-entrenar/bin/hf upload <usuario>/{dd.name} {dd} .")

    def correr(self):
        hasta = ETAPAS.index(self.a.hasta)
        plan = [("inspeccionar", self.inspeccionar, self.dir / "inspeccionado"),
                ("descargar", self.descargar, self.dir / "descargado"),
                ("corpus", self.hacer_corpus, self.corpus / "entrenar.jsonl"),
                ("entrenar", self.entrenar, self.adaptador / "adapter_config.json"),
                ("medir", self.medir, self.dir / "medido"),
                ("exportar", self.exportar, self.dir / "exportado"),
                ("empaquetar", self.empaquetar, self.dir / "empaquetado")]
        for i, (n, fn, marca) in enumerate(plan):
            if i > hasta:
                break
            self.etapa(n, fn, marca)
        log(f"manifiesto: {self.man_f}")
        b = self.man.get("medidas", {}).get("bf16", {}).get("banco", {}).get("0")
        if b:
            log("banco (sesgo 0):", {k: f"{v[0]}/{v[1]}" for k, v in b.items()})


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", required=True, help="p. ej. Qwen/Qwen3.5-4B")
    ap.add_argument("--revision", help="commit del repo de la base (por defecto el ultimo, y se anota)")
    ap.add_argument("--nombre", required=True)
    ap.add_argument("--perfil", default="reactivo",
                    help="sobre que herramientas del MCP se entrena: datos/perfiles/<perfil>.json")
    ap.add_argument("--trabajo", default="~/entrenamientos/fabrica")
    ap.add_argument("--epocas", type=float, default=2.0)
    ap.add_argument("--semilla", type=int, default=7)
    ap.add_argument("--hasta", choices=ETAPAS, default="empaquetar")
    ap.add_argument("--rehacer", choices=ETAPAS, help="rehacer desde esta etapa (las anteriores se reusan)")
    ap.add_argument("--sin-mtp", action="store_true", help="no entrenar la cabeza MTP (usar la de la base)")
    ap.add_argument("--curva-q4", action="store_true", help="medir el q4 en los 5 puntos del dial (si no, solo sesgo 0)")
    args = ap.parse_args()
    # el perfil viaja por el entorno a TODO lo que corre debajo (corpus,
    # entrenamiento, evaluacion): lib_prompt lo lee de BA_PERFIL
    os.environ["BA_PERFIL"] = args.perfil
    Fabrica(args).correr()


if __name__ == "__main__":
    main()
