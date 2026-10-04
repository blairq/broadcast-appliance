"""Hablar con equipos: USB (y los controles UVC de cualquier cámara USB), HID y puerto serie.

Lo que una marca hace "solo con su aplicación" casi siempre pasa por uno de
estos caminos:

- **UVC estándar**: pan/tilt/zoom/foco de la Terminal de Cámara (las cámaras
  USB PTZ "de verdad" lo implementan). ``UVC.abrir(vid, pid).mover(...)``.
- **Unidades de extensión UVC (XU)**: los controles propios de la marca
  (Logitech, Kandao, Jabra… exponen sus modos y su PTZ ahí). ``uvc.unidades()``
  lista sus GUID; ``uvc.xu_leer`` / ``uvc.xu_escribir`` hablan con cada una.
- **HID**: muchas cámaras de sala y botoneras tienen una interfaz HID aparte
  (``hid_listar()``, ``hid_abrir()``).
- **Serie** (VISCA por RS-232/USB, equipos viejos): ``serie_listar()``.
- **Red**: HTTP/JSON, WebSocket o UDP de la marca: con la biblioteca estándar
  (urllib, socket) o requests/websockets como dependencias.

Bibliotecas (van en ``"dependencias"`` de plugin.json): ``pyusb`` +
``libusb-package`` (trae libusb, portable) para USB/UVC, ``hidapi`` para HID,
``pyserial`` para serie.

Para averiguar el protocolo de un equipo raro: ``uvc.unidades()`` y
``uvc.xu_explorar(unidad)`` (qué selectores contestan y con cuántos bytes); en
HID, leer los reportes que manda mientras se usa la app de la marca.
"""

from __future__ import annotations

import struct
from typing import Any

# --- USB -------------------------------------------------------------------------------


def _usb():
    try:
        import usb.core
        import usb.util  # noqa: F401
    except ImportError as e:
        raise RuntimeError('falta pyusb: "dependencias": ["pyusb", "libusb-package"] en plugin.json') from e
    backend = None
    try:  # libusb portable (la que trae libusb-package), no la del sistema
        import libusb_package
        import usb.backend.libusb1
        backend = usb.backend.libusb1.get_backend(find_library=libusb_package.find_library)
    except ImportError:
        pass
    return usb.core, backend


def usb_listar() -> list[dict]:
    """Todos los equipos USB: vid, pid, fabricante, producto, serie y si son de video (UVC)."""
    core, backend = _usb()
    out = []
    for d in core.find(find_all=True, backend=backend):
        info = {"vid": f"{d.idVendor:04x}", "pid": f"{d.idProduct:04x}", "bus": d.bus, "direccion": d.address}
        for k, idx in (("fabricante", d.iManufacturer), ("producto", d.iProduct), ("serie", d.iSerialNumber)):
            try:
                info[k] = _texto(d, idx) if idx else ""
            except Exception:  # noqa: BLE001 — sin permiso para leer textos: van vacíos
                info[k] = ""
        try:
            info["uvc"] = any(i.bInterfaceClass == 0x0E for c in d for i in c)
        except Exception:  # noqa: BLE001
            info["uvc"] = False
        out.append(info)
    return out


def _texto(d, idx: int) -> str:
    import usb.util
    return usb.util.get_string(d, idx) or ""


# --- UVC -------------------------------------------------------------------------------

SET_CUR, GET_CUR, GET_MIN, GET_MAX, GET_RES, GET_LEN, GET_INFO, GET_DEF = 0x01, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87
# Selectores de la Terminal de Cámara (UVC 1.5, tabla A-12)
CT_FOCUS_ABSOLUTE, CT_FOCUS_RELATIVE, CT_FOCUS_AUTO = 0x06, 0x07, 0x08
CT_ZOOM_ABSOLUTE, CT_ZOOM_RELATIVE = 0x0B, 0x0C
CT_PANTILT_ABSOLUTE, CT_PANTILT_RELATIVE = 0x0D, 0x0E


class UVC:
    """Los controles UVC de una cámara USB (estándar y de extensión), por control transfers.

    No reclama la interfaz de video: la cámara sigue mandando imagen por el sistema
    (avfvideosrc / v4l2src / mfvideosrc) mientras se la mueve por acá."""

    def __init__(self, dispositivo, backend=None):
        self.d = dispositivo
        self.backend = backend
        self.interfaz = 0
        self.camara = None  # id de la Terminal de Cámara
        self.procesador = None
        self.extensiones: list[dict] = []
        self._leer_descriptores()

    @classmethod
    def abrir(cls, vid: int | str, pid: int | str | None = None, serie: str | None = None) -> "UVC":
        core, backend = _usb()
        vid = int(vid, 16) if isinstance(vid, str) else vid
        pid = int(pid, 16) if isinstance(pid, str) else pid
        for d in core.find(find_all=True, idVendor=vid, backend=backend):
            if pid is not None and d.idProduct != pid:
                continue
            if serie and (_texto(d, d.iSerialNumber) if d.iSerialNumber else "") != serie:
                continue
            return cls(d, backend)
        raise LookupError(f"no hay una cámara USB {vid:04x}:{pid or 0:04x} conectada")

    def _leer_descriptores(self) -> None:
        cfg = self.d.get_active_configuration() if _activa(self.d) else self.d[0]
        for i in cfg:
            if i.bInterfaceClass == 0x0E and i.bInterfaceSubClass == 0x01:  # Video Control
                self.interfaz = i.bInterfaceNumber
                extra = bytes(i.extra_descriptors or b"")
                k = 0
                while k + 3 <= len(extra):
                    largo, tipo, sub = extra[k], extra[k + 1], extra[k + 2]
                    if largo == 0:
                        break
                    if tipo == 0x24:  # CS_INTERFACE
                        if sub == 0x02 and k + 6 <= len(extra):  # INPUT_TERMINAL
                            terminal = struct.unpack_from("<H", extra, k + 4)[0]
                            if terminal == 0x0201:  # ITT_CAMERA
                                self.camara = extra[k + 3]
                        elif sub == 0x05:  # PROCESSING_UNIT
                            self.procesador = extra[k + 3]
                        elif sub == 0x06 and k + 22 <= len(extra):  # EXTENSION_UNIT
                            guid = extra[k + 4:k + 20]
                            nctl = extra[k + 20]
                            self.extensiones.append({"unidad": extra[k + 3], "guid": _guid(guid), "controles": nctl})
                    k += largo
                break

    def _pedido(self, req: int, selector: int, unidad: int, largo_o_datos) -> bytes:
        windex = (unidad << 8) | self.interfaz
        if req == SET_CUR:
            self.d.ctrl_transfer(0x21, req, selector << 8, windex, largo_o_datos, timeout=500)
            return b""
        return bytes(self.d.ctrl_transfer(0xA1, req, selector << 8, windex, largo_o_datos, timeout=500))

    # Estándar -------------------------------------------------------------------------

    def rangos(self) -> dict:
        """Mín/máx de pan, tilt (arcosegundos) y zoom (unidades de la cámara)."""
        if self.camara is None:
            return {}
        out = {}
        try:
            mn = struct.unpack("<ii", self._pedido(GET_MIN, CT_PANTILT_ABSOLUTE, self.camara, 8))
            mx = struct.unpack("<ii", self._pedido(GET_MAX, CT_PANTILT_ABSOLUTE, self.camara, 8))
            out["pan"], out["tilt"] = (mn[0], mx[0]), (mn[1], mx[1])
        except Exception:  # noqa: BLE001
            pass
        try:
            out["zoom"] = (struct.unpack("<H", self._pedido(GET_MIN, CT_ZOOM_ABSOLUTE, self.camara, 2))[0],
                           struct.unpack("<H", self._pedido(GET_MAX, CT_ZOOM_ABSOLUTE, self.camara, 2))[0])
        except Exception:  # noqa: BLE001
            pass
        return out

    def posicion(self) -> dict:
        pan, tilt = struct.unpack("<ii", self._pedido(GET_CUR, CT_PANTILT_ABSOLUTE, self.camara, 8))
        zoom = struct.unpack("<H", self._pedido(GET_CUR, CT_ZOOM_ABSOLUTE, self.camara, 2))[0]
        return {"pan": pan, "tilt": tilt, "zoom": zoom}

    def ir_a(self, pan: int | None = None, tilt: int | None = None, zoom: int | None = None) -> None:
        """Posición absoluta (pan/tilt en arcosegundos: 3600 = 1 grado)."""
        if pan is not None or tilt is not None:
            p = self.posicion()
            self._pedido(SET_CUR, CT_PANTILT_ABSOLUTE, self.camara,
                         struct.pack("<ii", p["pan"] if pan is None else pan, p["tilt"] if tilt is None else tilt))
        if zoom is not None:
            self._pedido(SET_CUR, CT_ZOOM_ABSOLUTE, self.camara, struct.pack("<H", zoom))

    def mover(self, pan: float, tilt: float, zoom: float = 0.0) -> None:
        """Velocidades -1..1 (relativo; 0 para). Si la cámara no tiene relativo, se acerca con pasos absolutos."""
        def sentido(v):
            return 0 if abs(v) < 0.02 else (1 if v > 0 else 0xFF)
        try:
            vel = lambda v: max(1, min(255, int(abs(v) * 255)))  # noqa: E731
            self._pedido(SET_CUR, CT_PANTILT_RELATIVE, self.camara,
                         struct.pack("<BBBB", sentido(pan), vel(pan), sentido(tilt), vel(tilt)))
        except Exception:  # noqa: BLE001 — sin relativo: un paso absoluto
            p, r = self.posicion(), self.rangos()
            paso = lambda v, rango: int(v * (rango[1] - rango[0]) / 40) if rango else 0  # noqa: E731
            self.ir_a(p["pan"] + paso(pan, r.get("pan")), p["tilt"] + paso(tilt, r.get("tilt")))
        if abs(zoom) >= 0.02:
            try:
                self._pedido(SET_CUR, CT_ZOOM_RELATIVE, self.camara,
                             struct.pack("<bBB", 1 if zoom > 0 else -1, 0, max(1, int(abs(zoom) * 255))))
            except Exception:  # noqa: BLE001
                p, r = self.posicion(), self.rangos().get("zoom", (0, 0))
                self.ir_a(zoom=max(r[0], min(r[1], p["zoom"] + int(zoom * (r[1] - r[0]) / 20))))

    def parar(self) -> None:
        try:
            self._pedido(SET_CUR, CT_PANTILT_RELATIVE, self.camara, struct.pack("<BBBB", 0, 0, 0, 0))
            self._pedido(SET_CUR, CT_ZOOM_RELATIVE, self.camara, struct.pack("<bBB", 0, 0, 0))
        except Exception:  # noqa: BLE001
            pass

    # Extensiones (lo propio de la marca) --------------------------------------------

    def unidades(self) -> list[dict]:
        """Las unidades de extensión: ``[{"unidad", "guid", "controles"}]``."""
        return list(self.extensiones)

    def xu_largo(self, unidad: int, selector: int) -> int:
        return struct.unpack("<H", self._pedido(GET_LEN, selector, unidad, 2))[0]

    def xu_leer(self, unidad: int, selector: int, largo: int | None = None, que: int = GET_CUR) -> bytes:
        return self._pedido(que, selector, unidad, largo or self.xu_largo(unidad, selector))

    def xu_escribir(self, unidad: int, selector: int, datos: bytes) -> None:
        self._pedido(SET_CUR, selector, unidad, datos)

    def xu_explorar(self, unidad: int, hasta: int = 32) -> list[dict]:
        """Qué selectores contestan en una unidad (largo, valor actual, mín, máx)."""
        out = []
        for sel in range(1, hasta + 1):
            try:
                n = self.xu_largo(unidad, sel)
            except Exception:  # noqa: BLE001
                continue
            fila: dict[str, Any] = {"selector": sel, "largo": n}
            for k, q in (("actual", GET_CUR), ("min", GET_MIN), ("max", GET_MAX), ("def", GET_DEF)):
                try:
                    fila[k] = self.xu_leer(unidad, sel, n, q).hex()
                except Exception:  # noqa: BLE001
                    pass
            out.append(fila)
        return out

    def info(self) -> dict:
        return {"interfaz": self.interfaz, "terminal_camara": self.camara, "procesador": self.procesador,
                "extensiones": self.extensiones, "rangos": self.rangos() if self.camara else {}}


def _activa(d) -> bool:
    try:
        d.get_active_configuration()
        return True
    except Exception:  # noqa: BLE001
        return False


def _guid(b: bytes) -> str:
    a, b2, c = struct.unpack_from("<IHH", b, 0)
    resto = b[8:16].hex()
    return f"{a:08x}-{b2:04x}-{c:04x}-{resto[:4]}-{resto[4:]}"


# --- HID y serie -------------------------------------------------------------------------


def hid_listar() -> list[dict]:
    try:
        import hid
    except ImportError as e:
        raise RuntimeError('falta hidapi: "dependencias": ["hidapi"] en plugin.json') from e
    return [{"vid": f"{d['vendor_id']:04x}", "pid": f"{d['product_id']:04x}", "producto": d.get("product_string", ""),
             "fabricante": d.get("manufacturer_string", ""), "uso": d.get("usage_page"), "ruta": d["path"].decode(errors="replace")}
            for d in hid.enumerate()]


def hid_abrir(vid: int | str, pid: int | str):
    """Un equipo HID abierto (``.write(bytes)``, ``.read(n, timeout_ms)``)."""
    import hid
    h = hid.device()
    h.open(int(vid, 16) if isinstance(vid, str) else vid, int(pid, 16) if isinstance(pid, str) else pid)
    return h


def serie_listar() -> list[dict]:
    try:
        from serial.tools import list_ports
    except ImportError as e:
        raise RuntimeError('falta pyserial: "dependencias": ["pyserial"] en plugin.json') from e
    return [{"puerto": p.device, "descripcion": p.description, "vid": f"{p.vid or 0:04x}", "pid": f"{p.pid or 0:04x}",
             "serie": p.serial_number or ""} for p in list_ports.comports()]
