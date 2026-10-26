"""documento-aamva — la licencia de conducir de EE.UU. y Canadá como tipo de documento.

Un TIPO DE DOCUMENTO de identidad (docs/funciones/DOCUMENTOS.md): la app lee el código de barras
de la cámara del tótem (o de `documento_leer`) con zxing y, si no es uno de fábrica (el DNI
argentino), nos pasa {formato, texto, bytes}. Acá se traduce el PDF417 de una licencia AAMVA a los
datos normalizados; si el código no es de este tipo, se devuelve None y la app sigue con otro.

El estándar AAMVA (DL/ID Card Design Standard): el texto empieza con «@», una cabecera
«ANSI <IIN de 6 dígitos><versión 2><versión de la jurisdicción 2><cantidad 2>», el directorio de
subarchivos («DL00410278…») y después los elementos, uno por línea, con un código de tres letras:

    DAQ número        DCS apellido        DAC nombre        DAD segundo nombre
    DCT nombres (v2-3) DAA nombre completo (v1: «APELLIDO,NOMBRE,SEGUNDO»)
    DBB nacimiento     DBD emisión        DBA vencimiento   DBC sexo (1 M, 2 F, 9 X)
    DCG país (USA/CAN) DAJ estado         DAI ciudad        DAK código postal   DCA clase

Las fechas son MMDDAAAA en EE.UU. y AAAAMMDD en Canadá (y en la versión 1).

COMO MOLDE para otro documento: cambiá `leer` (y el decorador: nombre, formatos, país). Los
`ejemplos` son SINTÉTICOS —nunca pongas datos de una persona real— y `plugin_probar` los corre.
Los datos son sensibles: no los mandes a ningún servicio de afuera ni los imprimas enteros.
"""
from __future__ import annotations

import re

import ba_plugin as ba

plugin = ba.Plugin()

# Sintéticos: «Ana María Ejemplo», número inventado (el IIN 636014 es el de la jurisdicción de
# California, que no identifica a nadie).
EJEMPLO_EEUU = ("@\n\x1e\rANSI 636014090102DL00410245ZC02860025DLDAQE1234567\nDCSEJEMPLO\nDACANA\nDADMARIA\n"
                "DBD01152020\nDBB03151990\nDBA03152028\nDBC2\nDAYBRO\nDAG123 CALLE FALSA\nDAISACRAMENTO\nDAJCA\n"
                "DAK958220000\nDCFPRUEBA0000\nDCGUSA\nDDEN\r")
EJEMPLO_CANADA = ("@\n\x1e\rANSI 636012080002DL00410200ZO02410040DLDAQP0000-00000-00000\nDCSPRUEBA\nDACLUIS\n"
                  "DBD20210601\nDBB19850704\nDBA20260704\nDBC1\nDAJON\nDCGCAN\r")
EJEMPLO_DNI = "00500000001@PRUEBA@JUANA ANDREA@F@11222333@A@15/03/1990@20/04/2016@274"

_PARTICULAS = {"de", "del", "la", "las", "los", "van", "von", "da", "di", "y"}


def _capitalizar(s: str) -> str:
    palabras = [p for p in s.replace(",", " ").split(" ") if p]
    out = []
    for i, p in enumerate(palabras):
        p = p.lower()
        out.append(p if (i > 0 and p in _PARTICULAS) else p[:1].upper() + p[1:])
    return " ".join(out)


def _fecha(s: str, canada: bool) -> str:
    """AAMVA -> ISO. «03151990» (EE.UU.) o «19900315» (Canadá, versión 1). "" si no es una fecha."""
    s = (s or "").strip()
    if len(s) != 8 or not s.isdigit():
        return ""
    a, m, d = (s[0:4], s[4:6], s[6:8]) if canada else (s[4:8], s[0:2], s[2:4])
    if not (1 <= int(m) <= 12 and 1 <= int(d) <= 31 and 1900 <= int(a) <= 2200):
        # Por si la jurisdicción usa el otro orden.
        a, m, d = (s[4:8], s[0:2], s[2:4]) if canada else (s[0:4], s[4:6], s[6:8])
        if not (1 <= int(m) <= 12 and 1 <= int(d) <= 31 and 1900 <= int(a) <= 2200):
            return ""
    return f"{a}-{m}-{d}"


def elementos(texto: str) -> tuple[dict, int] | None:
    """Los elementos {código: valor} y la versión del estándar, o None si no es un AAMVA."""
    i = texto.find("ANSI ")
    if i < 0:
        i = texto.find("AAMVA")
    if i < 0:
        return None
    cab = texto[i:]
    version = int(cab[11:13]) if cab[11:13].isdigit() else 0
    lineas = re.split(r"[\n\r\x1e]+", cab)
    campos: dict[str, str] = {}
    # La primera línea trae la cabecera, el directorio y el primer elemento pegado al
    # designador del subarchivo: «…DLDAQE1234567». El directorio («DL0041…») va con números.
    m = re.search(r"(?:DL|ID)(D[A-Z]{2})(.*)$", lineas[0][5:])
    if m:
        campos[m.group(1)] = m.group(2).strip()
    for linea in lineas[1:]:
        linea = linea.strip()
        if re.match(r"^(?:DL|ID)D[A-Z]{2}", linea):  # otro subarchivo (DL e ID juntos)
            linea = linea[2:]
        if re.match(r"^[DZ][A-Z]{2}", linea):
            campos.setdefault(linea[:3], linea[3:].strip())
    return (campos, version) if "DAQ" in campos else None


@plugin.documento("licencia", titulo="Licencia de conducir (EE.UU. y Canadá, AAMVA)", formatos=["PDF417"],
                  pais="US", como_se_dice="tu licencia de conducir",
                  ejemplos=[{"formato": "PDF417", "texto": EJEMPLO_EEUU},
                            {"formato": "PDF417", "texto": EJEMPLO_CANADA},
                            {"formato": "PDF417", "texto": EJEMPLO_DNI, "espera": False},
                            {"formato": "QRCode", "texto": "https://ejemplo.com", "espera": False}])
def leer(codigo: dict) -> dict | None:
    """La licencia de conducir de EE.UU. y Canadá (PDF417, estándar AAMVA)."""
    r = elementos(codigo.get("texto") or "")
    if r is None:
        return None
    c, version = r
    canada = c.get("DCG", "").upper() == "CAN" or version == 1
    apellido, nombres = c.get("DCS", ""), c.get("DAC") or c.get("DCT", "")
    if c.get("DAD") and c.get("DAD").upper() not in ("NONE", "NONE,"):
        nombres = f"{nombres} {c['DAD']}".strip()
    if not apellido and c.get("DAA"):  # versión 1: «APELLIDO,NOMBRE,SEGUNDO»
        partes = [p.strip() for p in c["DAA"].split(",")]
        apellido, nombres = partes[0], " ".join(partes[1:])
    sexo = {"1": "M", "2": "F", "9": "X", "M": "M", "F": "F"}.get(c.get("DBC", "").strip().upper(), "")
    return {
        "numero": c["DAQ"],
        "apellido": _capitalizar(apellido),
        "nombres": _capitalizar(nombres),
        "sexo": sexo,
        "nacimiento": _fecha(c.get("DBB", ""), canada),
        "emision": _fecha(c.get("DBD", ""), canada),
        "vencimiento": _fecha(c.get("DBA", ""), canada),
        "pais": "CA" if canada and c.get("DCG", "").upper() == "CAN" else "US",
        "estado": c.get("DAJ", ""),
        "clase": c.get("DCA", ""),
    }


@plugin.al_iniciar
def al_iniciar():
    plugin.estado("listo: licencias AAMVA (PDF417)")


plugin.correr()
