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
  empaquetar  los repos de Hugging Face (MLX q4 y GGUF): tarjeta del modelo
              (que hace, como usarlo, curva del banco, examen, costo de
              cuantizar, cabeza MTP, perfil y herramientas), licencia de la
              base, firma del prompt y, si hay etiquetas, equivalencias.json.
              Calcula lo que necesita aunque se reusen etapas.
  subir       solo con --subir --hf-usuario X; privados salvo --publico.

LOS -MTP-GGUF DE UNSLOTH (unsloth/Qwen3.5-4B-MTP-GGUF, -9B-MTP-GGUF) no traen
pesos para entrenar: son cuantizaciones de Qwen/Qwen3.5-4B y -9B con la cabeza
MTP adentro. Pedidos como --base, se entrena todo sobre su base (el modelo y la
cabeza MTP) y lo que se toma de ellos es la RECETA de cuantizacion «UD» (que
tipo lleva cada tensor), leida de la cabecera de su UD-Q4_K_XL sin bajarlo
entero. Se aplica a NUESTROS pesos con NUESTRA imatrix: el GGUF sale como
<repo>-UD-Q4_K_XL.gguf.

ESPACIO EN DISCO. Antes de cada etapa se calcula lo que va a ocupar (por el
tamano de la base) y si no entra con un margen de 10 GB, para y dice cuanto
falta. Al terminar exportar se borran los intermedios grandes (fusionado bf16
y GGUF bf16; --conservar para dejarlos) y los repos usan enlaces duros a los
archivos exportados: no se duplican gigas.

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
ETAPAS = ["inspeccionar", "descargar", "corpus", "entrenar", "medir", "exportar", "empaquetar", "subir"]

# COMO SE ENTRENA CADA BASE en dos RTX 3090 (24 GB) se DECIDE leyendo su config
# (inspeccionar_base, abajo). Medido el 30/09/2026 con entrenar-rapido.py:
#   Qwen3.5-2B  4,3 GB de pesos  paralelo   0,87 s/paso, 15 GB por placa
#   Qwen3.5-4B  8,8 GB           repartido  2,0 s/paso, 16,9 + 21,1 GB
#   Qwen3.5-9B  19 GB            repartido + recalcular (sin medir todavia)
# El limite sale de eso: en paralelo cada placa carga el modelo entero y el
# prefijo de ~3.000 tokens con gradiente; hasta ~5 GB de pesos entra.
PARALELO_HASTA_GB = 5.5
REPARTIDO_HASTA_GB = 12.0
MARGEN_GB = 10.0
RECETA_UNSLOTH = "UD-Q4_K_XL"


def libre_gb(p):
    return shutil.disk_usage(p).free / 1e9


def enlazar(src, dst):
    """Enlace duro para lo grande (no duplica gigas); copia para lo chico, que
    despues se puede sobrescribir (README) sin tocar el original."""
    if os.path.getsize(src) > 50 * 2**20:
        try:
            os.link(src, dst)
            return dst
        except OSError:
            pass
    return shutil.copy2(src, dst)


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
        if self.man.get("repo_pedido") == args.base:
            args.base = self.man["base"]  # se retoma: lo que se entrena es su base
        self.adaptador = self.dir / "adaptador"
        self.corpus = self.dir / "corpus"

    def guardar(self):
        self.man_f.write_text(json.dumps(self.man, ensure_ascii=False, indent=1))

    def hecho(self, etapa, marca):
        if self.a.rehacer and ETAPAS.index(etapa) >= ETAPAS.index(self.a.rehacer):
            return False
        return Path(marca).exists()

    def base_en_cache(self):
        try:
            from huggingface_hub import snapshot_download
            snapshot_download(self.a.base, revision=self.a.revision, local_files_only=True)
            return True
        except Exception:
            return False

    def necesita_gb(self, etapa):
        """Lo que la etapa deja en disco (en el pico), por el tamano de la base."""
        gb = self.man.get("arquitectura", {}).get("pesos_gb", 0)
        return {"descargar": 0 if self.base_en_cache() else gb * 1.1 + 1,
                "corpus": 0.5, "entrenar": 3, "medir": 1,
                # en el pico: fusionado bf16 + GGUF bf16 + q4 simulado (+ los cuantizados)
                "exportar": gb * 3.4 + 2, "empaquetar": 0.2}.get(etapa, 0)

    def controlar_espacio(self, etapas):
        """Para ANTES de empezar si no entra: mejor que morir a mitad de un GGUF."""
        pico = max((self.necesita_gb(e) for e in etapas), default=0)
        if "descargar" in etapas and "exportar" in etapas:
            pico = max(pico, self.necesita_gb("descargar") + self.necesita_gb("exportar"))
        libre = libre_gb(self.dir)
        log(f"disco: {libre:.0f} GB libres, se necesitan ~{pico:.0f} GB (+{MARGEN_GB:.0f} de margen)")
        if libre < pico + MARGEN_GB:
            grandes = sorted(((sum(f.stat().st_size for f in d.rglob("*") if f.is_file()) / 1e9, d)
                              for d in self.dir.parent.glob("*/export")), reverse=True)[:5]
            sys.exit(f"[fabricar] no hay espacio: faltan ~{pico + MARGEN_GB - libre:.0f} GB.\n"
                     + "".join(f"  {g:.0f} GB en {d}\n" for g, d in grandes)
                     + "Liberar espacio (p. ej. exportados de otras corridas ya subidas) y volver a correr.")

    def etapa(self, nombre, fn, marca):
        if self.hecho(nombre, marca):
            log(f"{nombre}: ya estaba")
            return
        self.controlar_espacio([nombre])
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
                              "herramientas": lp.herramientas_reactivas(), "prompt": lp.huella()["prompt"],
                              # el juego de etiquetas: lo que el modelo ESCRIBE por cada herramienta
                              "etiquetas": os.environ.get("BA_ETIQUETAS_JUEGO") if lp.USAR_ETIQUETAS else None}
        if lp.USAR_ETIQUETAS:
            self.man["perfil"]["equivalencias"] = {lp.salida(h): h for h in lp.nombres_posibles()}
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
        shutil.copytree(hf, hf_conv, copy_function=enlazar)  # enlaces: solo cambia el config
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
                if not self.a.sin_mtp:
                    self.mtp_a_mlx(mtp_f, mlx)
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
        receta = self.receta_unsloth(bf16, gg) if self.man.get("repo_pedido") else None
        cuant = RECETA_UNSLOTH if receta else "Q4_K_M"
        q4 = gg / f"{self.a.nombre}-{cuant}.gguf"
        cmd = [LLAMA / "llama-quantize", "--imatrix", gg / "imatrix.dat"]
        if receta:
            cmd += ["--tensor-type-file", receta]
        correr(cmd + [bf16, q4, "Q4_K_M"], env=env, salida=self.dir / "quantize.log")
        self.man["exportado"] = {"mlx_q4": str(mlx), "gguf_q4": str(q4), "gguf_bf16": str(bf16), "cuant": cuant,
                                 "tam_gb": {"mlx_q4": round(sum(f.stat().st_size for f in mlx.rglob("*") if f.is_file()) / 2**30, 2),
                                            "gguf_q4": round(q4.stat().st_size / 2**30, 2)}}
        # cuanto cuesta el q4 de MLX: sus pesos exactos, medidos en el banco
        sim = ex / "modelo-q4-simulado"
        correr([PY_MLX, E / "simular-q4-mlx.py", "--hf", hf, "--salida", sim], salida=self.dir / "simular.log")
        self.medir(modelo=sim, etiqueta="mlx-q4", banco_curva=self.a.curva_q4, margen=False)
        shutil.rmtree(sim)
        if not self.a.conservar:  # los intermedios grandes: se rehacen con --rehacer exportar
            shutil.rmtree(hf, ignore_errors=True)
            bf16.unlink(missing_ok=True)
            self.man["exportado"]["gguf_bf16"] = None
            log(f"intermedios borrados (fusionado y GGUF bf16); libres {libre_gb(self.dir):.0f} GB")
        (self.dir / "exportado").write_text("ok")

    @staticmethod
    def mtp_a_mlx(mtp_f, mlx):
        """La cabeza MTP en el modelo MLX, como ARCHIVOS APARTE, fuera del indice:
        mtp.safetensors (fp16) y mtp-q8.safetensors (8 bits). Ver mtp-mlx.py.

        No va dentro de model*.safetensors a proposito: mlx-lm (0.31.3,
        qwen3_5.sanitize) toma la presencia de CUALQUIER peso mtp.* como senal de
        un checkpoint sin convertir y le suma 1 a todas las normas: el modelo ya
        convertido quedaria roto sin avisar. mlx-lm solo lee model*.safetensors,
        asi que estos archivos no los toca; un motor con MTP los carga aparte.
        fp16 y no bf16: las GPU de M1/M2 no tienen bf16 nativo."""
        correr([PY_MLX, E / "mtp-mlx.py", "--entrada", mtp_f, "--salida", mlx])

    def receta_unsloth(self, bf16, gg):
        """La receta UD de unsloth: que tipo lleva cada tensor en su UD-Q4_K_XL.

        Solo hace falta la CABECERA del GGUF (nombres, formas y tipos; los pesos
        vienen despues): se baja el principio con un pedido por rango a un
        archivo disperso del tamano real y gguf-py lo lee como si estuviera
        entero. Sale un archivo para llama-quantize --tensor-type-file, con el
        nombre exacto de cada tensor anclado (^...$: la busqueda es regex y
        «blk.1.» tambien encontraria «blk.10.»)."""
        import urllib.request
        sys.path.insert(0, str(CONV / "gguf-py"))
        from gguf import GGUFReader, GGMLQuantizationType as T
        repo = self.man["repo_pedido"]
        info = json.loads(urllib.request.urlopen(f"https://huggingface.co/api/models/{repo}?blobs=true").read())
        arch = [s for s in info["siblings"] if s["rfilename"].endswith(f"-{RECETA_UNSLOTH}.gguf")]
        if not arch:
            sys.exit(f"{repo} no tiene un {RECETA_UNSLOTH}.gguf para copiar la receta")
        nom, tam = arch[0]["rfilename"], arch[0]["size"]
        cab = gg / f"cabecera-{nom}"
        for mb in (64, 256):
            req = urllib.request.Request(f"https://huggingface.co/{repo}/resolve/main/{nom}",
                                         headers={"Range": f"bytes=0-{mb * 2**20 - 1}"})
            datos = urllib.request.urlopen(req).read()
            with open(cab, "wb") as f:
                f.truncate(tam)
                f.write(datos)
            try:
                suyos = {t.name: t.tensor_type for t in GGUFReader(cab).tensors}
                break
            except Exception as e:  # la cabecera no entro en los primeros MB
                suyos, err = None, e
        cab.unlink()
        if suyos is None:
            sys.exit(f"no se pudo leer la cabecera de {nom}: {err}")
        nuestros = [t.name for t in GGUFReader(bf16).tensors]
        texto = {T.F32: None, T.F16: "f16", T.BF16: "bf16"}
        lineas, faltan, tipos = [], [], {}
        for n in nuestros:
            if n not in suyos:
                faltan.append(n)
                continue
            t = suyos[n]
            nombre_t = texto[t] if t in texto else t.name.lower()
            if nombre_t is None:  # F32 (normas, sesgos): llama-quantize no los toca
                continue
            lineas.append(f"^{re.escape(n)}$={nombre_t}")
            tipos[nombre_t] = tipos.get(nombre_t, 0) + 1
        receta = gg / "receta-unsloth.txt"
        receta.write_text("\n".join(lineas) + "\n")
        self.man["receta_unsloth"] = {"repo": repo, "archivo": nom, "tensores": len(lineas),
                                      "sin_par": faltan[:20], "sin_par_total": len(faltan), "tipos": tipos}
        log(f"receta {RECETA_UNSLOTH} de {repo}: {len(lineas)} tensores {tipos}"
            + (f"; {len(faltan)} sin par (van por Q4_K_M): {faltan[:5]}" if faltan else ""))
        return receta

    # --- empaquetar: el repo de Hugging Face, completo aunque se reusen etapas --------
    def completar_datos(self):
        """Lo que la tarjeta necesita, calculado aca: no depende de que las etapas
        anteriores hayan corrido en esta corrida (una fabrica retomada las saltea)."""
        sys.path.insert(0, str(RAIZ / "tools"))
        import lib_prompt as lp
        if self.corpus.exists():
            self.man["corpus_firma"] = firma(self.corpus)
            self.man["corpus_ejemplos"] = sum(1 for _ in open(self.corpus / "entrenar.jsonl"))
        p = self.man.get("perfil") or {}
        if not p.get("herramientas"):
            p = {"nombre": lp.PERFIL, "categorias": lp.REACTIVAS, "entradas": lp.ENTRADAS,
                 "herramientas": lp.herramientas_reactivas(), "prompt": lp.huella()["prompt"],
                 "etiquetas": os.environ.get("BA_ETIQUETAS_JUEGO") if lp.USAR_ETIQUETAS else None}
            if lp.USAR_ETIQUETAS:
                p["equivalencias"] = {lp.salida(h): h for h in lp.nombres_posibles()}
            self.man["perfil"] = p
        base_ruta = Path(self.man.get("base_ruta", "").split("=", 1)[-1])
        self.man["base_ruta"] = str(base_ruta)
        lic = base_ruta / "LICENSE"
        self.man["base_licencia"] = ("apache-2.0" if lic.exists() and "Apache License" in lic.read_text()[:400]
                                     else "other")
        if not self.man.get("repo_commit"):
            self.man["repo_commit"] = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=RAIZ,
                                                     capture_output=True, text=True).stdout.strip()
        self.guardar()

    def tarjeta(self, formato, repo):
        m, a = self.man, self.a
        p, med = m["perfil"], m.get("medidas", {})
        pct = lambda v: f"{100 * v[0] / v[1]:.1f}%" if v else "–"

        def fila(et, s):
            b = med.get(et, {}).get("banco", {}).get(s, {})
            return f"| {s} | {pct(b.get('cobertura'))} | {pct(b.get('precision'))} | {pct(b.get('derivado'))} |"
        curva = "\n".join(fila("bf16", s) for s in ["-4", "-2", "0", "2", "4"] if med.get("bf16", {}).get("banco", {}).get(s))
        ex16, exq = med.get("bf16", {}).get("examen", {}), med.get("mlx-q4", {}).get("examen", {})
        b16, bq = med.get("bf16", {}).get("banco", {}).get("0", {}), med.get("mlx-q4", {}).get("banco", {}).get("0", {})
        mtp = m.get("mtp")
        etq = p.get("etiquetas")
        contesta = (f"las etiquetas del juego «{etq}»: traducir con `equivalencias.json` (etiqueta → herramienta del MCP)"
                    " antes de ejecutar" if etq else "el nombre real de la herramienta del MCP (o `derivar`)")
        herramientas = ", ".join(f"`{h}`" for h in p.get("herramientas", []))
        if formato == "mlx":
            uso = f"""```python
from mlx_lm import load
modelo, tok = load("{a.hf_usuario or '<usuario>'}/{repo}")
# prompt = el MISMO de entrenamiento: fichas de las herramientas + el pedido (ver abajo)
```
Elegir con decodificación restringida a los nombres posibles (como `tools/entrenar/evaluar-mlx.py`
del repo del aparato): el prefijo con las fichas se procesa una vez y se reusa entre pedidos."""
            archivos = ("`model.safetensors`: MLX, 4 bits (afín, grupos de 64). Solo texto (sin la parte de visión)."
                        + ("\n`mtp.safetensors` (fp16) y `mtp-q8.safetensors` (lineales en 8 bits, afín, grupos de 64):"
                           " la cabeza MTP entrenada, en dos variantes para medir (las GPU M1/M2 no tienen bf16 nativo)."
                           " Nombres `mtp.*` y normas en la convención de Hugging Face (sin el +1 que mlx-lm aplica al"
                           " convertir). Van aparte a propósito: si un peso `mtp.*` estuviera en `model*.safetensors`,"
                           " mlx-lm 0.31 lo tomaría como un checkpoint sin convertir y correría las normas del modelo."
                           " Un motor con MTP los carga desde estos archivos." if mtp else ""))
        else:
            uso = f"""```bash
llama-server -m {self.gguf_nombre} -c 8192 --cache-reuse 256{' --spec-type draft-mtp --spec-draft-n-max 4' if mtp else ''}
```
llama.cpp b11255 o posterior (arquitectura `qwen35`, con atención lineal). Restringir la salida con
una gramática a los nombres posibles."""
            ru = m.get("receta_unsloth")
            cuant = (f"{RECETA_UNSLOTH} (la receta «UD» de [{ru['repo']}](https://huggingface.co/{ru['repo']}):"
                     f" el mismo tipo por tensor que su `{ru['archivo']}`, aplicado a estos pesos)") if ru else "Q4_K_M"
            archivos = (f"`{self.gguf_nombre}`: {cuant}, calibrado con imatrix sobre el propio"
                        f" corpus (sin el examen){' e incluye la cabeza MTP entrenada' if mtp else ''}.")
        return f"""---
license: {m['base_licencia']}
base_model: {m.get('base')}
language: [es]
library_name: {'mlx' if formato == 'mlx' else 'gguf'}
pipeline_tag: text-generation
tags: [tool-selection, function-calling, broadcast, live-tv, spanish, qwen3.5{', mlx' if formato == 'mlx' else ', gguf, llama.cpp'}{', mtp' if mtp and formato != 'mlx' else ''}]
---
# {repo}

**ES** · Modelo chico para un aparato de producción de TV en vivo: por cada pedido del operador de
cabina («bajale al mic 2», «take a la armada», «¿cuántas personas se ven en el programa?») elige
**qué herramienta del aparato ejecutar**, o contesta `derivar` para pasarle el pedido al asistente
grande. No genera texto ni argumentos: contesta solo {contesta}.

**EN** · Small model for a live-TV production appliance: for each operator request (Rioplatense
Spanish) it picks **which tool of the appliance to run** among {len(p.get('herramientas', []))}, or answers `derivar` to hand the
request to a larger assistant. It outputs only the tool name.

## Medidas

Banco de orientación: 1.423 frases escritas por agentes independientes (no se usan para entrenar).
*cobertura*: de lo que es suyo, cuánto atiende solo · *precisión*: de lo que atiende, cuánto acierta
(lo que se ve al aire) · *derivado*: de lo ajeno, cuánto pasa al grande. El sesgo mueve el punto de
operación sumándole al logit de `derivar`.

| sesgo | cobertura | precisión | derivado |
|---|---|---|---|
{curva}

| | bf16 | {'MLX q4' if bq else ''} |
|---|---|---|
| precisión (sesgo 0) | {pct(b16.get('precision'))} | {pct(bq.get('precision'))} |
| derivado (sesgo 0) | {pct(b16.get('derivado'))} | {pct(bq.get('derivado'))} |
| examen (105 casos) | {pct(ex16.get('acierto'))} | {pct(exq.get('acierto'))} |

{"La cabeza MTP (multi-token prediction) se reentrenó para este modelo: acierta el token siguiente en el " + f"{100 * mtp['aceptacion_despues']:.1f}% de los casos de validación (la original, {100 * mtp['aceptacion_antes']:.1f}%), así que la decodificación especulativa casi siempre acepta el borrador." if mtp else ""}

## Uso

{uso}

**El prompt tiene que ser exactamente el de entrenamiento**: el system con las fichas de las
herramientas del perfil, el pedido como turno de usuario y el turno del asistente con el bloque de
razonamiento vacío (`<think>\\n\\n</think>\\n\\n`). Su firma está en `huella-prompt.json` (`{p.get('prompt')}`):
con otras fichas es otro modelo, aunque siga contestando.

## Entrenamiento

- Base: [`{m.get('base')}`](https://huggingface.co/{m.get('base')}) (revisión `{m.get('base_revision', '?')[:12]}`), LoRA r=32 sobre todas
  las lineales (también las de atención lineal), fusionado. {m.get('entrenamiento', {}).get('epocas', 2)} épocas, lote 16, lr 2e-4 coseno,
  pérdida solo sobre la respuesta.
- Perfil `{p.get('nombre')}`: {len(p.get('herramientas', []))} herramientas de las categorías {', '.join(p.get('categorias', []))}. Lo que no es de
  ellas va a `derivar`.
- Corpus: {m.get('corpus_ejemplos')} ejemplos (firma `{m.get('corpus_firma')}`), generado desde frases semilla curadas con un bucle
  que mide la separación entre herramientas (CDNV, Frobenius), el margen del decodificador (AUM) y
  revisa el sentido de cada frase contra la ficha real de la herramienta. Código del aparato: commit
  `{m.get('repo_commit')}`.

## Limitaciones

- Elige la herramienta; los argumentos los completa el aparato o el asistente grande.
- Entrenado para castellano rioplatense de cabina; otros registros no están medidos.
- Fuera del perfil no hace nada útil: lo deriva.

## Herramientas del perfil

{herramientas}

## Archivos

{archivos} `huella-prompt.json`: firma del prompt. `LICENSE`: la de la base.
"""

    def prompt_para_servir(self):
        """Todo lo que necesita quien SIRVE el modelo, sin reconstruir el prompt:
        la app (C++) y el servidor MLX lo leen tal cual. Es el MISMO de
        entrenamiento: con otras fichas seria otro modelo (ver huella)."""
        sys.path.insert(0, str(RAIZ / "tools"))
        import lib_prompt as lp
        cabeza, cola = lp.partes(lp.inventario(), 0)
        nombres = lp.nombres_posibles()
        salidas = {lp.salida(h): h for h in nombres}
        # frases para «Probar»: de las SEMILLAS de entrenamiento (nunca del banco ni del examen)
        sem = json.loads((RAIZ / "datos/semillas-reactivas.json").read_text())["herramientas"]
        prueba = []
        for h in ("audio_set", "scene_take", "grafico_mostrar", "laminas_ir", "speaker_on_air"):
            fs = sem.get(h, {}).get("frases", [])
            if fs and h in nombres:
                prueba.append({"frase": fs[0], "herramienta": h})
        prueba.append({"frase": "poneme un cafe con leche", "herramienta": lp.DERIVAR})
        return {"cabeza": cabeza, "cola": cola, "fin": "<|im_end|>", "salidas": sorted(salidas),
                "equivalencias": salidas, "derivar": lp.salida(lp.DERIVAR), "gramatica": lp.gramatica_nombre(sorted(salidas), con_derivar=False),
                "huella": lp.huella()["prompt"], "perfil": lp.PERFIL, "frases_prueba": prueba}

    def actualizar_catalogo(self, subidos):
        """datos/modelos-catalogo.json: lo que la app ofrece descargar. La app
        consulta Hugging Face por cada repo y muestra solo los publicos."""
        cat_f = RAIZ / "datos" / "modelos-catalogo.json"
        cat = json.loads(cat_f.read_text()) if cat_f.exists() else {
            "_comentario": "Modelos que la app (Configuracion -> Modelos locales) ofrece bajar de Hugging Face."
                           " Lo actualiza tools/entrenar/fabricar-modelo.py al subir. La app muestra solo los"
                           " que existen y son publicos.", "modelos": []}
        med = self.man.get("medidas", {})
        for url in subidos:
            repo = url.split("huggingface.co/", 1)[-1]
            mlx = repo.endswith("-mlx-q4")
            et = "mlx-q4" if mlx and med.get("mlx-q4") else "bf16"
            b = med.get(et, {}).get("banco", {}).get("0", {})
            x = med.get(et, {}).get("examen", {})
            pct = lambda v: round(100 * v[0] / v[1], 1) if v else None
            e = {"repo": repo, "formato": "mlx" if mlx else "gguf",
                 "plataformas": ["mac-arm64"] if mlx else ["linux-x64", "win-x64"],
                 "archivo": None if mlx else self.gguf_nombre,
                 "base": self.man.get("base"), "perfil": self.man["perfil"].get("nombre"),
                 "herramientas": len(self.man["perfil"].get("herramientas", [])),
                 "prompt": self.man["perfil"].get("prompt"),
                 "tamano_gb": self.man.get("exportado", {}).get("tam_gb", {}).get("mlx_q4" if mlx else "gguf_q4"),
                 "precision": pct(b.get("precision")), "derivado": pct(b.get("derivado")),
                 "examen": pct(x.get("acierto")), "fecha": datetime.date.today().isoformat()}
            cat["modelos"] = [m for m in cat["modelos"] if m["repo"] != repo] + [e]
        cat_f.write_text(json.dumps(cat, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
        log(f"catalogo: datos/modelos-catalogo.json ({len(cat['modelos'])} modelos)")

    def nombre_repo(self):
        n = self.a.repo or f"broadcast-reactivo-{self.man['base'].split('/')[-1].lower()}"
        # el de la receta de unsloth es otro GGUF: no pisar el repo del Q4_K_M
        return n + ("-mtp-ud" if self.man.get("repo_pedido") and not self.a.repo else "")

    def empaquetar(self):
        self.completar_datos()
        rep = self.dir / "repos"
        if rep.exists():
            shutil.rmtree(rep)
        rep.mkdir()
        ex = self.man["exportado"]
        nombre = self.nombre_repo()
        self.gguf_nombre = f"{nombre}-{ex.get('cuant', 'Q4_K_M')}.gguf"
        hp = Path(ex["mlx_q4"]) / "huella-prompt.json"
        lic = Path(self.man["base_ruta"]) / "LICENSE"
        eq = self.man["perfil"].get("equivalencias")
        repos = []
        for formato in ("mlx", "gguf"):
            repo = f"{nombre}-{'mlx-q4' if formato == 'mlx' else 'GGUF'}"
            d = rep / repo
            if formato == "mlx":
                shutil.copytree(ex["mlx_q4"], d, copy_function=enlazar)
            else:
                d.mkdir()
                enlazar(ex["gguf_q4"], d / self.gguf_nombre)
            if hp.exists():
                shutil.copy(hp, d)
            if lic.exists():
                shutil.copy(lic, d / "LICENSE")
            if eq:
                (d / "equivalencias.json").write_text(json.dumps(eq, ensure_ascii=False, indent=1))
            (d / "prompt.json").write_text(json.dumps(self.prompt_para_servir(), ensure_ascii=False, indent=1))
            (d / ".gitattributes").write_text("*.safetensors filter=lfs diff=lfs merge=lfs -text\n"
                                              "*.gguf filter=lfs diff=lfs merge=lfs -text\n")
            (d / "README.md").write_text(self.tarjeta(formato, repo), encoding="utf-8")
            repos.append(str(d))
        self.man["repos"] = repos
        # LA TARJETA NO SE PIERDE: ademas de ir en cada repo, queda VERSIONADA en
        # docs/modelos/ (texto chico) con un resumen de lo que define al modelo.
        # Los pesos no: pesan gigas y van a Hugging Face (.gitignore los excluye).
        doc = RAIZ / "docs" / "modelos"
        doc.mkdir(parents=True, exist_ok=True)
        for d in repos:
            shutil.copy(Path(d) / "README.md", doc / f"{Path(d).name}.md")
        resumen = {k: self.man.get(k) for k in ("base", "base_revision", "base_licencia", "corpus_firma",
                                                 "corpus_ejemplos", "repo_commit", "arquitectura",
                                                 "entrenamiento", "medidas", "mtp")}
        resumen["perfil"] = {k: v for k, v in self.man["perfil"].items() if k != "equivalencias"}
        resumen["repos"] = [Path(d).name for d in repos]
        (doc / f"{nombre}.json").write_text(json.dumps(resumen, ensure_ascii=False, indent=1))
        log(f"tarjeta versionada en docs/modelos/{nombre}*")
        (self.dir / "empaquetado").write_text("ok")
        for d in repos:
            log(f"repo armado: {d}")

    def subir(self):
        """A Hugging Face, SOLO con --subir y --hf-usuario. Privado salvo --publico:
        algo publicado no se despublica del todo, eso lo decide una persona."""
        if not (self.a.subir and self.a.hf_usuario):
            log("subir: no se pidio (--subir --hf-usuario X)")
            return
        hf = RAIZ / ".venv-entrenar/bin/hf"
        if not hasattr(self, "gguf_nombre"):
            self.gguf_nombre = f"{self.nombre_repo()}-{self.man['exportado'].get('cuant', 'Q4_K_M')}.gguf"
        subidos = []
        for d in self.man["repos"]:
            rid = f"{self.a.hf_usuario}/{Path(d).name}"
            cmd = [hf, "repo", "create", rid, "--repo-type", "model", "--exist-ok"]
            if not self.a.publico:
                cmd.append("--private")
            subprocess.run([str(c) for c in cmd], cwd=RAIZ, capture_output=True, text=True)
            correr([hf, "upload", rid, d, ".", "--repo-type", "model",
                    "--commit-message", f"{self.a.nombre}: corpus {self.man.get('corpus_firma')}"])
            subidos.append(f"https://huggingface.co/{rid}")
        self.man["subido"] = {"repos": subidos, "privado": not self.a.publico,
                              "fecha": datetime.datetime.now().isoformat(timespec="seconds")}
        (self.dir / "subido").write_text("\n".join(subidos))
        self.actualizar_catalogo(subidos)
        for u in subidos:
            log(f"subido: {u}{' (privado)' if not self.a.publico else ''}")

    def correr(self):
        hasta = ETAPAS.index(self.a.hasta)
        if "arquitectura" not in self.man:
            self.etapa("inspeccionar", self.inspeccionar, self.dir / "inspeccionado")
        pendientes = [e for e in ETAPAS[:hasta + 1] if not self.hecho(e, self.dir / {
            "inspeccionar": "inspeccionado", "descargar": "descargado", "corpus": "corpus/entrenar.jsonl",
            "entrenar": "adaptador/adapter_config.json", "medir": "medido", "exportar": "exportado",
            "empaquetar": "empaquetado", "subir": "subido"}[e])]
        self.controlar_espacio(pendientes)
        plan = [("inspeccionar", self.inspeccionar, self.dir / "inspeccionado"),
                ("descargar", self.descargar, self.dir / "descargado"),
                ("corpus", self.hacer_corpus, self.corpus / "entrenar.jsonl"),
                ("entrenar", self.entrenar, self.adaptador / "adapter_config.json"),
                ("medir", self.medir, self.dir / "medido"),
                ("exportar", self.exportar, self.dir / "exportado"),
                ("empaquetar", self.empaquetar, self.dir / "empaquetado"),
                ("subir", self.subir, self.dir / "subido")]
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
    ap.add_argument("--hasta", choices=ETAPAS, default="subir")
    ap.add_argument("--repo", help="nombre del repo en HF (por defecto broadcast-reactivo-<base>); se le agrega -mlx-q4 / -GGUF")
    ap.add_argument("--hf-usuario", help="usuario u organizacion de Hugging Face")
    ap.add_argument("--subir", action="store_true", help="subir los repos (privados salvo --publico)")
    ap.add_argument("--publico", action="store_true")
    ap.add_argument("--rehacer", choices=ETAPAS, help="rehacer desde esta etapa (las anteriores se reusan)")
    ap.add_argument("--sin-mtp", action="store_true", help="no entrenar la cabeza MTP (usar la de la base)")
    ap.add_argument("--conservar", action="store_true", help="no borrar el fusionado ni el GGUF bf16 al exportar")
    ap.add_argument("--curva-q4", action="store_true", help="medir el q4 en los 5 puntos del dial (si no, solo sesgo 0)")
    args = ap.parse_args()
    # el perfil viaja por el entorno a TODO lo que corre debajo (corpus,
    # entrenamiento, evaluacion): lib_prompt lo lee de BA_PERFIL
    os.environ["BA_PERFIL"] = args.perfil
    Fabrica(args).correr()


if __name__ == "__main__":
    main()
