"""Bosch DCN Next Generation (Open Interface): micrófonos de banca, pedidos de palabra y oradores.

Ver plugin.json y docs/bosch-dcn-ng-open-interface.md. El puerto serie (o el socket) lo maneja un
solo hilo de E/S NO BLOQUEANTE (dcn/transporte.py); acá nada espera adentro de ese hilo: los
avisos de la CCU se convierten en eventos del bus y los controles que pide la app esperan el
resultado de su pedido desde su propio hilo (los del SDK), con tiempo máximo.

Eventos (el bus los publica como ``plugin/bosch-dcn/<canal>``):
  conferencia/microfono {banca, abierto, unidad, origen}   el estándar que usan Seguir al micrófono,
                                                            la fila de oradores y el Recinto
  conferencia/pedido {banca, unidad, accion: alta|baja}
  conferencia/siguiente {banca, unidad}                     el primero de la lista de pedidos cambió
  dcn/estado {...}                                          el estado completo (guía §7.2)
  dcn/enlace {enlace, motivo}
"""
from __future__ import annotations

import json
import socket
import threading
import time
from collections import deque
from concurrent.futures import ThreadPoolExecutor

import ba_plugin as ba
from dcn import mensajes as m
from dcn.ccu import Ccu, ErrorCcu
from dcn.simulador import Simulador
from dcn.transporte import FlujoSerie, FlujoSocket, SerieFull, Tcp

plugin = ba.Plugin()
c = plugin.config
ESPERA_S = 5.0  # lo que espera un control (desde SU hilo) la respuesta de la CCU

estado = {"enlace": "caido", "motivo": "sin conectar", "desde": time.time()}
ccu: Ccu | None = None
transporte = None
sim: Simulador | None = None
mapa_u2b: dict[int, str] = {}       # unidad -> banca (del Cuerpo: dcn_unidad)
nombres: dict[str, str] = {}        # banca -> nombre
aprender = {"banca": None, "siguientes": []}
traza = deque(maxlen=400)
pendiente_estado = threading.Event()
sincronizada = threading.Event()  # la CCU está lista para órdenes
lock = threading.RLock()
trabajos = ThreadPoolExecutor(max_workers=2, thread_name_prefix="dcn-trabajo")  # lo que sí puede esperar
generacion = [0]  # cada conexión nueva invalida la anterior
publicado: dict[int, bool] = {}  # unidad -> último abierto publicado (la CCU avisa lo mismo dos veces)


# --- Utilidades ---------------------------------------------------------------------------------

def registrar(texto: str) -> None:
    linea = time.strftime("%Y-%m-%d %H:%M:%S ") + texto
    try:
        with open(plugin.datos / "dcn.log", "a", encoding="utf-8") as f:
            f.write(linea + "\n")
    except OSError:
        pass


def banca_de(u: int) -> str:
    return mapa_u2b.get(u, "")


def unidad_de(args: dict) -> int:
    if args.get("unidad") not in (None, ""):
        return int(args["unidad"])
    b = str(args.get("banca", "")).strip()
    for u, bb in mapa_u2b.items():
        if bb.lower() == b.lower():
            return u
    if not b:
        raise ValueError("falta la banca")
    raise ValueError(f"la banca {b} no tiene unidad de la CCU asignada (Conferencia → Unidades → Aprender)")


def esperar(fut, seg: float = ESPERA_S):
    try:
        return fut.result(timeout=seg)
    except ErrorCcu as e:
        raise RuntimeError(str(e)) from None
    except TimeoutError:
        raise RuntimeError(f"la CCU no terminó en {seg:.0f} s (sigue en la cola)") from None


def requiere_ccu() -> Ccu:
    # Recién conectada: el control (no el hilo serie) espera un poco a que termine de sincronizar.
    if estado["enlace"] == "sincronizando":
        sincronizada.wait(ESPERA_S)
    if ccu is None or estado["enlace"] != "ok":
        raise RuntimeError("sin control de la CCU: " + estado.get("motivo", "no conectada"))
    return ccu


def con_banca(u: int, extra: dict | None = None) -> dict:
    d = {"unidad": u, "banca": banca_de(u)}
    if d["banca"] and d["banca"] in nombres:
        d["nombre"] = nombres[d["banca"]]
    d.update(extra or {})
    return d


def foto() -> dict:
    """El estado de la guía §7.2, con bancas."""
    with lock:
        e = ccu.estado if ccu else None
        out = {"enlace": estado["enlace"], "motivo": estado.get("motivo", ""),
               "solo_escuchar": bool(c.get("solo_escuchar", True)),
               "transporte": c.get("transporte", "serie"), "aprendiendo": aprender["banca"]}
        if e is None:
            return out
        with e.lock:
            aj = dict(e.ajustes)
            oradores = [list(o) for o in e.oradores]
            pedidos = list(e.pedidos)
            unidades = {u: dict(x) for u, x in e.unidades.items()}
        modo = aj.get("modo")
        out.update({
            "ccu": {"version": e.version},
            "sincronizado": e.sincronizado,
            "modo": m.MODO_NOMBRE.get(modo, str(modo) if modo is not None else ""),
            "modo_operador": modo in m.MODOS_OPERADOR,
            "max_abiertos": aj.get("max_abiertos"), "max_pedidos": aj.get("max_pedidos"),
            "unidades": [{"unidad": u, "tipo": "presidente" if (x["tipo"] & 0xF0) == m.TIPO_PRESIDENTE else "delegado",
                          "conectada": x["conectada"], "banca": banca_de(u)} for u, x in sorted(unidades.items())],
            "abiertos": [banca_de(u) or f"u{u}" for u, on in oradores if on],
            "abiertos_unidades": [u for u, on in oradores if on],
            "hablando": [con_banca(u, {"microfono": bool(on)}) for u, on in oradores],
            "pedidos": [con_banca(u, {"delegado": d, "turno": i + 1}) for i, (u, d) in enumerate(pedidos)],
            "siguiente": (banca_de(pedidos[0][0]) or f"u{pedidos[0][0]}") if pedidos else "",
            "bancas_sin_unidad": sorted(set(nombres) - set(mapa_u2b.values())),
            "unidades_sin_banca": [u for u in sorted(unidades) if u not in mapa_u2b],
            "contadores": dict(transporte.contadores) if transporte else {},
        })
        return out


def publicar_estado() -> None:
    pendiente_estado.set()


@plugin.cada(0.2)
def _publicar_si_cambio():
    # Los avisos de la CCU llegan en ráfagas: el estado completo sale como mucho 5 veces por segundo.
    if pendiente_estado.is_set():
        pendiente_estado.clear()
        f = foto()
        plugin.evento("dcn/estado", f)
        plugin.mostrar("enlace", {"ok": "conectada", "sincronizando": "sincronizando…"}.get(f["enlace"], "sin CCU: " + f.get("motivo", "")))
        plugin.mostrar("abiertos", ", ".join(f.get("abiertos", [])) or "ninguno")
        plugin.mostrar("pedidos", ", ".join(p["banca"] or f"u{p['unidad']}" for p in f.get("pedidos", [])) or "ninguno")


# --- Avisos de la CCU (desde el hilo de E/S: rápido, sin esperas) ---------------------------------

def al_cambio(que: str, d: dict) -> None:
    if que == "microfono":
        u = d["unidad"]
        if aprender["banca"] and d.get("origen") == "ccu" and d.get("abierto"):
            trabajos.submit(asignar_aprendida, u)
        if publicado.get(u) != d["abierto"]:
            publicado[u] = d["abierto"]
            plugin.evento("conferencia/microfono", con_banca(u, {"abierto": d["abierto"], "origen": d.get("origen")}))
    elif que == "pedidos":
        if "alta" in d:
            u = d["alta"]
            if aprender["banca"]:
                trabajos.submit(asignar_aprendida, u)
            plugin.evento("conferencia/pedido", con_banca(u, {"accion": "alta"}))
        if "primero" in d:
            plugin.evento("conferencia/siguiente", con_banca(d["primero"]))
    elif que == "modo":
        plugin.evento("conferencia/modo", foto().get("modo"))
    elif que == "todo":
        publicado.clear()
        publicado.update({u: True for u in ccu.estado.abiertos()} if ccu else {})
    elif que == "reinicio":
        registrar("la CCU se reinició: se resincroniza")
        trabajos.submit(sincronizar, generacion[0])
    publicar_estado()


# --- Conexión -----------------------------------------------------------------------------------

def al_traza(sentido: str, b: bytes) -> None:
    if c.get("traza"):
        traza.append(f"{time.strftime('%H:%M:%S')} {sentido} {b.hex(' ')}")


def abrir():
    """Crea el transporte según la configuración. No bloquea más que lo que tarda abrir el puerto."""
    global sim
    t = c.get("transporte", "serie")
    latido = float(c.get("latido_s", 5))
    esc = int(str(c.get("escape", "0xC8")), 0)
    if t == "simulador":
        a, b = socket.socketpair()
        sim = Simulador(SerieFull(FlujoSocket(b), escape=esc, latido_s=latido),
                        unidades=int(c.get("simulador_unidades", 12)),
                        actividad_s=float(c.get("simulador_actividad_s", 0)))
        sim.arrancar()
        return SerieFull(FlujoSocket(a), escape=esc, latido_s=latido, traza=al_traza), "simulador"
    sim = None
    if t == "tcp":
        s = socket.create_connection((c["host"], int(c.get("puerto_tcp", 9451))), timeout=5)
        return Tcp(FlujoSocket(s), latido_s=latido, fn_en_datos=bool(c.get("tcp_fn_en_datos", False)),
                   traza=al_traza), f"{c['host']}:{c.get('puerto_tcp', 9451)}"
    puerto = c.get("puerto", "")
    if not puerto:
        raise RuntimeError("falta elegir el puerto serie (Conferencia → Conexión)")
    if c.get("protocolo", "full") != "full":
        raise RuntimeError("el protocolo «simple» todavía no está (guía §3.3): usá «full»")
    f = FlujoSerie(puerto, int(c.get("velocidad", 19200)))
    return SerieFull(f, escape=esc, latido_s=latido, reintentos=int(c.get("reintentos", 2)),
                     duplicar_escape=bool(c.get("duplicar_escape", True)), traza=al_traza), puerto


def sincronizar(gen: int) -> None:
    if gen != generacion[0] or ccu is None:
        return
    estado.update(enlace="sincronizando")
    publicar_estado()
    try:
        ccu.sincronizar().result(timeout=30)
    except Exception as e:  # noqa: BLE001
        registrar(f"no se pudo sincronizar: {e}")
        estado.update(enlace="caido", motivo=f"no sincronizó: {e}")
        plugin.estado(f"la CCU no sincronizó: {e}", ok=False)
        if transporte:
            transporte.parar()
        return
    estado.update(enlace="ok", motivo="", desde=time.time())
    sincronizada.set()
    plugin.estado(f"conectada ({ccu.estado.version}){' · solo escuchar' if ccu.solo_escuchar else ''}")
    plugin.evento("dcn/enlace", {"enlace": "ok"})
    registrar(f"sincronizada: {ccu.estado.version}, {len(ccu.estado.unidades)} unidades")
    publicar_estado()


def conectar() -> None:
    """Conecta (o reconecta). Si falla, el vigía reintenta cada 5 s."""
    global ccu, transporte
    desconectar("conectando", avisar=False)
    sincronizada.clear()
    estado.update(enlace="sincronizando", motivo="conectando")
    generacion[0] += 1
    gen = generacion[0]
    try:
        t, donde = abrir()
    except Exception as e:  # noqa: BLE001
        estado.update(enlace="caido", motivo=str(e))
        plugin.estado(f"sin CCU: {e}", ok=False)
        publicar_estado()
        return
    nuevo = Ccu(t, solo_escuchar=bool(c.get("solo_escuchar", True)), registro=registrar)
    nuevo.al_cambio = al_cambio

    def cayo(motivo: str) -> None:
        nuevo.fallar_todo(motivo)
        if gen != generacion[0]:
            return
        estado.update(enlace="caido", motivo=motivo)
        registrar(f"enlace caído: {motivo}")
        plugin.estado(f"sin control de la CCU: {motivo}", ok=False)
        plugin.evento("dcn/enlace", {"enlace": "caido", "motivo": motivo})
        publicar_estado()

    t.al_caer = cayo
    with lock:
        ccu, transporte = nuevo, t
    t.arrancar()
    registrar(f"conectando por {c.get('transporte', 'serie')} ({donde})")
    trabajos.submit(sincronizar, gen)


def desconectar(motivo: str = "desconectada", avisar: bool = True) -> None:
    global ccu, transporte, sim
    with lock:
        t, s = transporte, sim
        ccu, transporte, sim = None, None, None
    if t:
        t.parar()
    if s:
        s.parar()
    if avisar or motivo != "conectando":
        estado.update(enlace="caido", motivo=motivo)
    if avisar:
        plugin.estado(motivo, ok=False)
        publicar_estado()


@plugin.cada(5)
def _vigia():
    if c.get("activo", True) and estado["enlace"] == "caido" and estado.get("motivo") != "desconectada":
        conectar()


@plugin.cada(60)
def _mapa():
    trabajos.submit(leer_mapa)


@plugin.escuchar("cuerpo/cambio")
def _cuerpo_cambio(_datos, _de):
    # El Cuerpo cambió en la app (una unidad asignada, un nombre): el mapa se relee ya.
    trabajos.submit(leer_mapa)


def leer_mapa() -> None:
    """El mapa unidad↔banca y los nombres, del Cuerpo (dcn_unidad de cada integrante)."""
    try:
        r = plugin.llamar("cuerpo_listar", {}, timeout=10)
    except RuntimeError:
        return
    u2b, nom = {}, {}
    for i in (r or {}).get("integrantes", []):
        b = str(i.get("banca", "")).strip()
        if not b:
            continue
        nom[b] = i.get("nombre", "")
        try:
            if i.get("dcn_unidad") not in (None, ""):
                u2b[int(i["dcn_unidad"])] = b
        except (TypeError, ValueError):
            pass
    if u2b != mapa_u2b or nom != nombres:
        mapa_u2b.clear()
        mapa_u2b.update(u2b)
        nombres.clear()
        nombres.update(nom)
        publicar_estado()


def asignar_aprendida(u: int) -> None:
    banca = aprender["banca"]
    if not banca:
        return
    asignar(banca, u)
    plugin.aviso(f"Banca {banca} = unidad {u} de la CCU", "success")
    aprender["banca"] = aprender["siguientes"].pop(0) if aprender["siguientes"] else None
    publicar_estado()


def asignar(banca: str, u: int) -> None:
    r = plugin.llamar("legislador_ver", {"clave": banca}, timeout=10) or {}
    if not r.get("id"):
        raise RuntimeError(f"no hay nadie con la banca {banca} en el Cuerpo")
    # La unidad es de UNA banca: si otra la tenía, se la saca.
    vieja = mapa_u2b.get(u)
    if vieja and vieja != banca:
        rv = plugin.llamar("legislador_ver", {"clave": vieja}, timeout=10) or {}
        if rv.get("id"):
            plugin.llamar("legislador_editar", {"id": rv["id"], "dcn_unidad": ""}, timeout=10)
    plugin.llamar("legislador_editar", {"id": r["id"], "dcn_unidad": str(u)}, timeout=10)
    registrar(f"unidad {u} asignada a la banca {banca}")
    leer_mapa()


# --- Panel --------------------------------------------------------------------------------------

plugin.panel([
    ba.seccion("Central DCN"),
    ba.indicador("enlace", "Enlace"),
    ba.indicador("abiertos", "Micrófonos abiertos"),
    ba.indicador("pedidos", "Pidieron la palabra"),
], "Bosch DCN")


# --- Controles (la app los expone como herramientas dcn_*) ----------------------------------------

def control(nombre, descripcion, parametros=None):
    # Un error se tira como excepción: el SDK lo devuelve como respuesta fallida (las herramientas
    # dcn_* de la app le sacan el «RuntimeError: » de adelante).
    def deco(fn):
        return plugin.control(nombre, descripcion, parametros or {})(lambda args: fn(args or {}) or {})
    return deco


@control("estado", "El estado completo de la CCU (enlace, modo, unidades, abiertos, pedidos, oradores).")
def _estado(_a):
    return foto()


@control("conectar", "Conecta (o reconecta) con la configuración dada; lo que venga se guarda.",
         {"transporte": {"type": "string", "enum": ["serie", "tcp", "simulador"]}, "puerto": {"type": "string"},
          "velocidad": {"type": "integer"}, "protocolo": {"type": "string"}, "host": {"type": "string"},
          "puerto_tcp": {"type": "integer"}, "solo_escuchar": {"type": "boolean"}, "escape": {"type": "string"},
          "traza": {"type": "boolean"}})
def _conectar(a):
    claves = ("transporte", "puerto", "velocidad", "protocolo", "host", "puerto_tcp", "solo_escuchar", "escape", "traza",
              "simulador_actividad_s", "simulador_unidades")
    cambios = {k: a[k] for k in claves if k in a}
    if cambios:
        c.update(cambios)
        guardar_config()
    c["activo"] = True
    conectar()
    return {"enlace": estado["enlace"], "motivo": estado.get("motivo", "")}


def guardar_config() -> None:
    try:
        p = plugin.dir / "plugin.json"
        man = json.loads(p.read_text("utf-8"))
        man["config"] = {k: v for k, v in c.items() if k != "activo"}
        p.write_text(json.dumps(man, ensure_ascii=False, indent=2), "utf-8")
    except (OSError, ValueError) as e:
        registrar(f"no se pudo guardar la configuración: {e}")


@control("desconectar", "Suelta la CCU (la sala sigue andando sola).")
def _desconectar(_a):
    c["activo"] = False
    desconectar("desconectada")
    return {}


@control("puertos", "Los puertos serie del equipo (nombre, descripción, USB VID:PID).")
def _puertos(_a):
    try:
        from serial.tools import list_ports
    except ImportError:
        raise RuntimeError("falta pyserial en el bundle") from None
    return {"puertos": [{"puerto": p.device, "descripcion": p.description,
                         "usb": f"{p.vid:04X}:{p.pid:04X}" if p.vid else ""} for p in list_ports.comports()]}


@control("microfono", "Abre o cierra el micrófono de una banca (o unidad). El del presidente pide confirmar=true.",
         {"banca": {"type": "string"}, "unidad": {"type": "integer"}, "abierto": {"type": "boolean"},
          "confirmar": {"type": "boolean"}, "quitar": {"type": "boolean"}})
def _microfono(a):
    k = requiere_ccu()
    u = unidad_de(a)
    abierto = bool(a.get("abierto", True))
    if not abierto and k.estado.es_presidente(u) and not a.get("confirmar"):
        raise RuntimeError("es el micrófono del presidente: confirmá (confirmar=true) para cerrarlo")
    registrar(f"microfono {'abre' if abierto else 'cierra'} banca {banca_de(u)} (unidad {u}) por {a.get('quien', 'app')}")
    esperar(k.microfono(u, abierto))
    if not abierto and a.get("quitar"):
        # «Terminar»: además sale de la lista de oradores (si ya no estaba, no importa).
        try:
            esperar(k.orador_quitar(u))
        except RuntimeError:
            pass
    return con_banca(u, {"abierto": abierto})


@control("siguiente", "Le da la palabra al primero de la lista de pedidos (MM_C_SHIFT).")
def _siguiente(_a):
    k = requiere_ccu()
    esperar(k.siguiente())
    f = foto()
    return {"hablando": f.get("hablando", []), "pedidos": f.get("pedidos", [])}


@control("cerrar_todos", "Cierra todos los micrófonos abiertos (no el del presidente salvo incluir_presidente).",
         {"incluir_presidente": {"type": "boolean"}})
def _cerrar_todos(a):
    k = requiere_ccu()
    us = esperar(k.cerrar_todos(bool(a.get("incluir_presidente"))), 10)
    return {"cerrados": [banca_de(u) or f"u{u}" for u in us]}


@control("pedidos", "La lista de pedidos de palabra, en orden.")
def _pedidos(_a):
    return {"pedidos": foto().get("pedidos", [])}


@control("pedido", "Edita la lista de pedidos: accion agregar | quitar | primero | antes_de (con referencia).",
         {"banca": {"type": "string"}, "unidad": {"type": "integer"},
          "accion": {"type": "string", "enum": ["agregar", "quitar", "primero", "antes_de"]},
          "referencia": {"type": "string"}})
def _pedido(a):
    k = requiere_ccu()
    u = unidad_de(a)
    acc = a.get("accion", "agregar")
    if acc == "agregar":
        esperar(k.pedido_agregar(u))
    elif acc == "quitar":
        esperar(k.pedido_quitar(u))
    elif acc == "primero":
        esperar(k.pedido_antes_de(u, None))
    elif acc == "antes_de":
        esperar(k.pedido_antes_de(u, unidad_de({"banca": a.get("referencia", "")})))
    else:
        raise ValueError("accion: agregar, quitar, primero o antes_de")
    return {"pedidos": foto().get("pedidos", [])}


@control("pedidos_limpiar", "Vacía la lista de pedidos de palabra.")
def _pedidos_limpiar(_a):
    esperar(requiere_ccu().pedidos_limpiar())
    return {}


@control("oradores", "La lista de oradores (los que tienen la palabra), con micrófono prendido o no.")
def _oradores(_a):
    return {"hablando": foto().get("hablando", [])}


@control("orador_quitar", "Saca a una banca de la lista de oradores.", {"banca": {"type": "string"}, "unidad": {"type": "integer"}})
def _orador_quitar(a):
    k = requiere_ccu()
    u = unidad_de(a)
    esperar(k.orador_quitar(u))
    return con_banca(u)


@control("modo", "Cambia el modo de operación y los límites.",
         {"modo": {"type": "string", "enum": list(m.MODOS)}, "max_abiertos": {"type": "integer"},
          "max_pedidos": {"type": "integer"}})
def _modo(a):
    k = requiere_ccu()
    esperar(k.modo(a.get("modo"), a.get("max_abiertos"), a.get("max_pedidos")))
    f = foto()
    return {"modo": f.get("modo"), "max_abiertos": f.get("max_abiertos"), "max_pedidos": f.get("max_pedidos")}


@control("tiempo", "Aviso en la unidad de la banca: ultimo_minuto o terminado.",
         {"banca": {"type": "string"}, "aviso": {"type": "string", "enum": ["ultimo_minuto", "terminado"]}})
def _tiempo(a):
    k = requiere_ccu()
    esperar(k.aviso_tiempo(unidad_de(a), a.get("aviso") == "terminado"))
    return {}


@control("unidades", "Unidades de la CCU con su tipo y banca; bancas sin unidad y unidades sin banca.")
def _unidades(_a):
    f = foto()
    return {k: f.get(k, []) for k in ("unidades", "bancas_sin_unidad", "unidades_sin_banca")} | {"aprendiendo": aprender["banca"]}


@control("unidad_asignar", "Asigna una unidad a una banca, o aprende: {aprender: banca} espera el próximo botón "
         "que se apriete en la sala; {aprender_todas: true} recorre las bancas sin unidad; {cancelar: true}.",
         {"banca": {"type": "string"}, "unidad": {"type": "integer"}, "aprender": {"type": "string"},
          "aprender_todas": {"type": "boolean"}, "cancelar": {"type": "boolean"}})
def _unidad_asignar(a):
    if a.get("cancelar"):
        aprender.update(banca=None, siguientes=[])
    elif a.get("aprender_todas"):
        sin = sorted(set(nombres) - set(mapa_u2b.values()), key=lambda b: (len(b), b))
        aprender.update(banca=sin[0] if sin else None, siguientes=sin[1:])
    elif a.get("aprender"):
        aprender.update(banca=str(a["aprender"]), siguientes=[])
    else:
        if not a.get("banca") or a.get("unidad") in (None, ""):
            raise ValueError("falta banca y unidad (o aprender)")
        asignar(str(a["banca"]), int(a["unidad"]))
    publicar_estado()
    return {"aprendiendo": aprender["banca"], "faltan": list(aprender["siguientes"])}


@control("traza", "Tramas crudas para diagnóstico: {activa: bool} la prende o apaga; {ultimas: n} las devuelve.",
         {"activa": {"type": "boolean"}, "ultimas": {"type": "integer"}})
def _traza(a):
    if "activa" in a:
        c["traza"] = bool(a["activa"])
        guardar_config()
    n = int(a.get("ultimas", 50))
    return {"activa": bool(c.get("traza")), "tramas": list(traza)[-n:],
            "contadores": dict(transporte.contadores) if transporte else {}}


@control("simular_boton", "Solo con el simulador: aprieta el botón de micrófono de una unidad (o banca) en la sala.",
         {"banca": {"type": "string"}, "unidad": {"type": "integer"}})
def _simular_boton(a):
    if sim is None:
        raise RuntimeError("no está el simulador (transporte: simulador)")
    try:
        u = unidad_de(a)
    except ValueError:
        u = int(a.get("unidad") or a.get("banca"))
    sim.apretar(u)
    return {"unidad": u}


@plugin.al_cambiar_config
def _config(nueva):
    c.update(nueva)
    conectar()


@plugin.al_iniciar
def arrancar():
    trabajos.submit(leer_mapa)
    conectar()


@plugin.al_terminar
def terminar():
    desconectar("plugin detenido", avisar=False)


plugin.correr()
