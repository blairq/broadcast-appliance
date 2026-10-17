#!/usr/bin/env python3
"""tools/entrenar/voz-banco.py — el banco del modelo local con el aparato andando.

Para cada herramienta reactiva (datos/semillas-reactivas.json), N frases:
  1. elegir: ¿el modelo local elige la herramienta esperada?
  2. argumentos: generar con las reglas de datos/voz-argumentos.txt y el
     estado real del aparato (por su MCP), y la misma validación que
     agent/ArgumentosLocales (nombres que existen, relativos, enum, requeridos);
  3. ejecución: SOLO las herramientas seguras (lectura, audio, escenas,
     previa, transición, gráficos, oradores), y verificación del efecto
     (audio_set: el mezclador; scene_switch: la escena al aire). Después de
     cada una se restaura el mezclador y se vuelve a «Principal».

Nunca ejecuta transmisión, grabación, PIN, gateway ni nada destructivo.
Requiere la app abierta (MCP en 127.0.0.1:8790) y el modelo bajado.
Resultado: build/voz-banco.json; resumen: docs/modelos/voz-modelo-local.md.

    python3 tools/entrenar/voz-banco.py --por 3
    python3 tools/entrenar/voz-banco.py --solo audio_set,scene_switch --por 6
"""
# Banco en vivo: frase -> elegir -> argumentos (modelo local) -> validación -> ejecución (las seguras) -> verificación.
import argparse, json, os, subprocess, sys, time, random, difflib, re, unicodedata, urllib.request
RAIZ=os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument("--app", default=os.path.join(RAIZ,"dist","BroadcastAppliance.app","Contents","Resources"))
ap.add_argument("--modelo", default="BlairQ__broadcast-reactivo-qwen3.5-4b-mtp-ud-mlx-q4")
ap.add_argument("--por", type=int, default=3, help="frases por herramienta")
ap.add_argument("--solo", default="", help="herramientas separadas por coma")
ap.add_argument("--salida", default=os.path.join(RAIZ,"build","voz-banco.json"))
ap.add_argument("--sistema", default=os.path.join(RAIZ,"datos","voz-argumentos.txt"))
A=ap.parse_args()
R=A.app; SYS=open(A.sistema).read(); POR=A.por; SALIDA=A.salida; SOLO=A.solo.split(",") if A.solo else None
TOK=subprocess.run([R+"/bin/64bit/ba-mcp","--token"],capture_output=True,text=True).stdout.strip()
_sid=[None]
def _call(method, params):
    h={"Content-Type":"application/json","Accept":"application/json, text/event-stream","Authorization":"Bearer "+TOK}
    if _sid[0]: h["Mcp-Session-Id"]=_sid[0]
    r=urllib.request.urlopen(urllib.request.Request("http://127.0.0.1:8790/mcp",data=json.dumps({"jsonrpc":"2.0","id":1,"method":method,"params":params}).encode(),headers=h),timeout=120)
    s=r.headers.get("Mcp-Session-Id")
    if s: _sid[0]=s
    return json.loads(r.read().decode())
_call("initialize",{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"voz-banco","version":"1"}})
def _sesion_nueva():
    _sid[0]=None
    _call("initialize",{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"voz-banco","version":"1"}})
def tool(n,a=None):
    # Cada llamada en su sesión: el MCP «contamina» una sesión que leyó algo
    # de invitados o del público (chat, capturas) y desde ahí cada acción pide
    # aprobación a mano. Una sesión limpia por llamada es como un cliente
    # nuevo: la política de aprobación no se toca.
    try:
        _sesion_nueva()
        r=_call("tools/call",{"name":n,"arguments":a or {}})["result"]
    except Exception as e:  # una llamada lenta o caída no corta el banco
        return False, "falló la llamada: %s"%e
    t="".join(c.get("text","") for c in r.get("content",[]) if c.get("type")=="text")
    try: v=json.loads(t)
    except Exception: v=t
    return (not r.get("isError",False)), v
TODAS={t["name"]:t for t in _call("tools/list",{})["result"]["tools"]}
SEM=json.load(open(os.path.join(RAIZ,"datos","semillas-reactivas.json")))["herramientas"]
EJEC={"audio_set","audio_escuchar","scene_switch","scene_preview","preview_al_aire","transition_set",
      "grafico_mostrar","grafico_ocultar","laminas_ir","speaker_next","speaker_time","speaker_on_air","speaker_move",
      "speaker_presenter","speaker_queue","scene_folder","scene_move","scene_protect"}
for n,t in TODAS.items():
    a=t.get("annotations",{})
    if a.get("readOnlyHint") and n not in ("chat_wait",): EJEC.add(n)
p=subprocess.Popen([R+"/bin/64bit/ba-modelo","--modelo",R+"/Modelos/"+A.modelo,"--mtp","no"],
                   stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,text=True,bufsize=1)
def pedir(d):
    p.stdin.write(json.dumps(d)+"\n"); p.stdin.flush()
    while True:
        j=json.loads(p.stdout.readline())
        if j.get("id")==d["id"] and ("fin" in j or "error" in j or "herramienta" in j): return j
while json.loads(p.stdout.readline()).get("ev")!="listo": pass

def estado():
    _,sl=tool("scene_list"); _,src=tool("escena_fuentes_ver"); _,mx=tool("mixer_list"); _,rs=tool("room_status"); _,gr=tool("grafico_listar"); _,la=tool("carruseles_listar")
    return {"escena_actual":sl.get("current"),"escena_armada":sl.get("armada",""),
            "escenas":[e["name"] for e in sl.get("scenes",[])],
            "fuentes_de_la_escena_actual":[{"nombre":s["name"],"visible":s.get("visible",True)} for s in src.get("sources",[])],
            "audio":[{"nombre":s["name"],"volumen_db":round(s.get("volumeDb",0),1),"silenciada":s.get("muted",False)} for s in mx.get("sources",[])],
            "participantes":[{"id":x.get("id"),"nombre":x.get("name"),"estado":x.get("state","")} for x in rs.get("participants",[])],
            "graficos":[g.get("nombre") or g.get("name") or g.get("id") for g in (gr.get("graficos",[]) if isinstance(gr,dict) else [])],
            "carruseles":[c if isinstance(c,str) else c.get("nombre") for c in (la.get("carruseles",[]) if isinstance(la,dict) else [])]}
def estado_para(h, E):
    """La parte del estado que usa la herramienta (igual que agent/ArgumentosLocales::estadoPara)."""
    def con(*ks): return {k: E[k] for k in ks if k in E}
    if h.startswith(("scene_","transition_")): return con("escena_actual","escena_armada","escenas")
    if h.startswith(("audio","mixer_")): return con("audio")
    if h.startswith(("source_","foto_fuente")): return con("escena_actual","escenas","fuentes_de_la_escena_actual","audio")
    if h.startswith(("participant_","speaker_","chat_","foto_invitado")): return con("participantes")
    if h.startswith(("grafico_","rotulo_")): return con("graficos","escena_actual")
    if h.startswith("laminas_"): return con("carruseles","escenas","escena_actual")
    return E
def norm(s): return "".join(c for c in unicodedata.normalize("NFD",str(s).lower()) if unicodedata.category(c)!="Mn").strip()
NOMBRE_KEYS=("carrusel","name","source","scene","nombre","fuente","escena","target","grafico","lamina","to","from","id","participant","sceneName","sourceName")
def nombres(E, herr, clave=""):
    """Los nombres que puede querer decir: según qué maneja la herramienta."""
    audio=[a["nombre"] for a in E["audio"]]; escenas=E["escenas"]; fuentes=[f["nombre"] for f in E["fuentes_de_la_escena_actual"]]
    gente=[x["nombre"] for x in E["participantes"] if x["nombre"]]; graf=[g for g in E["graficos"] if g]
    if "scene" in clave.lower() or herr.startswith("scene_") and clave in ("name","to","from","nombre"): return escenas
    if herr.startswith("audio") or herr=="mixer_list": return audio
    if herr.startswith(("source_","foto_fuente")): return fuentes+audio
    if herr.startswith(("grafico_","rotulo_")): return graf
    if herr.startswith("laminas_") and clave=="carrusel": return E.get("carruseles",[])
    if herr.startswith(("participant_","speaker_","foto_invitado")): return gente
    return escenas+fuentes+audio+gente+graf
def ajustar(v, cand):
    """El nombre que existe: igual (sin mayúsculas ni acentos), contenido sin ambigüedad, o muy parecido."""
    if v in cand: return v, None
    nv=norm(v)
    for c in cand:
        if norm(c)==nv: return c, "nombre normalizado: %s -> %s"%(v,c)
    sub=[c for c in cand if nv and (nv in norm(c) or norm(c) in nv)]
    if len(sub)==1: return sub[0], "nombre completado: %s -> %s"%(v,sub[0])
    m=difflib.get_close_matches(nv,[norm(c) for c in cand],n=1,cutoff=0.8)
    if m:
        c=[c for c in cand if norm(c)==m[0]][0]; return c, "nombre parecido: %s -> %s"%(v,c)
    return None, "no existe: "+v
def leer(crudo, h):
    """El objeto JSON: claves sin comillas, envuelto en el nombre de la herramienta, o solo el nombre."""
    m=re.search(r"\{.*\}",crudo,re.S)
    if not m: return None
    t=m.group(0)
    if re.fullmatch(r"\{\s*"+re.escape(h)+r"[^{}\"]*\}",t): return {}
    t=re.sub(r"^\{\s*"+re.escape(h)+r"\s*,", "{", t)
    for intento in (t, re.sub(r'([{,]\s*)([A-Za-z_]\w*)\s*:', r'\1"\2":', t)):
        try: a=json.loads(intento)
        except Exception: continue
        if isinstance(a,dict) and len(a)==1 and h in a: a=a[h] if isinstance(a[h],dict) else {}
        return a if isinstance(a,dict) else None
    return None
CREA=re.compile(r"_(create|add|copy|rename|folder|define)$")
def validar(h, args, E):
    sch=TODAS[h].get("inputSchema",{}); props=sch.get("properties",{}); notas=[]; out={}
    if isinstance(args,dict) and len(args)==1 and list(args)[0] not in props and isinstance(list(args.values())[0],dict):
        notas.append("desenvuelto de «%s»"%list(args)[0]); args=list(args.values())[0]
    vol={a["nombre"]:a["volumen_db"] for a in E["audio"]}
    for k,v in args.items():
        if k not in props: notas.append("clave de más: "+k); continue
        t=props[k].get("type")
        if isinstance(v,str) and v=="": notas.append("vacío: afuera "+k); continue
        if isinstance(v,str) and (v==h or (re.fullmatch(r"[a-z]+(_[a-z]+)+",v) and "enum" not in props[k]) or re.fullmatch(r".*:[A-Z]",v)):
            notas.append("«%s» no es un dato: afuera %s"%(v,k)); continue
        desc=props[k].get("description","")
        especial=isinstance(v,str) and bool(v) and re.search(r"(^|[^\w])"+re.escape(v)+r"([^\w]|$)",desc) is not None
        nuevo=CREA.search(h) is not None and k in ("name","newName","nuevo","nombre","to","folder")
        if t=="string" and isinstance(v,str) and "enum" not in props[k] and not especial and not nuevo and (k in NOMBRE_KEYS or k.lower().endswith("name")):
            nv,nota=ajustar(v,nombres(E,h,k))
            if nota: notas.append(nota)
            if nv is None: out[k]=v; continue
            v=nv
        if isinstance(v,str) and t in ("number","integer") and re.fullmatch(r"\s*[+-]\d+(\.\d+)?\s*",v):
            d=float(v); fuente=out.get("name") or args.get("name")
            if k.lower().startswith("volume") and fuente in vol: v=round(vol[fuente]+d,1); notas.append("relativo %s%s"%(("+" if d>0 else ""),d))
            else: notas.append("con signo, sin valor actual: %s=%s"%(k,v)); v=d
        if "enum" in props[k] and v not in props[k]["enum"]: notas.append("fuera del enum: %s=%s"%(k,v))
        out[k]=v
    for r in sch.get("required",[]):
        if r not in out: notas.append("falta requerido: "+r)
    return out, notas
def verificar(h,args):
    if h=="audio_set":
        _,mx=tool("mixer_list"); s=[x for x in mx["sources"] if x["name"]==args.get("name")]
        if not s: return "fuente no encontrada"
        s=s[0]; malos=[]
        if "muted" in args and s["muted"]!=args["muted"]: malos.append("muted=%s"%s["muted"])
        if "volumeDb" in args and abs(s["volumeDb"]-float(args["volumeDb"]))>0.6: malos.append("volumeDb=%.1f"%s["volumeDb"])
        return "ok" if not malos else "no quedó: "+",".join(malos)
    if h=="scene_switch":
        _,sl=tool("scene_list"); return "ok" if sl.get("current")==args.get("name") else "escena actual: %s"%sl.get("current")
    return ""
BASE=None
def restaurar():
    _,mx=tool("mixer_list")
    for s in mx["sources"]:
        o=BASE[s["name"]] if s["name"] in BASE else None
        if o and (abs(o["volumeDb"]-s["volumeDb"])>0.1 or o["muted"]!=s["muted"]):
            tool("audio_set",{"name":s["name"],"volumeDb":o["volumeDb"],"muted":o["muted"]})
    _,sl=tool("scene_list")
    if sl.get("current")!="Principal": tool("scene_switch",{"name":"Principal"})
_,mx0=tool("mixer_list"); BASE={s["name"]:s for s in mx0["sources"]}
res=[]; n=0; random.seed(11)
for h,sem in SEM.items():
    if SOLO and h not in SOLO: continue
    frases=sem["frases"][:]; random.shuffle(frases)
    for f in frases[:POR]:
        n+=1; E=estado()
        e=pedir({"id":n,"op":"elegir","frase":f,"sesgo":0}); hh=e.get("herramienta")
        fila={"esperada":h,"frase":f,"elegida":hh,"prob":round(e.get("prob") or 0,3),"ms_elegir":e.get("ms")}
        if hh in TODAS and hh!="derivar":
            t=TODAS[hh]; args={}
            if t.get("inputSchema",{}).get("properties"):
                user=("Herramienta: %s — %s\n%s\nEsquema de argumentos: %s\n\nEstado del aparato: %s\n\nPedido del operador: «%s»"
                      %(hh,t.get("title",""),t.get("description","")[:700],json.dumps(t["inputSchema"],ensure_ascii=False),json.dumps(estado_para(hh,E),ensure_ascii=False),f))
                texto="<|im_start|>system\n"+SYS+"<|im_end|>\n<|im_start|>user\n"+user+"<|im_end|>\n<|im_start|>assistant\n<think>\n\n</think>\n\n{"
                n+=1; t0=time.time(); mx=256 if any(pp.get("type") in ("array","object") for pp in t["inputSchema"].get("properties",{}).values()) else 96
                g=pedir({"id":n,"op":"generar","texto":texto,"prefijo":False,"max":mx})
                crudo="{"+g.get("texto",""); fila["ms_args"]=round((time.time()-t0)*1000); fila["crudo"]=crudo.strip()
                a=leer(crudo,hh)
                if a is None or not isinstance(a,dict): fila["notas"]=["no es JSON"]; args=None
                else: args,fila["notas"]=validar(hh,a,E)
            fila["args"]=args
            bloqueo=[x for x in fila.get("notas",[]) if x.startswith(("no existe","falta requerido","no es JSON","fuera del enum"))]
            if args is not None and hh in EJEC and not bloqueo:
                ok,v=tool(hh,args); fila["ejecutada"]=True; fila["ok"]=ok; fila["respuesta"]=(json.dumps(v,ensure_ascii=False) if not isinstance(v,str) else v)[:240]
                fila["verificacion"]=verificar(hh,args)
                restaurar()
            else:
                fila["ejecutada"]=False; fila["motivo"]=("bloqueado: "+"; ".join(bloqueo)) if bloqueo else ("no se ejecuta en la prueba (%s)"%("no está en la lista segura" if hh not in EJEC else "sin args"))
        res.append(fila)
        print("%-22s %-22s %-3s %-46s %-60s %s"%(h[:22],(hh or "")[:22],"OK" if hh==h else "--",f[:46],json.dumps(fila.get("args"),ensure_ascii=False)[:60],
              ("EJEC %s %s"%("ok" if fila.get("ok") else "ERROR",fila.get("verificacion","")) if fila.get("ejecutada") else fila.get("motivo",""))[:70]),flush=True)
        json.dump(res,open(SALIDA,"w"),ensure_ascii=False,indent=1)
p.terminate()
