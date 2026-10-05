"""OSC como botonera (ver plugin.json). Escrito para que el asistente lo adapte."""
import ba_plugin as ba

plugin = ba.Plugin()
sup = plugin.superficie
SID = "osc"
estado = {"servidor": None, "cliente": None}


def permitida(ip):
    lista = plugin.config.get("permitidas") or []
    return ip in ("127.0.0.1", "::1") or ip in lista


@plugin.al_iniciar
def arrancar():
    from pythonosc.dispatcher import Dispatcher
    from pythonosc.osc_server import ThreadingOSCUDPServer
    from pythonosc.udp_client import SimpleUDPClient

    c = plugin.config
    if c.get("cliente"):
        h, p = c["cliente"].rsplit(":", 1)
        estado["cliente"] = SimpleUDPClient(h, int(p))
    d = Dispatcher()

    def con_ip(fn):
        def h(ip_puerto, direccion, *args):
            if not permitida(ip_puerto[0]):
                plugin.log(f"OSC: descarto {direccion} de {ip_puerto[0]} (no está en «permitidas»)")
                return
            try:
                fn(*args)
            except (TypeError, ValueError) as e:
                plugin.log(f"OSC: {direccion} {args}: {e}")
        return h

    d.map("/botonera/presionar", con_ip(lambda x, y: sup.presionar(SID, int(x), int(y))), needs_reply_address=True)
    d.map("/botonera/soltar", con_ip(lambda x, y: sup.soltar(SID, int(x), int(y))), needs_reply_address=True)
    d.map("/botonera/valor", con_ip(lambda x, y, v: sup.valor(SID, int(x), int(y), float(v))), needs_reply_address=True)
    d.map("/botonera/girar", con_ip(lambda x, y, dl: sup.girar(SID, int(x), int(y), float(dl))), needs_reply_address=True)
    d.map("/botonera/pagina", con_ip(lambda pid: sup.pagina(SID, str(pid))), needs_reply_address=True)
    srv = ThreadingOSCUDPServer(("0.0.0.0", int(c.get("puerto", 9000))), d)
    estado["servidor"] = srv
    sup.registrar(SID, "OSC :" + str(c.get("puerto", 9000)), modelo="osc",
                  columnas=int(c.get("columnas", 4)), filas=int(c.get("filas", 4)))
    plugin.estado(f"escuchando OSC en el puerto {c.get('puerto', 9000)}")
    srv.serve_forever()


@sup.al_estado(SID)
def al_estado(sid, e):
    cli = estado["cliente"]
    if cli is None:
        return
    for c in e.get("celdas", []):
        base = f'/botonera/celda/{c["x"]}/{c["y"]}'
        cli.send_message(base + "/texto", c.get("texto", ""))
        cli.send_message(base + "/color", c.get("fondo", ""))
        cli.send_message(base + "/activo", 1 if c.get("activo") else 0)
        if c.get("max", 0) != c.get("min", 0) and "valor" in c:
            cli.send_message(base + "/valor", (c["valor"] - c["min"]) / (c["max"] - c["min"]))


@plugin.al_terminar
def cerrar():
    if estado["servidor"] is not None:
        estado["servidor"].shutdown()


plugin.correr()
