#!/usr/bin/env python3
"""Laboratorio de la RED DE DESPUES: derivar cuando la llamada no valida.

LA IDEA (del operador). La sonda y el modelo deciden ANTES de llamar si el pedido
es suyo. Hay errores que el modelo comete seguro (manda «pasa-altos en 80 al mic
2» a audio_set). Muchos de esos se delatan DESPUES: la llamada no cumple el
esquema de la herramienta, o nombra algo que no esta en el estado. Ahi la app
deriva al asistente grande con la frase, la llamada y el error, en vez de
ejecutar algo mal. Es el «escalar si no verifica» de las cascadas.

QUE MIDE, sobre el banco y el examen (la frase con el estado fijo de tc-evaluar):
el modelo genera la llamada ENTERA; se valida como lo haria la app:
  · el nombre es de una herramienta del perfil (o derivar / falta_dato);
  · los argumentos estan en el esquema, estan los obligatorios, el enum vale,
    el tipo coincide (los cambios con signo «+3» valen para los numeros);
  · los nombres de las entidades estan en el estado (escenas, audio, fuentes...).
y cuenta: de los AJENOS que el modelo atendio (no derivo), cuantos atrapa la red;
de los PROPIOS que atendio BIEN, cuantos atrapa sin razon (el costo).

    BA_PERFIL=vivo5 python tools/entrenar/lab-despues.py --modelo <adaptador> --corpus <corpus>
"""
import argparse
import json
import random
import re
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import lib_prompt as lp  # noqa: E402
import lib_tc as tc  # noqa: E402

RE_NUM = re.compile(r"^[+=]?-?\d+(\.\d+)?$")


def nombres_del_estado(est):
    """Todo nombre que el modelo podria copiar del estado."""
    out = set()

    def recorrer(v):
        if isinstance(v, str):
            out.add(v)
        elif isinstance(v, list):
            for x in v:
                recorrer(x)
        elif isinstance(v, dict):
            for k in ("nombre", "ruta", "id", "key", "integrante", "titulo", "autor"):
                if isinstance(v.get(k), str):
                    out.add(v[k])
            for k in ("banca", "capa", "n"):
                if k in v:
                    out.add(str(v[k]))
            for k in ("posiciones",):
                if isinstance(v.get(k), list):
                    out.update(x for x in v[k] if isinstance(x, str))
    for v in est.values():
        recorrer(v)
    return out


# los argumentos que nombran una entidad del estado (si el nombre no esta, la app falla)
ENTIDAD = re.compile(r"^(name|scene|escena|source|fuente|entrada|posicion|persona|participant|nombre_escena|"
                     r"grafico|carrusel|discurso|medio|pregunta|pantalla|receta|banca|a|b|key|mensaje_id)$")


def validar(nombre, args, esquemas, est):
    """El motivo por el que la app rechazaria la llamada, o None."""
    if nombre in (tc.DERIVAR, tc.FALTA):
        return None
    if nombre not in esquemas:
        return f"no existe la herramienta {nombre}"
    esq = esquemas[nombre]
    props = esq.get("properties") or {}
    for r in esq.get("required", []):
        if r not in args:
            return f"falta el obligatorio {r}"
    nombres = nombres_del_estado(est)
    for k, v in args.items():
        if k not in props:
            return f"{k} no esta en el esquema"
        p = props[k]
        t = p.get("type")
        if "enum" in p and str(v) not in [str(x) for x in p["enum"]]:
            return f"{k}={v!r} fuera del enum"
        if t in ("number", "integer") and not RE_NUM.match(str(v).strip()):
            return f"{k}={v!r} no es numero"
        if t == "boolean" and str(v).strip().lower() not in ("true", "false"):
            return f"{k}={v!r} no es booleano"
        if t == "string" and ENTIDAD.match(k) and str(v) not in nombres:
            return f"{k}={v!r} no esta en el estado"
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--modelo", required=True)
    ap.add_argument("--limite-banco", type=int, default=0)
    ap.add_argument("--json", help="donde dejar el resultado")
    args = ap.parse_args()

    import torch
    from transformers import AutoModelForCausalLM, AutoTokenizer
    from peft import PeftModel
    import cache_hibrido as ch
    m_dir = Path(args.modelo).expanduser()
    cfg = json.loads((m_dir / "adapter_config.json").read_text())
    modelo = AutoModelForCausalLM.from_pretrained(cfg["base_model_name_or_path"], dtype=torch.bfloat16,
                                                  device_map={"": "cuda:0"})
    modelo = PeftModel.from_pretrained(modelo, str(m_dir)).merge_and_unload().eval()
    tok = AutoTokenizer.from_pretrained(str(m_dir))
    dev = modelo.device
    ch.parchear()
    ids_cab = tok(tc.cabeza(), add_special_tokens=False, return_tensors="pt").input_ids.to(dev)
    with torch.no_grad():
        kv = modelo(input_ids=ids_cab, use_cache=True).past_key_values
    Lp = ids_cab.shape[1]
    foto = ch.foto(kv)
    fin = tok.convert_tokens_to_ids("<|im_end|>")

    def generar(usuario, max_nuevos=160):
        try:
            with torch.no_grad():
                lg = modelo(input_ids=torch.tensor([tok(usuario + tc.COLA, add_special_tokens=False).input_ids],
                                                   device=dev), past_key_values=kv, use_cache=True).logits[0, -1]
                out = []
                for _ in range(max_nuevos):
                    t = int(lg.argmax())
                    if t == fin:
                        break
                    out.append(t)
                    if tok.decode(out[-6:]).endswith("</tool_call>"):
                        break
                    lg = modelo(input_ids=torch.tensor([[t]], device=dev), past_key_values=kv,
                                use_cache=True).logits[0, -1]
            return tok.decode(out)
        finally:
            ch.volver(kv, foto, Lp)

    esquemas = {f["function"]["name"]: f["function"]["parameters"] for f in tc.funciones()}
    conocidas = set(lp.herramientas_reactivas())
    est, _ = tc.estado_sintetico(random.Random(0))  # el mismo estado fijo que tc-evaluar
    res = {}
    for nombre_conj, archivo in (("banco", RAIZ / "datos/orientacion.jsonl"),
                                 ("examen", RAIZ / "datos/entrenamiento/examen.jsonl")):
        casos = [json.loads(l) for l in archivo.read_text().splitlines() if l.strip()]
        if nombre_conj == "banco" and args.limite_banco and len(casos) > args.limite_banco:
            paso = len(casos) / args.limite_banco
            casos = [casos[int(i * paso)] for i in range(args.limite_banco)]
        c = {"ajenos": 0, "ajenos_atendidos": 0, "ajenos_atrapados": 0, "propios": 0, "propios_bien": 0,
             "propios_bien_atrapados": 0, "propios_mal": 0, "propios_mal_atrapados": 0}
        ejemplos = {"atrapados": [], "de_mas": []}
        for x in casos:
            acept = x.get("aceptables") or ([x["herramienta"]] if x["herramienta"] in conocidas else [tc.DERIVAR])
            salida = generar(tc.usuario(est, x["frase"]))
            leida = tc.leer(salida)  # (nombre, args), o None si no hay una llamada legible
            nombre, a = leida if leida else (None, {})
            motivo = validar(nombre, a, esquemas, est) if nombre else "no hay llamada"
            if tc.DERIVAR in acept:
                c["ajenos"] += 1
                if nombre not in (tc.DERIVAR, tc.FALTA):
                    c["ajenos_atendidos"] += 1
                    if motivo:
                        c["ajenos_atrapados"] += 1
                        if len(ejemplos["atrapados"]) < 8:
                            ejemplos["atrapados"].append([x["frase"], nombre, motivo])
            else:
                c["propios"] += 1
                if nombre in acept:
                    c["propios_bien"] += 1
                    if motivo:
                        c["propios_bien_atrapados"] += 1
                        if len(ejemplos["de_mas"]) < 8:
                            ejemplos["de_mas"].append([x["frase"], nombre, motivo])
                elif nombre not in (tc.DERIVAR, tc.FALTA):
                    c["propios_mal"] += 1
                    c["propios_mal_atrapados"] += bool(motivo)
        c["red_atrapa_ajenos"] = c["ajenos_atrapados"] / max(1, c["ajenos_atendidos"])
        c["red_atrapa_propios_mal"] = c["propios_mal_atrapados"] / max(1, c["propios_mal"])
        c["red_molesta_propios_bien"] = c["propios_bien_atrapados"] / max(1, c["propios_bien"])
        res[nombre_conj] = {"cuentas": c, "ejemplos": ejemplos}
        print(nombre_conj, json.dumps(c, ensure_ascii=False), flush=True)
    if args.json:
        Path(args.json).write_text(json.dumps(res, ensure_ascii=False, indent=1))


if __name__ == "__main__":
    main()
