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
  vision      el modelo CON vision: checkpoint completo (base + modulos
              ajustados, con-vision.py), MLX con vision (mlx_vlm.convert, la torre
              sin cuantizar; reemplaza al de solo texto) y mmproj para llama.cpp.
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
ETAPAS = ["inspeccionar", "descargar", "corpus", "entrenar", "medir", "exportar", "vision", "empaquetar", "subir"]

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


# El contrato del formato de tool calling (v2), para la tarjeta: lo que tiene que
# hacer quien lo sirve (ba-modelo, llama.cpp). Fuente: tools/entrenar/lib_tc.py.
METODO_LOOP = """## Cómo se entrenó: el loop

Este modelo no sale de juntar muchos ejemplos: sale de un **loop** que mide cada frase, la corrige y
vuelve a medir, primero sobre el corpus y después sobre el modelo entrenado.

### 1. El corpus crece por calidad, no por cantidad

De 15 a 20 frases semilla por herramienta (84 herramientas), como las dice un operador de cabina
rioplatense. **Cada frase es un vector**: el estado interno del modelo cuando la lee. Cada herramienta
es una nube de vectores, con su centro μ y su dispersión Σ, y se mide cuánto se separa de las demás:

- **Separación de un grupo** contra el más peligroso (Frobenius):
  `Q(a) = min_b ‖μa − μb‖² / (‖Σa‖_F + ‖Σb‖_F)`
- **Puntaje de una frase**: cuánto cae Q sin ella, `(Q(a) − Q(a sin la frase)) / Q(a)`. Negativo: la
  frase empeora su grupo, y se sabe hacia qué vecina tira.
- **Choque entre dos grupos** (CDNV direccional, arXiv 2603.03530): la varianza de las dos nubes sobre
  el eje que las une, dividida por la distancia entre centros; acota el error del clasificador por
  centro más cercano. Un grupo bueno es **angosto hacia sus vecinas y ancho en todo lo demás**.
- **Margen del decodificador** (AUM): logit de la herramienta correcta menos el del mejor rival.

La matemática **no poda a ciegas**: señala qué frase está mal y hacia dónde tira.

### 2. Agentes que corrigen, con contexto real

Un agente (una LLM sin conexión, que solo ve su lote) recibe la ficha real de la herramienta, la de
sus vecinas y la frase señalada, y la reescribe atacando ese detalle; separa los pares que se pisan;
un juez ciego verifica. Después de cada cambio se recalcula el grupo: si se corrió se realinea, si
quedó apretado se abre con formas de pedir distintas. **Test → arreglar → test**, hasta que todas
dan bien.

### 3. STE-ES: el alma de ASD-STE100, en castellano de cabina

Las frases las reescribe una LLM, así que se le pone un lenguaje controlado: **una palabra, un
significado**; las palabras de muchos sentidos («poné», «sacá», «pasá») solo con el objeto que fija
cuál; un pedido, una acción; objeto y cantidad explícitos. El núcleo de la frase se controla y la
superficie (voseo, muletillas, typos) queda libre, porque el modelo tiene que entender al operador
real. En las semillas, el 64 % arrancaba con una palabra que comparten cuatro o más herramientas:
justo donde los grupos se pisan.

### 4. Lo que mide no se toca

Un examen (105 casos) y un banco de 1.423 frases escritas aparte **nunca se entrenan ni se le
muestran a un agente**. Es lo único que mide de verdad.

### 5. Herramienta y argumentos, con estados del aparato inventados

Cada semilla es una plantilla con huecos y sus argumentos («bajale 4 a `{audio}`» → `audio_set`,
`volumeDb: -4`), validada contra el esquema de la herramienta y contra STE-ES. Se rinde varias veces
con **estados del aparato sintéticos y distintos**: así el modelo aprende a **copiar** el nombre que
hay en el estado («Micro Laura», «Cámara 2») en vez de memorizarlo.

### 6. Lo que no es suyo, y no olvidar lo general

Además de las llamadas: «no es mío» (`derivar`, al asistente grande), «falta un dato»
(`falta_dato`, pregunta) y **charla general escrita por el propio modelo base**, para que el
ajuste no le borre lo genérico (ajustado solo a un catálogo, Qwen3-4B conserva el 61 % de su
capacidad general: arXiv 2605.17774).

### 7. El loop del modelo

Entrenar → medir con plantillas que nunca vio y con el banco → **autoexamen**: las plantillas
entrenadas con estados nuevos → un agente corrige las anotaciones o suma ejemplos de lo que falló
(validados por esquema y STE-ES) → otra vuelta. Se queda con la mejor.

### 8. Ingeniería

El system con las 84 herramientas son ~8.200 tokens: se procesan una vez por lote y se reusan. En el
backward cada capa se recalcula sola (con una foto de su caché que se repone antes), y los logits se
calculan solo donde hay pérdida: así entra en dos RTX 3090. La cabeza MTP se reentrena para este
modelo con los borradores en cadena, como se usan en vivo, y con texto escrito por el propio modelo.
"""

FORMATO_TC = """## Formato (el contrato para servirlo)

Todo sale de `prompt.json` (fuente: `tools/entrenar/lib_tc.py` del aparato). Firma del prompt: `@HUELLA@`.

**1. Cabeza fija** (`cabeza`, ~8.200 tokens, idéntica byte a byte: procesarla una vez y reusar el
caché). Es el system nativo de tools de Qwen3.5: `# Tools … <tools>` con un JSON por línea por cada
herramienta (descripción corta y esquema compactado) más dos pseudo-funciones, **`derivar`** (sin
argumentos: no es para estas herramientas) y **`falta_dato`** (argumento `pregunta`), la instrucción
estándar de Qwen y las reglas del aparato. Termina en `<|im_start|>user\\n`.

**2. Turno del usuario**

```
Estado: {JSON compacto del estado}
Pedido: <frase del operador>
```

JSON sin espacios (`separators=(',',':')`) y con tildes reales. El estado **completo**:
`escena_actual`, `escena_armada` (texto), `escenas` (nombres), `fuentes_de_la_escena_actual`
(`[{nombre, visible}]`), `audio` (`[{nombre, volumen_db, silenciada}]`; el primero es el micrófono
del operador, el predeterminado si no se nombra fuente), `participantes` (`[{id, nombre, estado}]`),
`graficos`, `carruseles` (nombres). Charla sin estado: solo la frase.

**3. Cola** (`cola`): `<|im_end|>\\n<|im_start|>assistant\\n<think>\\n\\n</think>\\n\\n` (sin razonamiento).

**4. Respuesta** (termina en `<|im_end|>`)

```
<tool_call>
<function=audio_set>
<parameter=name>
Música
</parameter>
<parameter=volumeDb>
-4
</parameter>
</function>
</tool_call>
```

- herramienta: `<function=nombre>` con sus `<parameter=…>`;
- no es suyo: `<function=derivar>` sin parámetros (al asistente grande);
- falta un dato obligatorio: `<function=falta_dato>` con `pregunta` («¿A qué escena?»);
- charla: texto, sin `<tool_call>`.

Para leerla: `<tool_call>\\s*<function=([^>\\n]+)>(.*?)</function>\\s*</tool_call>` y los parámetros con
`<parameter=([^>\\n]+)>\\n?(.*?)\\n?</parameter>`. Los valores salen como texto.

**5. Convenciones de los valores** (en XML no hay comillas: por eso el `=`)

| | se escribe | ejemplo |
|---|---|---|
| nombres | exactos, copiados del estado | `Micro Laura` |
| cambio relativo | con signo; «un poco» = 3, sin cantidad = 6; **quien sirve lo suma** al valor real y lo recorta al rango | `-4`, `+3` |
| valor exacto | con `=` adelante | `=-10` |
| booleanos | | `true` / `false` |
| enum | el valor exacto del esquema | |

**6. Recomendado al servirlo**

- Restringir el nombre: después de `<tool_call>\\n<function=` solo un nombre de `funciones` y `>`;
  el resto, libre (así se midió el banco; el formato salió válido en el 100 % sin gramática).
- Dial de `derivar`: sumarle un sesgo al logit de su primer token, como en el v1.
- Validar siempre: nombres que existen, rangos, requeridos.
- MTP: la misma cabeza que el v1 (`mtp.safetensors` fp16 / `mtp-q8.safetensors` en el MLX; adentro
  del GGUF para `--spec-type draft-mtp`).

**Diferencias con el v1**: el v1 contesta solo el nombre de la herramienta (con otro prompt, de
fichas) y los argumentos van en una segunda llamada; el v2 hace las dos cosas en una. En el v1 el
valor exacto iba como número suelto; en el v2, `-10` es un cambio y `=-10` es exacto."""


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
                "exportar": gb * 3.4 + 2,
                # fusionado (si hay que rehacerlo) + el completo con vision + MLX + mmproj
                "vision": gb * 2.5 + 2, "empaquetar": 0.2}.get(etapa, 0)

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
        if self.a.formato == "tc":
            return self.hacer_corpus_tc()
        env = {"BA_ETIQUETAS": "0"}
        correr(["make", "corpus-entrenamiento"], env=env, salida=self.dir / "corpus.log")
        if self.corpus.exists():
            shutil.rmtree(self.corpus)
        shutil.copytree(RAIZ / "datos/entrenamiento", self.corpus)
        self.man["corpus_firma"] = firma(self.corpus)
        sys.path.insert(0, str(RAIZ / "tools" / "entrenar"))
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

    def hacer_corpus_tc(self):
        """El corpus de tool calling viene armado (tc-corpus.py, por el bucle)."""
        if not self.a.corpus_de:
            sys.exit("--formato tc necesita --corpus-de (la carpeta de tc-corpus.py)")
        if self.corpus.exists():
            shutil.rmtree(self.corpus)
        shutil.copytree(Path(self.a.corpus_de).expanduser(), self.corpus)
        self.man["corpus_firma"] = firma(self.corpus)
        sys.path.insert(0, str(RAIZ / "tools" / "entrenar"))
        import lib_prompt as lp
        import lib_tc as tc
        self.man["perfil"] = {"nombre": lp.PERFIL, "categorias": lp.REACTIVAS, "entradas": lp.ENTRADAS,
                              "herramientas": lp.herramientas_reactivas(), "prompt": tc.huella(),
                              "etiquetas": None, "formato": "tc"}
        self.man["corpus_ejemplos"] = sum(1 for _ in open(self.corpus / "entrenar.jsonl"))
        self.man["corpus_huella"] = json.loads((self.corpus / "huella.json").read_text())
        self.man["repo_commit"] = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=RAIZ,
                                                 capture_output=True, text=True).stdout.strip()

    def entrenar(self):
        cfg = self.man["arquitectura"]
        if self.a.adaptador_de:  # ya entrenado (p. ej. el mejor del bucle)
            if self.adaptador.exists():
                shutil.rmtree(self.adaptador)
            shutil.copytree(Path(self.a.adaptador_de).expanduser(), self.adaptador)
            self.man["entrenamiento"] = {"modo": cfg["modo"], "adaptador_de": str(self.a.adaptador_de),
                                         "lote": 8 if self.a.formato == "tc" else 16, "lr": 2e-4, "lora_r": 32,
                                         "epocas": self.a.epocas}
            return
        comun = ["--base", self.a.base, "--datos", self.corpus, "--salida", self.adaptador,
                 "--epocas", str(self.a.epocas), "--eval-cada", "100", "--semilla", str(self.a.semilla)]
        comun += cfg["extra"]
        if self.a.formato == "tc":  # prefijo de ~8.000 tokens: lote 8 y recalculo por capa
            comun += ["--lote", "8"] + ([] if "--recalcular" in comun else ["--recalcular"])
        env = {"PYTORCH_CUDA_ALLOC_CONF": "expandable_segments:True", "BA_ETIQUETAS": "0"}
        a_medias = (self.adaptador / "estado" / "estado.pt").exists()
        if self.adaptador.exists() and not a_medias:
            shutil.rmtree(self.adaptador)
        if self.a.formato == "tc":
            # las dos placas como una (FSDP2): entrenar-rapido supervisa y, si se
            # corta, sigue desde su ultimo estado (tambien al volver a fabricar)
            correr([PY, E / "entrenar-rapido.py", "--fsdp", "--micro", "4"] + comun
                   + (["--reanudar"] if a_medias else []), env=env, salida=self.dir / "entrenar.log")
        elif cfg["modo"] == "repartir":
            correr([PY, E / "entrenar-rapido.py", "--repartir"] + comun, env=env, salida=self.dir / "entrenar.log")
        else:
            correr([RAIZ / ".venv-entrenar/bin/torchrun", "--nproc_per_node", "2", E / "entrenar-rapido.py"] + comun,
                   env=env, salida=self.dir / "entrenar.log")
        self.man["entrenamiento"] = {"modo": cfg["modo"], "epocas": self.a.epocas, "semilla": self.a.semilla,
                                     "lote": 16, "lr": 2e-4, "lora_r": 32,
                                     "fin": (self.dir / "entrenar.log").read_text().strip().splitlines()[-1]}

    def medir(self, modelo=None, etiqueta="bf16", banco_curva=True, margen=True, tipo="bf16"):
        modelo = modelo or self.adaptador
        if self.a.formato == "tc":
            return self.medir_tc(modelo, etiqueta, tipo)
        md = self.dir / "medidas"
        md.mkdir(exist_ok=True)
        env = {"BA_ETIQUETAS": "0"}
        res = {}
        log(f"examen ({etiqueta})")
        out = subprocess.run([str(PY), str(E / "evaluar.py"), "--modelo", str(modelo), "--tipo", tipo,
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
            cmd = [str(PY), str(E / "evaluar.py"), "--modelo", str(modelo), "--tipo", tipo, "--casos",
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

    def medir_tc(self, modelo, etiqueta, tipo="bf16"):
        """tc-evaluar.py en las dos placas: prueba + charla en una, banco + examen en
        la otra. El bf16 con el banco entero; las variantes (q4, fp16), con 500."""
        md = self.dir / "medidas"
        md.mkdir(exist_ok=True)
        lim = "0" if etiqueta == "bf16" else "500"
        procs = []
        for placa, conj in (("0", "prueba,charla"), ("1", "banco,examen")):
            sal = md / f"{etiqueta}-{placa}.json"
            procs.append((sal, subprocess.Popen(
                [str(PY), str(E / "tc-evaluar.py"), "--modelo", str(modelo), "--corpus", str(self.corpus),
                 "--conjuntos", conj, "--limite-banco", lim, "--tipo", tipo, "--json", str(sal)],
                cwd=RAIZ, env=dict(os.environ, CUDA_VISIBLE_DEVICES=placa),
                stdout=open(md / f"{etiqueta}-{placa}.log", "w"), stderr=subprocess.STDOUT)))
        med, fallos = {}, {}
        for sal, p in procs:
            if p.wait():
                sys.exit(f"fallo midiendo {etiqueta} (ver {sal.with_suffix('.log')})")
            r = json.loads(sal.read_text())
            med.update(r["medidas"])
            fallos.update(r["fallos"])
        (md / f"{etiqueta}-fallos.json").write_text(json.dumps(fallos, ensure_ascii=False, indent=1))
        res = {"banco": {"0": med["banco"]["conteos"]}, "examen": med["examen"]["conteos"],
               "prueba": med.get("prueba"), "charla": med.get("charla")}
        log(f"{etiqueta}: prueba {json.dumps(med.get('prueba', {}).get('conteos'))} · banco "
            f"{json.dumps(med['banco']['conteos'])}")
        self.man.setdefault("medidas", {})[etiqueta] = res
        self.guardar()
        if etiqueta == "bf16":
            (self.dir / "medido").write_text("ok")

    def exportar(self):
        ex = self.dir / "export"
        ex.mkdir(exist_ok=True)
        # LA SONDA DE DERIVAR (sonda-exportar.py): se entrena sobre el modelo final y
        # viaja con el (sonda.safetensors + su entrada en modelo.json)
        if self.a.formato == "tc" and not (self.dir / "sonda.safetensors").exists():
            correr([PY, E / "sonda-exportar.py", "--modelo", self.adaptador, "--corpus", self.corpus,
                    "--salida", self.dir], env={"CUDA_VISIBLE_DEVICES": "0"}, salida=self.dir / "sonda.log")
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
                if self.a.formato == "tc":  # prefijo de ~8.000-10.000 tokens: lotes chicos, media epoca
                    # (con 4 la v5, un 30 % mas larga que la v3, no entraba en ningun lote)
                    cmd += ["--lote", "2", "--lote-texto", "2", "--epocas", "0.5"]
                if self.man["arquitectura"]["modo"] == "repartir":
                    cmd.append("--repartir")
                correr(cmd, env={"PYTORCH_CUDA_ALLOC_CONF": "expandable_segments:True"}, salida=self.dir / "mtp.log")
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
                    self.mtp_a_mlx(mtp_f, mlx, self.man["base"])
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
        if not self.a.conservar:  # el GGUF bf16 se rehace con --rehacer exportar; el fusionado
            bf16.unlink(missing_ok=True)  # lo usa la etapa vision, que lo borra al terminar
            self.man["exportado"]["gguf_bf16"] = None
            log(f"intermedio borrado (GGUF bf16); libres {libre_gb(self.dir):.0f} GB")
        (self.dir / "exportado").write_text("ok")

    @staticmethod
    def mtp_a_mlx(mtp_f, mlx, base):
        """La cabeza MTP en el modelo MLX como la carga ba-modelo (mlx-swift-lm):
        mtp.safetensors (fp16) y mtp-q8.safetensors, normas SIN el +1 (lo suma
        mlx-swift-lm). Ver mtp-mlx.py."""
        correr([PY_MLX, E / "mtp-mlx.py", "--entrada", mtp_f, "--salida", mlx])

    def vision(self):
        """EL MODELO CON VISION. Entrenamos solo la parte de texto (AutoModelForCausalLM)
        y el fusionado sale sin la torre de vision; el LoRA no la toca, asi que se
        arma el checkpoint completo (con-vision.py: base + modulos ajustados) y de
        ahi salen el MLX con vision (mlx_vlm.convert; la torre sin cuantizar) y el
        mmproj de llama.cpp. El MLX con vision REEMPLAZA al de solo texto: mlx-lm
        lo carga igual (ignora la vision) y mlx-vlm lo usa con imagenes."""
        ex = self.dir / "export"
        hf = ex / "modelo-hf"
        if not self.man["arquitectura"].get("vision"):
            log("la base no tiene vision")
        else:
            if not (hf / "config.json").exists():
                correr([PY, E / "exportar-mlx.py", "--modelo", self.adaptador, "--salida", ex / "modelo",
                        "--solo-fusionar"], env={"CUDA_VISIBLE_DEVICES": "0"}, salida=self.dir / "fusionar.log")
            completo = ex / "modelo-vl-hf"
            correr([PY, E / "con-vision.py", "--fusionado", hf, "--base", self.man["base_ruta"],
                    "--adaptador", self.adaptador, "--salida", completo], salida=self.dir / "vision.log")
            # TODO EN FP16 (decision: el aparato es una M1, sin bf16 nativo; y mezclar
            # vision en fp16 con texto en bf16 obliga a MLX a pasar a fp32). Se mide
            # igual el q4 corriendo ENTERO en fp16 contra el de bf16, para saber si
            # desborda: queda en el manifiesto y en la tarjeta, y avisa si pierde.
            sim = ex / "modelo-q4-simulado-fp16"
            correr([PY_MLX, E / "simular-q4-mlx.py", "--hf", hf, "--salida", sim], salida=self.dir / "simular.log")
            self.medir(modelo=sim, etiqueta="mlx-q4-fp16", banco_curva=False, margen=False, tipo="fp16")
            shutil.rmtree(sim)
            pct = lambda v: 100 * v[0] / v[1] if v else 0.0
            ref = self.man["medidas"].get("mlx-q4", {}).get("banco", {}).get("0", {})
            f16 = self.man["medidas"]["mlx-q4-fp16"]["banco"].get("0", {})
            caida = max(pct(ref.get(k)) - pct(f16.get(k)) for k in ("precision", "acierto")) if ref else 0.0
            tipo = "float16"
            self.man["exportado"]["mlx_dtype"] = {"tipo": tipo, "caida_fp16_puntos": round(caida, 2)}
            log(f"MLX en fp16: corriendo en fp16 el q4 pierde {caida:.1f} puntos en el banco"
                + ("  <-- OJO: desborda, revisar" if caida > 1.0 else ""))
            vl = ex / "modelo-vl-q4"
            if vl.exists():
                shutil.rmtree(vl)
            correr([PY_MLX, "-m", "mlx_vlm.convert", "--hf-path", completo, "--mlx-path", vl, "-q",
                    "--q-bits", "4", "--q-group-size", "64", "--dtype", tipo], salida=self.dir / "mlx-vlm.log")
            # mlx-vlm 0.7.4 escribe el config SIN la cuantizacion (los pesos si estan
            # cuantizados): sin esto ni mlx-lm ni mlx-vlm lo cargan
            cfg = json.loads((vl / "config.json").read_text())
            q = {"group_size": 64, "bits": 4, "mode": "affine"}
            cfg.setdefault("quantization", q)
            cfg.setdefault("quantization_config", q)
            (vl / "config.json").write_text(json.dumps(cfg, indent=2))
            if (ex / "mtp.safetensors").exists():
                self.mtp_a_mlx(ex / "mtp.safetensors", vl, self.man["base"])
            correr([PY_MLX, "-c", f"from mlx_lm import load; load({str(vl)!r}); "
                                  f"from mlx_vlm import load as v; v({str(vl)!r}); print('carga: mlx-lm y mlx-vlm')"],
                   salida=self.dir / "vision-carga.log")
            mmproj = ex / "gguf" / "mmproj-F16.gguf"
            correr([PY, CONV / "convert_hf_to_gguf.py", completo, "--mmproj", "--outtype", "f16", "--outfile", mmproj],
                   env={"PYTHONPATH": str(CONV / "gguf-py")}, salida=self.dir / "mmproj.log")
            self.man["exportado"].update({
                "mlx_q4": str(vl), "mmproj": str(mmproj), "vision": True})
            self.man["exportado"]["tam_gb"]["mlx_q4"] = round(
                sum(f.stat().st_size for f in vl.rglob("*") if f.is_file()) / 2**30, 2)
            self.man["exportado"]["tam_gb"]["mmproj"] = round(mmproj.stat().st_size / 2**30, 2)
            if not self.a.conservar:
                shutil.rmtree(completo, ignore_errors=True)
            log(f"con vision: {vl.name} ({self.man['exportado']['tam_gb']['mlx_q4']} GB) y {mmproj.name}")
        if not self.a.conservar:
            shutil.rmtree(hf, ignore_errors=True)
        (self.dir / "vision-hecha").write_text("ok")

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
        sys.path.insert(0, str(RAIZ / "tools" / "entrenar"))
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
            vis = m.get("exportado", {}).get("vision")
            archivos = ("`model.safetensors`: MLX, 4 bits (afín, grupos de 64). "
                        + ("Con la parte de visión (la torre sin cuantizar, igual a la de la base: el ajuste no la"
                           " toca): mlx-vlm lo usa con imágenes y mlx-lm lo carga como texto."
                           if vis else "Solo texto (sin la parte de visión).")
                        + (lambda t: f" Lo que no está cuantizado (escalas, normas, visión) va en"
                                     f" **{'fp16' if t['tipo'] == 'float16' else 'bf16'}**: corriendo en fp16 el q4"
                                     f" pierde {max(0.0, t['caida_fp16_puntos']):.1f} puntos en el banco"
                                     f" (las GPU M1/M2 no tienen bf16 nativo)."
                           if t else "")(m.get("exportado", {}).get("mlx_dtype"))
                        + ("\n`mtp.safetensors` (fp16) y `mtp-q8.safetensors` (lineales en 8 bits, afín, grupos de 64):"
                           " la cabeza MTP entrenada, como la carga `ba-modelo` (mlx-swift-lm). Normas en la"
                           " convención de Hugging Face (sin el +1: lo suma mlx-swift-lm al cargarla). Van fuera de"
                           " `model*.safetensors` a propósito: mlx-lm 0.31 tomaría cualquier peso `mtp.*` ahí como un"
                           " checkpoint sin convertir y correría las normas del modelo." if mtp else ""))
        else:
            uso = f"""```bash
llama-server -m {self.gguf_nombre}{' --mmproj mmproj-F16.gguf' if m.get('exportado', {}).get('mmproj') else ''} -c 8192 --cache-reuse 256{' --spec-type draft-mtp --spec-draft-n-max 3' if mtp else ''}
```
llama.cpp b11255 o posterior (arquitectura `qwen35`, con atención lineal). Restringir la salida con
una gramática a los nombres posibles."""
            ru = m.get("receta_unsloth")
            cuant = (f"{RECETA_UNSLOTH} (la receta «UD» de [{ru['repo']}](https://huggingface.co/{ru['repo']}):"
                     f" el mismo tipo por tensor que su `{ru['archivo']}`, aplicado a estos pesos)") if ru else "Q4_K_M"
            archivos = (f"`{self.gguf_nombre}`: {cuant}, calibrado con imatrix sobre el propio"
                        f" corpus (sin el examen){' e incluye la cabeza MTP entrenada' if mtp else ''}."
                        + ("\n`mmproj-F16.gguf`: la parte de visión para llama.cpp (`--mmproj`), igual a la de la"
                           " base." if m.get("exportado", {}).get("mmproj") else ""))
        if self.a.formato == "tc":
            return self.tarjeta_tc(formato, repo, archivos, mtp)
        return f"""---
license: {m['base_licencia']}
base_model: {m.get('base')}
language: [es]
library_name: {'mlx' if formato == 'mlx' else 'gguf'}
pipeline_tag: {'image-text-to-text' if m.get('exportado', {}).get('vision') else 'text-generation'}
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

{self.texto_mtp(mtp)}

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

    def prompt_para_servir_tc(self):
        sys.path.insert(0, str(RAIZ / "tools" / "entrenar"))
        import lib_prompt as lp
        import lib_tc as tc
        prueba = [{"estado": {"audio": [{"nombre": "Mic operador", "volumen_db": -6.0, "silenciada": False},
                                        {"nombre": "Música", "volumen_db": -12.0, "silenciada": False}]},
                   "frase": "bajale 4 a la música", "llamada": tc.llamada("audio_set", {"name": "Música", "volumeDb": "-4"})},
                  {"estado": None, "frase": "poneme un cafe con leche", "llamada": tc.llamada(tc.DERIVAR, {})}]
        return {"formato": "tc", "cabeza": tc.cabeza(), "cola": tc.COLA, "fin": tc.FIN,
                "usuario": "Estado: <JSON compacto del estado del aparato>\nPedido: <frase>",
                "funciones": [f["function"]["name"] for f in tc.funciones()],
                "derivar": tc.DERIVAR, "falta_dato": tc.FALTA,
                "convenciones": {"nombres": "copiados exactos del estado",
                                 "cambio": "con signo: -4 / +3 (un poco = 3, sin cantidad = 6)",
                                 "exacto": "con = adelante: =-10", "booleanos": "true / false",
                                 "charla": "texto, sin tool_call"},
                "huella": tc.huella(), "perfil": lp.PERFIL, "frases_prueba": prueba}

    def prompt_para_servir(self):
        if self.a.formato == "tc":
            return self.prompt_para_servir_tc()
        """Todo lo que necesita quien SIRVE el modelo, sin reconstruir el prompt:
        la app (C++) y el servidor MLX lo leen tal cual. Es el MISMO de
        entrenamiento: con otras fichas seria otro modelo (ver huella)."""
        sys.path.insert(0, str(RAIZ / "tools" / "entrenar"))
        import lib_prompt as lp
        cabeza, cola = lp.partes(lp.inventario(), 0)
        nombres = lp.nombres_posibles()
        salidas = {lp.salida(h): h for h in nombres}
        # frases para «Probar»: de las SEMILLAS de entrenamiento (nunca del banco ni del examen)
        sem = json.loads((RAIZ / "datos/semillas-reactivas.json").read_text())["herramientas"]
        prueba = []
        for h in ("audio_set", "preview_al_aire", "grafico_mostrar", "laminas_ir", "speaker_on_air"):
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
                 "examen": pct(x.get("acierto")), "fecha": datetime.date.today().isoformat(),
                 "version": self.man.get("version"),
                 "formato_llamada": "qwen3.5-tool-call-xml" if self.man["perfil"].get("formato") == "tc"
                 else "nombre-de-herramienta", "manifiesto": "modelo.json"}
            cat["modelos"] = [m for m in cat["modelos"] if m["repo"] != repo] + [e]
        # la app solo habla tool calling: los modelos de solo nombres (v1) salen del catalogo
        cat["modelos"] = [m for m in cat["modelos"] if not m["repo"].startswith(f"{self.a.hf_usuario}/broadcast-reactivo")
                          or m.get("formato_llamada") == "qwen3.5-tool-call-xml"]
        cat_f.write_text(json.dumps(cat, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
        log(f"catalogo: datos/modelos-catalogo.json ({len(cat['modelos'])} modelos)")

    def tarjeta_tc(self, formato, repo, archivos, mtp):
        m, a = self.man, self.a
        p, med = m["perfil"], m.get("medidas", {})
        pct = lambda v: f"{100 * v[0] / v[1]:.1f} %" if v and v[1] else "–"
        b16, bq = med.get("bf16", {}), med.get("mlx-q4", {})
        pr = (b16.get("prueba") or {}).get("conteos", {})
        prq = (bq.get("prueba") or {}).get("conteos", {})
        bb, bbq = b16.get("banco", {}).get("0", {}), bq.get("banco", {}).get("0", {})
        ch16 = b16.get("charla") or {}
        hu = m.get("corpus_huella", {})
        usuario = a.hf_usuario or "<usuario>"
        q4 = bool(prq or bbq)
        filas = [("Argumentos exactos (plantillas nunca vistas)", pr.get("args_exactos"), prq.get("args_exactos")),
                 ("Herramienta correcta (ídem)", pr.get("herramienta"), prq.get("herramienta")),
                 ("Formato válido", pr.get("formato"), prq.get("formato")),
                 ("`falta_dato` cuando corresponde", pr.get("falta_dato"), prq.get("falta_dato")),
                 ("Banco · precisión al elegir", bb.get("precision"), bbq.get("precision")),
                 ("Banco · cobertura", bb.get("cobertura"), bbq.get("cobertura")),
                 ("Banco · derivado", bb.get("derivado"), bbq.get("derivado")),
                 ("Examen (105 casos)", b16.get("examen", {}).get("acierto"), bq.get("examen", {}).get("acierto"))]
        tabla = ("| | bf16 | MLX q4 |\n|---|---:|---:|\n" if q4 else "| | bf16 |\n|---|---:|\n") + "\n".join(
            f"| {n} | {pct(x)} |" + (f" {pct(y)} |" if q4 else "") for n, x, y in filas)
        porc = hu.get("porcion", {})
        mezcla = " · ".join(f"{k} {100 * v:.0f} %" for k, v in porc.items())
        if formato == "mlx":
            uso = f"""```python
from mlx_vlm import load          # con imágenes; mlx_lm.load si es solo texto
modelo, proc = load("{usuario}/{repo}")
# el prompt: prompt.json -> cabeza + "Estado: {{...}}\\nPedido: ..." + cola
```
En la Mac del aparato lo sirve `ba-modelo` (mlx-swift-lm), con la cabeza MTP en `mtp.safetensors`."""
        else:
            uso = f"""```bash
llama-server -m {getattr(self, 'gguf_nombre', '')}{' --mmproj mmproj-F16.gguf' if m.get('exportado', {}).get('mmproj') else ''} \\
    -c 16384 --cache-reuse 256{' --spec-type draft-mtp --spec-draft-n-max 3' if mtp else ''}
```
llama.cpp b11255 o posterior (arquitectura `qwen35`)."""
        if m.get("exportado", {}).get("vision"):
            if formato == "mlx":
                como = f"""```python
from mlx_vlm import load, generate
from mlx_vlm.prompt_utils import apply_chat_template
modelo, proc = load("{usuario}/{repo}")
p = apply_chat_template(proc, modelo.config, "¿Qué se ve en esta imagen?", num_images=1)
print(generate(modelo, proc, p, image=["captura.png"], max_tokens=80).text)
```"""
                donde = ("la torre de visión de Qwen3.5 (333M parámetros) va **dentro de `model.safetensors`**, sin cuantizar"
                         " y en fp16 como el resto. mlx-vlm la usa con imágenes; mlx-lm carga el mismo archivo como texto"
                         " (la ignora).")
            else:
                como = f"""```bash
llama-mtmd-cli -m {getattr(self, 'gguf_nombre', '')} --mmproj mmproj-F16.gguf --image captura.png -p "¿Qué se ve?"
# o con el servidor: llama-server ... --mmproj mmproj-F16.gguf (imágenes por la API compatible con OpenAI)
```"""
                donde = "la torre de visión va en **`mmproj-F16.gguf`** (F16), aparte del modelo, como lo usa llama.cpp."
            vision = f"""## Visión

El modelo **ve imágenes**: {donde} Es **la misma torre de la base**: el ajuste entrenó solo la parte
de texto y la visión no se toca, así que ve como Qwen3.5-4B (capturas de la salida, de una fuente, del
multiview). Verificado al armarlo: describe una imagen de prueba igual que el modelo base.

{como}"""
        else:
            vision = ""
        return f"""---
license: {m['base_licencia']}
base_model: {m.get('base')}
language: [es]
library_name: {'mlx' if formato == 'mlx' else 'gguf'}
pipeline_tag: {'image-text-to-text' if m.get('exportado', {}).get('vision') else 'text-generation'}
tags: [function-calling, tool-calling, broadcast, live-tv, spanish, qwen3.5, vision{', mlx' if formato == 'mlx' else ', gguf, llama.cpp'}]
---

# {repo}

> **ES** · El modelo local de un aparato de producción de TV en vivo. Por cada pedido del operador
> («bajale 4 a la música») escribe **la llamada completa** —herramienta y argumentos— en el formato
> nativo de tool calling de Qwen3.5, copiando los nombres del estado del aparato. Si el pedido no es
> suyo lo deriva; si falta un dato, lo pregunta; si es charla, contesta en texto. **Ve imágenes**
> (la torre de visión de Qwen3.5 va incluida).
>
> **EN** · Local model for a live-TV production appliance: it writes the **full tool call** (tool +
> arguments) in Qwen3.5's native format, copying names from the appliance state; it hands off what
> isn't its own and asks when data is missing. Includes Qwen3.5's vision tower. Rioplatense Spanish.

**{len(p.get('herramientas', []))} herramientas** · base [`{m.get('base')}`](https://huggingface.co/{m.get('base')}) · {'MLX 4 bits, todo en fp16, con visión' if formato == 'mlx' else 'GGUF Q4_K_M + mmproj (visión)'} · cabeza MTP entrenada

## Resultados

{tabla}

Medido contra lo que **no** se entrenó: plantillas apartadas con estados nuevos, y el banco de
orientación (1.423 frases escritas aparte). *Cobertura*: de lo que es suyo, cuánto atiende solo ·
*precisión*: de lo que atiende, cuánto acierta · *derivado*: de lo ajeno, cuánto pasa al asistente
grande. Charla general: pérdida {ch16.get('nll_por_token', 0):.2f} por token sobre respuestas del modelo base no
entrenadas; contesta charla con una llamada en el {100 * ch16.get('llamadas_en_charla', 0):.0f} % de los casos.

## Uso rápido

{uso}

{vision}

{FORMATO_TC.replace("@HUELLA@", str(p.get('prompt')))}

{METODO_LOOP}

## Datos de entrenamiento

- {m.get('corpus_ejemplos')} ejemplos: {mezcla} (herramienta con argumentos, `falta_dato`, `derivar`, charla).
- LoRA r=32 sobre todas las lineales (también las de atención lineal), lr 2e-4 coseno, fusionado.
- La charla: pedidos de [databricks-dolly-15k](https://huggingface.co/datasets/argilla/databricks-dolly-15k-curated-multilingual)
  en castellano (CC-BY-SA 3.0, solo las instrucciones) y del oficio, contestados por el Qwen3.5 base.
- Firma del prompt `{p.get('prompt')}` · corpus `{m.get('corpus_firma')}` · código del aparato `{m.get('repo_commit')}`.

{self.texto_mtp(mtp)}

## Limitaciones

- Primera versión del formato: medida contra plantillas apartadas y el banco, todavía no con el
  aparato andando.
- Atiende solo lo suyo: deriva al asistente grande más que la versión de solo nombres (ver cobertura).
- Quien lo sirve tiene que validar los argumentos: nombres que existen, rangos, la cuenta del cambio.

## Archivos

{archivos}
`prompt.json`: todo para servirlo · `huella-prompt.json`: firma del prompt · `LICENSE`: la de la base.
"""

    @staticmethod
    def texto_mtp(mtp):
        """La seccion de la cabeza MTP: tokens aceptados por ciclo y por paso,
        en nombres y en texto, la original contra la entrenada (y cuantizada)."""
        if not mtp:
            return ""
        if "despues" not in mtp:  # cabezas viejas: solo el paso 1 en nombres
            return (f"La cabeza MTP se reentrenó para este modelo: acierta el token siguiente en el "
                    f"{100 * mtp['aceptacion_despues']:.1f}% de los casos de validación (la original, "
                    f"{100 * mtp['aceptacion_antes']:.1f}%).")
        K = mtp["pasos_mtp"]
        filas = []
        for clase, nombre in (("nombres", "nombres de herramienta"), ("texto", "texto libre")):
            for et, d in (("original", mtp["antes"]), ("entrenada", mtp["despues"]),
                          ("entrenada, lineales q8", mtp.get("despues_q8")),
                          ("entrenada, lineales q4", mtp.get("despues_q4"))):
                r = (d or {}).get(clase)
                if r:
                    filas.append(f"| {nombre} | {et} | {r['aceptados_por_ciclo']:.2f} | "
                                 + " / ".join(f"{100 * x:.0f}%" for x in r["por_paso"]) + " |")
        return (f"## Cabeza MTP\n\nReentrenada para este modelo, con {K} borradores EN CADENA (cada paso recibe la salida"
                f" del anterior, como se usa en vivo) y con nombres y texto escrito por el propio modelo. Medido en"
                f" validación: tokens aceptados por ciclo (de {K}) y acierto de cada paso dado que los anteriores"
                f" acertaron.\n\n| datos | cabeza | aceptados por ciclo | por paso |\n|---|---|---|---|\n"
                + "\n".join(filas))

    def nombre_repo(self):
        n = self.a.repo or f"broadcast-reactivo-{self.man['base'].split('/')[-1].lower()}"
        # el de la receta de unsloth es otro GGUF: no pisar el repo del Q4_K_M
        return n + ("-mtp-ud" if self.man.get("repo_pedido") and not self.a.repo else "")

    def modelo_json(self, formato_repo, repo):
        """modelo.json: lo que la app compara al revisar un modelo, sin prosa.
        Endpoints cubiertos (con la huella de su esquema, calculable desde el
        registro MCP de la app), version de entrenamiento y formato de llamada."""
        sys.path.insert(0, str(RAIZ / "tools" / "entrenar"))
        import lib_prompt as lp
        inv = lp.inventario()
        por = {h["nombre"]: h for h in inv["herramientas"]}
        canon = lambda x: json.dumps(x, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        huella = lambda x: hashlib.sha256(canon(x).encode()).hexdigest()[:16]
        nombres = sorted(self.man["perfil"]["herramientas"])
        endpoints = [{"nombre": n, "esquema_huella": huella(por[n]["esquema"]),
                      "parametros": sorted((por[n]["esquema"].get("properties") or {}).keys()),
                      "requeridos": sorted(por[n]["esquema"].get("required") or []),
                      "solo_lectura": bool(por[n].get("soloLectura")), "categoria": por[n].get("categoria")}
                     for n in nombres if n in por]
        ex = self.man.get("exportado", {})
        m = self.man
        out = {
            "schema": 1,
            "modelo": {"repo": repo, "build": m["nombre"], "version": self.a.version or m.get("version"),
                       "fecha": datetime.date.today().isoformat(), "base": m.get("base"),
                       "base_revision": m.get("base_revision"), "commit": m.get("repo_commit"),
                       "corpus": m.get("corpus_firma"), "perfil": m["perfil"].get("nombre"),
                       "archivo": "model.safetensors" if formato_repo == "mlx" else self.gguf_nombre,
                       "cuantizacion": ({"tipo": "mlx-affine", "bits": 4, "grupo": 64,
                                         "dtype": (ex.get("mlx_dtype") or {}).get("tipo")} if formato_repo == "mlx"
                                        else {"tipo": ex.get("cuant", "Q4_K_M")}),
                       "vision": {"incluida": bool(ex.get("vision")),
                                  "archivo": "model.safetensors" if formato_repo == "mlx" else
                                  ("mmproj-F16.gguf" if ex.get("mmproj") else None)},
                       "mtp": ({"archivos": ["mtp.safetensors", "mtp-q8.safetensors"], "normas": "hf-sin-+1"}
                               if formato_repo == "mlx" else {"en_gguf": True}) if m.get("mtp") else None},
            "endpoints": endpoints,
            "endpoints_huella": huella([[e["nombre"], e["esquema_huella"]] for e in endpoints]),
            "esquema_huella_metodo": {"algoritmo": "sha256", "largo": 16, "json": "sort_keys,separators=(',',':'),utf-8",
                                      "de": "inputSchema del endpoint MCP"},
            "inventario_mcp": {"generado": inv.get("generado"), "cuantas": inv.get("cuantas")},
        }
        if m["perfil"].get("formato") == "tc":
            import lib_tc as tc
            out["formato"] = {
                "id": "qwen3.5-tool-call-xml", "version": 1, "prompt_huella": tc.huella(),
                "cabeza": "prompt.json#cabeza", "cola": tc.COLA, "fin_turno": tc.FIN,
                "usuario": {"con_estado": "Estado: {estado}\nPedido: {frase}", "sin_estado": "{frase}",
                            "estado_json": "separators=(',',':'),ensure_ascii=false"},
                # las del perfil con que se entreno (v4: + camaras con posiciones, discursos,
                # medios, preguntas, pantallas, recetas)
                "estado_claves": (tc.ESTADO_CLAVES_V6 if tc.V6 else tc.ESTADO_CLAVES_V5 if tc.V5 else tc.ESTADO_CLAVES_V4 if tc.NUEVO
                                  else tc.ESTADO_CLAVES),
                "llamada": {"abre": "<tool_call>", "funcion": "<function={nombre}>", "parametro":
                            "<parameter={clave}>\n{valor}\n</parameter>", "cierra_funcion": "</function>",
                            "cierra": "</tool_call>", "regex_llamada": tc.RE_LLAMADA.pattern,
                            "regex_parametro": tc.RE_PARAM.pattern},
                "pseudo": {"derivar": {"nombre": tc.DERIVAR, "parametros": []},
                           "falta_dato": {"nombre": tc.FALTA, "parametros": ["pregunta"]}},
                "texto_sin_llamada": "charla",
                "valores": {"cambio": r"^[+-]\d+(\.\d+)?$", "exacto": r"^=-?\d+(\.\d+)?$",
                            "booleano": ["true", "false"], "un_poco": 3, "sin_cantidad": 6},
                "turnos": 1, "tool_response": False}
        else:
            out["formato"] = {"id": "nombre-de-herramienta", "version": 1, "prompt_huella": m["perfil"].get("prompt")}
        sonda = self.dir / "sonda.json"
        if sonda.exists():  # la sonda de derivar: que hacer con sonda.safetensors (ver sonda-exportar.py)
            out["sonda"] = {"archivo": "sonda.safetensors", **json.loads(sonda.read_text())}
        return out

    def empaquetar(self):
        self.completar_datos()
        if self.a.version:
            self.man["version"] = self.a.version
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
                if ex.get("mmproj") and Path(ex["mmproj"]).exists():
                    enlazar(ex["mmproj"], d / "mmproj-F16.gguf")
            if hp.exists():
                shutil.copy(hp, d)
            if (self.dir / "sonda.safetensors").exists():
                shutil.copy(self.dir / "sonda.safetensors", d)
            if lic.exists():
                shutil.copy(lic, d / "LICENSE")
            if eq:
                (d / "equivalencias.json").write_text(json.dumps(eq, ensure_ascii=False, indent=1))
            (d / "prompt.json").write_text(json.dumps(self.prompt_para_servir(), ensure_ascii=False, indent=1))
            mj = self.modelo_json(formato, repo)
            (d / "modelo.json").write_text(json.dumps(mj, ensure_ascii=False, indent=1))
            # copia versionada en el repo del aparato (texto chico, como la tarjeta)
            (RAIZ / "docs" / "modelos").mkdir(parents=True, exist_ok=True)
            (RAIZ / "docs" / "modelos" / "publicados" / f"{repo}.modelo.json").write_text(json.dumps(mj, ensure_ascii=False, indent=1))
            (d / ".gitattributes").write_text("*.safetensors filter=lfs diff=lfs merge=lfs -text\n"
                                              "*.gguf filter=lfs diff=lfs merge=lfs -text\n")
            (d / "README.md").write_text(self.tarjeta(formato, repo), encoding="utf-8")
            repos.append(str(d))
        self.man["repos"] = repos
        # LA TARJETA NO SE PIERDE: ademas de ir en cada repo, queda VERSIONADA en
        # docs/modelos/publicados/ (texto chico) con un resumen de lo que define al
        # modelo. Los pesos no: pesan gigas y van a Hugging Face (.gitignore los excluye).
        doc = RAIZ / "docs" / "modelos" / "publicados"
        doc.mkdir(parents=True, exist_ok=True)
        for d in repos:
            shutil.copy(Path(d) / "README.md", doc / f"{Path(d).name}.md")
        resumen = {k: self.man.get(k) for k in ("base", "base_revision", "base_licencia", "corpus_firma",
                                                 "corpus_ejemplos", "repo_commit", "arquitectura",
                                                 "entrenamiento", "medidas", "mtp")}
        resumen["perfil"] = {k: v for k, v in self.man["perfil"].items() if k != "equivalencias"}
        resumen["repos"] = [Path(d).name for d in repos]
        (doc / f"{nombre}.json").write_text(json.dumps(resumen, ensure_ascii=False, indent=1))
        log(f"tarjeta versionada en docs/modelos/publicados/{nombre}*")
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
            "vision": "vision-hecha", "empaquetar": "empaquetado", "subir": "subido"}[e])]
        self.controlar_espacio(pendientes)
        plan = [("inspeccionar", self.inspeccionar, self.dir / "inspeccionado"),
                ("descargar", self.descargar, self.dir / "descargado"),
                ("corpus", self.hacer_corpus, self.corpus / "entrenar.jsonl"),
                ("entrenar", self.entrenar, self.adaptador / "adapter_config.json"),
                ("medir", self.medir, self.dir / "medido"),
                ("exportar", self.exportar, self.dir / "exportado"),
                ("vision", self.vision, self.dir / "vision-hecha"),
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
    ap.add_argument("--version", help="version de entrenamiento del modelo (va a modelo.json: la compara la app)")
    ap.add_argument("--formato", choices=["nombres", "tc"], default="nombres",
                    help="nombres: el modelo contesta solo la herramienta (lib_prompt); tc: tool calling con "
                         "argumentos (lib_tc, docs/modelos/entrenamiento-avanzado.md)")
    ap.add_argument("--corpus-de", help="tc: la carpeta del corpus ya armado (tc-corpus.py / tc-bucle.py)")
    ap.add_argument("--adaptador-de", help="usar este adaptador ya entrenado en vez de entrenar")
    ap.add_argument("--curva-q4", action="store_true", help="medir el q4 en los 5 puntos del dial (si no, solo sesgo 0)")
    args = ap.parse_args()
    # el perfil viaja por el entorno a TODO lo que corre debajo (corpus,
    # entrenamiento, evaluacion): lib_prompt lo lee de BA_PERFIL
    os.environ["BA_PERFIL"] = args.perfil
    Fabrica(args).correr()


if __name__ == "__main__":
    main()
