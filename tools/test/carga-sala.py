#!/usr/bin/env python3
"""Prueba de carga de la sala: invitados emulados con Chrome headless.

Levanta N navegadores contra la app web del invitado (cada uno con cámara y
micrófono falsos), los hace entrar a la sala y va midiendo cuánto le cuesta al
gateway: gente conectada, con medios arriba, CPU del proceso y —lo que más
importa— el hilo más cargado, porque libwebrtc reparte la decodificación de
cada invitado en su propio hilo pero la red y el worker son UNO para todas las
conexiones.

Se apunta a http://127.0.0.1:<puerto>/ a propósito: localhost es contexto
seguro, así que `getUserMedia` anda sin certificados.

Uso (con el sistema corriendo):

    tools/test/carga-sala.py --invitados 8
    tools/test/carga-sala.py --invitados 16 --segundos 120

Desde otro equipo, para que los navegadores no le compitan la CPU al gateway,
se parte en dos: la API de control solo escucha en loopback, así que lo que
mide corre acá y lo que levanta clientes corre allá.

    # en esta máquina (mide y admite)
    tools/test/carga-sala.py --solo-medir --segundos 180
    # en el servidor (solo clientes; https por el certificado propio)
    tools/test/carga-sala.py --sin-admin --invitados 16 \
        --url https://192.168.1.119:8789/ --sala sala1 --pin 422043

Dos cosas que confunden si no se saben:

  * La sala solo admite hasta "Cantidad de invitados" (Configuración →
    Invitados; hoy el tope es 16, y cambiarlo reinicia el gateway). Si pedís
    más, el resto queda esperando en el lobby — que igual conecta y manda
    video, o sea que también es carga real.
  * Sin `--video`, la cámara falsa de Chrome manda un patrón sintético que se
    comprime a casi nada: el ancho de banda medido no se parece al de un
    evento. `tools/test/video-de-prueba.sh` prepara un video real (una persona
    frente a la cámara) que Chrome reproduce en bucle.
  * Los números de CPU solo valen con el sistema ENTERO corriendo. Contra un
    gateway suelto (sin la app A del otro lado) nadie consume el video que
    llega, asi que no se decodifica y la CPU da casi cero: se mide la
    señalizacion, no el trabajo real.
  * Al cortar, los invitados siguen figurando ~30 s: la sala le guarda el lugar
    al que se cae por si vuelve. Entre corrida y corrida, esperar ese rato o
    los lugares van a estar ocupados por fantasmas.
"""

import argparse
import json
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

CHROMES = [
    "google-chrome",
    "google-chrome-stable",
    "chromium",
    "chromium-browser",
    "/opt/google/chrome/chrome",
]


def buscar_chrome() -> str:
    for c in CHROMES:
        ruta = shutil.which(c) or (c if os.path.exists(c) else None)
        if ruta:
            return ruta
    sys.exit("No encontré Chrome ni Chromium. Instalá uno o pasá --chrome <ruta>.")


class Sala:
    """Cliente de la API de control del gateway (la misma que usa la consola)."""

    def __init__(self, bundle: str):
        runtime = os.path.join(bundle, "Runtime", "gateway-admin.json")
        try:
            with open(runtime, encoding="utf-8") as fh:
                d = json.load(fh)
        except OSError:
            sys.exit(f"No pude leer {runtime}: ¿está corriendo el sistema?")
        self.token = d["token"]
        self.puerto = d["port"]
        self.puerto_invitado = d["guestPort"]

    def _pedir(self, ruta: str, cuerpo=None):
        req = urllib.request.Request(
            f"http://127.0.0.1:{self.puerto}{ruta}",
            data=json.dumps(cuerpo).encode() if cuerpo is not None else None,
            headers={"Authorization": f"Bearer {self.token}", "Content-Type": "application/json"},
            method="POST" if cuerpo is not None else "GET",
        )
        with urllib.request.urlopen(req, timeout=5) as r:
            cuerpo = r.read()
            return json.loads(cuerpo) if cuerpo else {}

    def estado(self) -> dict:
        return self._pedir("/api/v1/room")

    def admitir(self, pid: str):
        try:
            self._pedir(f"/api/v1/participants/{pid}/admit", {})
        except urllib.error.HTTPError:
            pass  # sala llena: se queda esperando, que también es carga


def lanzar_invitado(chrome: str, url: str, perfil: str, inseguro: bool, video: str = "",
                    audio: str = "") -> subprocess.Popen:
    args = [
        chrome,
        "--headless=new",
        "--no-sandbox",
        "--disable-gpu",
        "--mute-audio",
        # Cámara y micrófono falsos: sin hardware.
        "--use-fake-device-for-media-stream",
        "--use-fake-ui-for-media-stream",
        "--autoplay-policy=no-user-gesture-required",
        f"--user-data-dir={perfil}",
    ]
    # Con un archivo, la cámara falsa reproduce ESE video en bucle en vez del
    # patrón sintético. Importa: el patrón es casi todo plano y el codificador
    # lo resuelve con cuatro bits, así que sin esto la prueba miente justo en
    # lo que se quiere medir (ancho de banda y trabajo del codificador).
    if video:
        args.append(f"--use-file-for-fake-video-capture={video}")
    if audio:
        args.append(f"--use-file-for-fake-audio-capture={audio}")
    if inseguro:
        # El gateway sirve HTTPS con certificado propio: sin esto el navegador
        # ni carga la página (y sin HTTPS no hay cámara fuera de localhost).
        args += ["--ignore-certificate-errors", "--allow-insecure-localhost"]
    args.append(url)
    return subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                            start_new_session=True)


def main() -> int:
    p = argparse.ArgumentParser(description="Invitados emulados contra la sala.")
    p.add_argument("--invitados", type=int, default=4, help="cuántos levantar")
    p.add_argument("--segundos", type=int, default=60, help="cuánto sostener la prueba")
    p.add_argument("--bundle", default="release", help="carpeta del sistema corriendo")
    p.add_argument("--chrome", default="", help="ruta a Chrome/Chromium")
    p.add_argument("--escalonado", type=float, default=1.5,
                   help="segundos entre un invitado y el siguiente")
    p.add_argument("--sin-admitir", action="store_true",
                   help="dejarlos en el lobby (mide solo la señalización)")
    p.add_argument("--sin-admin", action="store_true",
                   help="solo levantar clientes, sin tocar la API de control "
                        "(para correr desde otro equipo; requiere --url, --sala y --pin)")
    p.add_argument("--solo-medir", action="store_true",
                   help="no levantar clientes: solo medir y admitir (corre en esta máquina "
                        "mientras otro equipo manda los clientes)")
    p.add_argument("--url", default="", help="base de la app del invitado, p. ej. https://192.168.1.119:8789/")
    p.add_argument("--sala", default="", help="nombre de la sala (con --sin-admin)")
    p.add_argument("--pin", default="", help="PIN de la sala (con --sin-admin)")
    p.add_argument("--etiqueta", default="",
                   help="prefijo en el nombre de los invitados, para distinguir equipos")
    p.add_argument("--video", default="",
                   help="archivo .y4m que la cámara falsa reproduce en bucle "
                        "(lo prepara tools/test/video-de-prueba.sh). Sin esto, "
                        "Chrome manda su patrón sintético, que no pesa nada")
    p.add_argument("--audio", default="",
                   help="archivo .wav para el micrófono falso")
    a = p.parse_args()

    if a.sin_admin and a.solo_medir:
        sys.exit("--sin-admin y --solo-medir se excluyen: uno manda clientes, el otro mide.")

    chrome = ""
    sala = None
    if a.sin_admin:
        if not (a.url and a.pin):
            sys.exit("Con --sin-admin hace falta al menos --url y --pin.")
        base, cuarto, pin = a.url, a.sala or "sala1", a.pin
        lugares = "?"
    else:
        sala = Sala(a.bundle)
        estado = sala.estado()
        pin = a.pin or estado.get("pin", "")
        cuarto = a.sala or estado.get("room", "sala1")
        lugares = estado.get("maxGuests")
        if not pin:
            sys.exit("La sala no informó PIN: fijate que el gateway esté arriba.")
        base = a.url or f"http://127.0.0.1:{sala.puerto_invitado}/"

    if a.solo_medir:
        a.invitados = 0
    else:
        chrome = a.chrome or buscar_chrome()

    for ruta, que in ((a.video, "video"), (a.audio, "audio")):
        if ruta and not os.path.isfile(ruta):
            sys.exit(f"No encuentro el archivo de {que}: {ruta}")
    if a.video and not a.video.endswith(".y4m"):
        sys.exit("La cámara falsa de Chrome solo lee .y4m (video crudo). "
                 "Usá tools/test/video-de-prueba.sh para prepararlo.")

    inseguro = base.startswith("https://")
    print(f"Sala «{cuarto}» en {base} · lugares: {lugares} · "
          + ("midiendo, los clientes vienen de afuera" if a.solo_medir
             else f"invitados a levantar: {a.invitados}"))
    if a.invitados:
        print("Cámara: " + (os.path.basename(a.video) if a.video
                            else "patrón sintético de Chrome (no pesa nada: los números de red no valen)"))
    print("Ctrl+C corta y limpia.\n")

    tmp = tempfile.mkdtemp(prefix="ba-carga-")
    procesos: list[subprocess.Popen] = []
    admitidos: set[str] = set()
    picos = {"cpu": 0.0, "hilo": 0.0, "hiloNombre": "", "conectados": 0, "medios": 0}

    def limpiar(*_):
        print("\nCerrando invitados…")
        for pr in procesos:
            try:
                os.killpg(os.getpgid(pr.pid), signal.SIGTERM)
            except (ProcessLookupError, PermissionError):
                pass
        shutil.rmtree(tmp, ignore_errors=True)

    signal.signal(signal.SIGINT, lambda *_: (limpiar(), sys.exit(130)))

    if sala is not None:
        print(f"{'t':>4}  {'conect':>6} {'medios':>6} {'sala':>5}  {'CPU':>6}  "
              f"{'hilo más cargado':<22} {'mem':>6}")
    t0 = time.time()
    proximo = 0.0
    try:
        while True:
            t = time.time() - t0
            if len(procesos) < a.invitados and t >= proximo:
                n = len(procesos) + 1
                sufijo = f"&name=Carga+{a.etiqueta}{n:02d}&go=1"
                url = f"{base}#room={cuarto}&pin={pin}{sufijo}"
                procesos.append(
                    lanzar_invitado(chrome, url, os.path.join(tmp, f"p{n}"), inseguro, a.video, a.audio))
                proximo = t + a.escalonado

            if sala is None:
                # Sin API de control no hay nada que mirar desde acá: se sostiene
                # la carga y se mide del otro lado.
                if t >= a.segundos and len(procesos) >= a.invitados:
                    break
                time.sleep(2)
                continue

            e = sala.estado()
            gente = e.get("participants", [])
            conectados = [x for x in gente if x.get("connected")]
            medios = [x for x in conectados if x.get("mediaUp")]
            en_sala = [x for x in conectados if x.get("state") == "admitted"]
            if not a.sin_admitir:
                for x in conectados:
                    if x.get("state") == "waiting" and x["id"] not in admitidos:
                        admitidos.add(x["id"])
                        sala.admitir(x["id"])

            u = e.get("uso", {})
            cpu = float(u.get("cpuPct", 0) or 0)
            hilo = float(u.get("hiloPct", 0) or 0)
            picos["cpu"] = max(picos["cpu"], cpu)
            picos["conectados"] = max(picos["conectados"], len(conectados))
            picos["medios"] = max(picos["medios"], len(medios))
            if hilo > picos["hilo"]:
                picos["hilo"] = hilo
                picos["hiloNombre"] = u.get("hiloTop", "")
            print(f"{t:4.0f}  {len(conectados):6d} {len(medios):6d} {len(en_sala):5d}  "
                  f"{cpu:5.1f}%  {(u.get('hiloTop','') + ' ' + format(hilo, '.0f') + '%'):<22} "
                  f"{u.get('memMb', 0):5.0f}M")

            if t >= a.segundos and len(procesos) >= a.invitados:
                break
            time.sleep(2)
    finally:
        limpiar()

    print("\n--- Resumen ---")
    print(f"Invitados levantados:      {len(procesos)}")
    if sala is None:
        print("(Sin API de control: los números salen del equipo que mide.)")
        return 0
    print(f"Máximo conectados:         {picos['conectados']}")
    print(f"Máximo con medios arriba:  {picos['medios']}")
    print(f"Pico de CPU del gateway:   {picos['cpu']:.1f}%")
    print(f"Pico del hilo más cargado: {picos['hilo']:.0f}%  ({picos['hiloNombre']})")
    if picos["hilo"] >= 85:
        print("\nOjo: un solo hilo casi al tope. El techo no es la máquina sino ese núcleo:\n"
              "     ahí tiene sentido repartir las conexiones en varias fábricas de libwebrtc\n"
              "     (ver docs/planes/IDEAS.md).")
    elif picos["cpu"] >= 70:
        print("\nOjo: el proceso está apretado en CPU total; la máquina es el límite.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
