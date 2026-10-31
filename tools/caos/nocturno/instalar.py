#!/usr/bin/env python3
"""Arma la tarea que corre el banco de caos de noche (F4-03) en una MÁQUINA DE PRUEBAS.

Opcional y APAGADA de fábrica: escribe la tarea del sistema (launchd en la Mac, un timer de
systemd del usuario en Linux, el Programador de tareas en Windows) sin prenderla, y dice con
qué comando se prende y se apaga. Nunca en la máquina de un evento: el banco abre la app,
la rompe a propósito (kill -9, SIGSTOP, la GPU, el disco) y la cierra.

Cada noche corre `banco.py --lanzar <app> --nocturno`: todos los escenarios, 5 veces cada
uno, el informe en <datos>/Logs/caos/<fecha>/ (caos.json + caos.md, con la versión de la
app) comparado con la noche anterior; sale distinto de 0 si algo no volvió o empeoró (lo
que el sistema anota como fallida). Necesita la sesión del usuario abierta (la app tiene
ventana) y la máquina sin dormir a esa hora.

    python3 -I tools/caos/nocturno/instalar.py --app dist/BroadcastAppliance.app            # la Mac
    python3 -I tools/caos/nocturno/instalar.py --app dist/BroadcastAppliance --hora 02:30   # Linux
    py -3 -I tools\\caos\\nocturno\\instalar.py --app C:\\ba-pruebas\\BroadcastAppliance      # Windows
    ... --quitar      # borra la tarea (y la apaga)
    ... --mostrar     # solo muestra lo que escribiría

Mac: ~/Library/LaunchAgents/ar.broadcastappliance.caos-nocturno.plist (con Disabled: launchd
no la corre hasta `launchctl enable`); registro en ~/Library/Logs/ba-caos-nocturno.log.
Linux: ~/.config/systemd/user/ba-caos-nocturno.{service,timer} (sin enable); registro en
el journal (`journalctl --user -u ba-caos-nocturno`).
Windows: «BroadcastAppliance caos nocturno», creada deshabilitada (solo con la sesión abierta);
registro en %LOCALAPPDATA%\\ba-caos-nocturno.log.
"""
from __future__ import annotations

import argparse
import os
import platform
import subprocess
import sys
from xml.sax.saxutils import escape

AQUI = os.path.dirname(os.path.abspath(__file__))
BANCO = os.path.join(os.path.dirname(AQUI), "banco.py")
ETIQUETA = "ar.broadcastappliance.caos-nocturno"
UNIDAD = "ba-caos-nocturno"
TAREA = "BroadcastAppliance caos nocturno"


def comando(app: str, extra: list[str]) -> list[str]:
    return [sys.executable, "-I", "-B", BANCO, "--lanzar", os.path.abspath(app), "--nocturno"] + extra


def mac(cmd: list[str], h: int, m: int) -> tuple[str, str, list[str], list[str]]:
    ruta = os.path.expanduser(f"~/Library/LaunchAgents/{ETIQUETA}.plist")
    registro = os.path.expanduser("~/Library/Logs/ba-caos-nocturno.log")
    args = "\n".join(f"        <string>{escape(c)}</string>" for c in cmd)
    texto = f"""<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<!-- El banco de caos de noche (tools/caos/nocturno/instalar.py). Apagada: Disabled. -->
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>{ETIQUETA}</string>
    <key>Disabled</key>
    <true/>
    <key>ProgramArguments</key>
    <array>
{args}
    </array>
    <key>StartCalendarInterval</key>
    <dict>
        <key>Hour</key>
        <integer>{h}</integer>
        <key>Minute</key>
        <integer>{m}</integer>
    </dict>
    <key>StandardOutPath</key>
    <string>{escape(registro)}</string>
    <key>StandardErrorPath</key>
    <string>{escape(registro)}</string>
</dict>
</plist>
"""
    uid = os.getuid() if hasattr(os, "getuid") else 0
    prender = [f"launchctl enable gui/{uid}/{ETIQUETA}", f"launchctl bootstrap gui/{uid} '{ruta}'"]
    apagar = [f"launchctl bootout gui/{uid}/{ETIQUETA}", f"launchctl disable gui/{uid}/{ETIQUETA}"]
    return ruta, texto, prender, apagar


def linux(cmd: list[str], h: int, m: int) -> tuple[str, str, list[str], list[str]]:
    carpeta = os.path.expanduser("~/.config/systemd/user")
    # systemd entiende las comillas dobles (sin shell en el medio).
    linea = " ".join('"' + c.replace("\\", "\\\\").replace('"', '\\"') + '"' for c in cmd)
    servicio = f"""# El banco de caos de noche (tools/caos/nocturno/instalar.py): lo dispara {UNIDAD}.timer.
[Unit]
Description=Banco de caos nocturno de BroadcastAppliance (máquina de pruebas)

[Service]
Type=oneshot
ExecStart={linea}
"""
    timer = f"""# Apagado de fábrica: systemctl --user enable --now {UNIDAD}.timer
[Unit]
Description=Banco de caos nocturno de BroadcastAppliance a las {h:02d}:{m:02d}

[Timer]
OnCalendar=*-*-* {h:02d}:{m:02d}:00
Persistent=false

[Install]
WantedBy=timers.target
"""
    # Los dos archivos en uno para mostrarlos; escribir() los separa por el marcador.
    texto = servicio + f"\n### {UNIDAD}.timer\n" + timer
    prender = ["systemctl --user daemon-reload", f"systemctl --user enable --now {UNIDAD}.timer"]
    apagar = [f"systemctl --user disable --now {UNIDAD}.timer"]
    return os.path.join(carpeta, f"{UNIDAD}.service"), texto, prender, apagar


def windows(cmd: list[str], h: int, m: int) -> tuple[str, str, list[str], list[str]]:
    registro = os.path.join(os.environ.get("LOCALAPPDATA", os.path.expanduser("~")), "ba-caos-nocturno.log")
    linea = subprocess.list2cmdline(cmd) + f' > "{registro}" 2>&1'
    texto = f"""<?xml version="1.0" encoding="UTF-16"?>
<!-- El banco de caos de noche (tools/caos/nocturno/instalar.py). Apagada: Enabled=false. -->
<Task version="1.4" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task">
  <Triggers>
    <CalendarTrigger>
      <StartBoundary>2026-01-01T{h:02d}:{m:02d}:00</StartBoundary>
      <ScheduleByDay><DaysInterval>1</DaysInterval></ScheduleByDay>
    </CalendarTrigger>
  </Triggers>
  <Principals>
    <Principal id="Usuario"><LogonType>InteractiveToken</LogonType><RunLevel>LeastPrivilege</RunLevel></Principal>
  </Principals>
  <Settings>
    <Enabled>false</Enabled>
    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>
    <ExecutionTimeLimit>PT6H</ExecutionTimeLimit>
    <StartWhenAvailable>false</StartWhenAvailable>
  </Settings>
  <Actions Context="Usuario">
    <Exec>
      <Command>cmd.exe</Command>
      <Arguments>/c {escape(linea)}</Arguments>
    </Exec>
  </Actions>
</Task>
"""
    ruta = os.path.join(os.environ.get("LOCALAPPDATA", os.path.expanduser("~")), "ba-caos-nocturno.xml")
    prender = [f'schtasks /Create /TN "{TAREA}" /XML "{ruta}" /F', f'schtasks /Change /TN "{TAREA}" /ENABLE']
    apagar = [f'schtasks /Change /TN "{TAREA}" /DISABLE']
    return ruta, texto, prender, apagar


def escribir(sistema: str, ruta: str, texto: str) -> list[str]:
    """Escribe la tarea (los archivos que sean); devuelve las rutas."""
    os.makedirs(os.path.dirname(ruta), exist_ok=True)
    if sistema == "Linux":
        servicio, timer = texto.split(f"\n### {UNIDAD}.timer\n")
        otra = os.path.join(os.path.dirname(ruta), f"{UNIDAD}.timer")
        for r, t in ((ruta, servicio), (otra, timer)):
            with open(r, "w", encoding="utf-8") as f:
                f.write(t)
        return [ruta, otra]
    with open(ruta, "w", encoding="utf-16" if sistema == "Windows" else "utf-8") as f:
        f.write(texto)
    return [ruta]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--app", help="la app a probar (la .app, la carpeta del bundle o el launcher)")
    ap.add_argument("--hora", default="03:00", help="HH:MM (de fábrica 03:00)")
    ap.add_argument("--quitar", action="store_true", help="apagar y borrar la tarea")
    ap.add_argument("--mostrar", action="store_true", help="solo mostrar lo que escribiría")
    ap.add_argument("extra", nargs="*", help="más argumentos para banco.py (después de --)")
    a = ap.parse_args()
    sistema = platform.system()
    armar = {"Darwin": mac, "Linux": linux, "Windows": windows}.get(sistema)
    if armar is None:
        print("sistema sin tarea:", sistema)
        return 2
    h, m = (int(x) for x in a.hora.split(":"))
    ruta, texto, prender, apagar = armar(comando(a.app or "<app>", a.extra), h, m)
    if a.quitar:
        print("Para apagarla (si estaba prendida):")
        print("\n".join("  " + c for c in apagar))
        for r in (ruta, os.path.join(os.path.dirname(ruta), f"{UNIDAD}.timer")):
            if os.path.exists(r):
                os.remove(r)
                print("borrada:", r)
        return 0
    if a.mostrar or not a.app:
        print(texto)
        if not a.app:
            print("(falta --app: no se escribió nada)")
        return 0 if a.app else 2
    for r in escribir(sistema, ruta, texto):
        print("escrita (APAGADA):", r)
    print("Para prenderla:")
    print("\n".join("  " + c for c in prender))
    print("Para apagarla:")
    print("\n".join("  " + c for c in apagar))
    return 0


if __name__ == "__main__":
    sys.exit(main())
