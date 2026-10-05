# Lo que salió del software de PC de Bosch

Las dudas del Open Interface que los manuales no resolvían se contestaron leyendo el **software de
PC** de Bosch que vino en el DVD: las librerías que hablan con la CCU. Cada dato está anotado en la
sección que corresponde de [open-interface.md](open-interface.md) y en el código
(`assets/plugins/bosch-dcn/dcn/`). Este documento junta qué se miró, cómo, y qué salió.

Alcance: solo **el lado de la PC** (cómo la PC le habla a la CCU por serie o por red). El bus interno
entre la CCU y las unidades y el firmware de los equipos quedan afuera: la app no los necesita.

## 1. Qué se miró

| Archivo | De dónde | Qué es | Qué se sacó |
|---|---|---|---|
| `SCS.DLL` | MSI de DCN-PC 3.1x (`dvd/librerias/dcn-pc-3.1x/`) | Windows de 16 bits (NE), «Serial Communication Services» | El transporte serie Full: checksum, escape, latido |
| `CCU_DDTK.exe` | DDTK (`dvd/librerias/ddtk/`) | Kit de desarrollo del Open Interface (Win32) | Byte de escape 0xC8, encabezado TCP, `byData` con el wFnId |
| `FNID.DEF` | DDTK | Códigos de función con sus formatos | Cruce de todos los códigos del plugin (cero diferencias) |
| `Bosch.Dcn.OpenInterface.Logic` | MSI de DCN-SWSMV 3.1x (`dvd/librerias/sinoptico-smv/bosch.dcn.openinterface.logi`) | .NET, ofuscado (Dotfuscator) | Enums con TODOS los códigos; un `…Marshaler` por estructura |
| `Bosch.Dcn.OpenInterface.Interfaces` | ídem (`…openinterface.inte`) | .NET, **sin** ofuscar | Nombres de los métodos, de los parámetros y de los campos; tipos de unidad |
| `bosch.dcn.bcpc.logic.dll` | ídem | .NET, el sinóptico | Cómo se numeran las dos mitades de una unidad doble |

## 2. Cómo

En una carpeta temporal, con un entorno de Python aparte (nada de esto va en la app ni en el repo):

- **MSI → archivos:** `olefile` saca los `.cab` del MSI y `bsdtar` los abre. Los nombres quedan
  truncados como en la tabla File del MSI (`bosch.dcn.openinterface.logi` = `…Logic.dll`).
- **NE de 16 bits (`SCS.DLL`):** un lector propio de la tabla de segmentos y `capstone` en modo x86-16.
  Las rutinas se encontraron por las constantes (0xC8, '$', '@', '#', '?').
- **.NET:** `dnfile` para las tablas de metadatos (TypeDef, MethodDef, Field, Param, Constant,
  MemberRef, #US) y un desensamblador de IL propio. El código está ofuscado con *control flow
  flattening* (un `switch` que salta entre bloques), así que no se lee de corrido: alcanza con la
  secuencia de llamadas `Write*`/`Read*` de cada `Marshal`/`Unmarshal` y las constantes.
  - `Marshal`/`Unmarshal` usan `System.IO.BinaryWriter`/`BinaryReader`: **little-endian**, un
    `bool` es **un byte**.
  - Los mensajes se arman **encadenando marshalers** (`new UInt16Marshaler(anterior, valor)`): el
    orden de las construcciones es el orden de los campos en el cable.
  - El ensamblado de interfaces no está ofuscado: de ahí salen los nombres de los parámetros
    (`IDcnOiCpcMM.SetMicrophoneSettings(cancelRequest, microOff)`) y de los campos
    (`MM_T_RTS {UnitId, DelegateId}`).

## 3. Lo que salió

### 3.1 Transporte serie Full (`SCS.DLL`)

- El checksum suma los bytes **como viajan**, con los dos 0xC8 del escape duplicado (el manual 2.0 dice
  lo contrario). Emisor: segmento 4, 0x0345–0x048b; receptor: 0x0856–0x0975. Detalle en
  [open-interface.md §3.1](open-interface.md); prueba `test_full_igual_que_scs_dll`.
- CORR = 0x13 si la suma da 0x37 (así el CHK invertido nunca vale 0xC8).
- La CCU no acepta datos hasta recibir un latido.
- `MDSC_COMMUNICATION_PARAMS` (tipo 15): `{wFnId=1; latido; reintentos; tiempo de reintento}`, y la
  respuesta viene con wFnId = 2.

### 3.2 TCP (`CCU_DDTK.exe`, para la CCU2)

El `byData` lleva el mensaje entero con el wFnId adelante (y el wError en las
respuestas); largo = datos + 16. Detalle en [open-interface.md §3.4](open-interface.md).

### 3.3 Votación (`VotingResultMarshaler`)

- **25** totales (no 24 como dice `FNID.DEF`).
- Formato comprimido: nibble bajo = delegado impar, alto = par; 14 = no votó, 15 = sin asignar.

### 3.4 Las 66 funciones sin documentar

Los enums `*_T_REQ_ID` / `*_T_MSG_ID` de `…Logic` tienen 66 códigos que no están en `FNID.DEF` ni en los
manuales 2.0, 4.0, 4.1 y 4.7 (cruzados por código, no por nombre). El método que usa cada una
(`DcnOiAppCpc*.Metodo`) dice qué marshaler la arma, y el marshaler dice el formato.

- **56 están en el plugin** (`dcn/extra.py`), con su formato, la descripción de cada parámetro y si el
  formato quedó del todo claro (`verificado`).
- **Las 10 de descarga de firmware (DL, 0x1201–0x120C) están comentadas**: la app no las puede mandar.
- **No hay ninguna que baje el firmware de la CCU a la PC**: todas las DL van hacia la CCU.

Formatos que no son obvios (de los marshalers):

| Función | En el cable | De dónde |
|---|---|---|
| `MM_C_MICSOFF_AFTER_SHIFT` 0x003F, `MM_C_SET_AMBIENT_CTRL` 0x0039 | `WORD 0` + `WORD (activo ? 0x0100 : 0)` | `UInt16Marshaler(…, 0)` + `UInt16Marshaler(…, b ? 256 : 0)` |
| `MM_C_SET_ATTENTION_TONE` 0x0038 | `WORD 0` + `WORD (tono << 8)` | `ldarg; ldc.i4.8; shl; conv.u2` |
| `MM_C_SET_MIC_SETTINGS` 0x0037 | `BOOL cancelRequest` + `BOOL microOff` | dos `BoolMarshaler` |
| `MM_C_SPK_INSERT/REPLACE` 0x0032/0x0033 | `WORD actual` + `WORD nuevo` | `TwoUnitsMarshaler` |
| `MM_C_RTS_REPLACE` 0x0029 | `{unidad, delegado}` actual + nuevo | `TwoRtsEntriesMarshaler` |
| `MM_C_NEXT_TO_SPEAK_ON_PC` 0x0047 (aviso) | `WORD unidad` + `WORD delegado` | `FirstToSpeakEntryMarshaler` |
| `VT_C_START_VOTING_REQ` … `VT_C_NEXT_VOTING_REQ` 0x0110–0x0115 (avisos) | `WORD número de votación` | `OnApplicationMessage` → `get_WordValue` → `set_VotingNumber` |
| `VT_C_NEXT_ROLL_CALLER` 0x0116 (aviso) | `WORD delegado` + `INT16 segundos restantes` | → `NextRollCallerEventArgs` |
| `VT_C_ATTENTION_TONE_NOTIFY` 0x011B (aviso) | `BYTE tono` | `get_ByteValue` |
| `VT_C_SET_ROLL_CALL` 0x0108 | `WORD n` + `WORD[n]` delegados | `DelegateRecordsMarshaler` |
| `VT_C_SET_VOTINGPARAMS2` 0x010B | `VT_T_VOTINGPARAMS` + leyendas de 11 bytes (tiempo, presentes, no votaron, 4 respuestas, llamado nominal) | `LegendParamsMarshaler` (la cantidad de respuestas, a verificar) |
| `AT_C_STORE_APP_ACCESS` 0x0907 | `BOOL MM` + `BOOL VT` + `BOOL IC` | `ApplicationAccessMarshaler` |
| `SC_C_FPGA_VERSION_INFO` 0x101C | `BYTE mayor` + `BYTE menor` | `FpgaVersionInfoMarshaler` |
| `SC_C_GET_MMP_VERSION_INFO` 0x101D | `INT16` ×2 + 4 bytes de serie | `MmpVersionInfoMarshaler` |
| `SI_C_SET_CHANNEL`/`GET_CHANNEL` 0x1102/0x1125 | pares `(BYTE número, BYTE tipo)` **sin** cantidad adelante | `ChannelListMarshaler` (recorre hasta el largo del arreglo) |

Las de formato dudoso (licencias, visor, canales individuales, pupitres de intérprete) aceptan `hex`
(los bytes tal cual) y devuelven los bytes crudos.

### 3.5 Tipos de unidad (`DCN_T_UNIT_TYPE`, en `…Interfaces`)

| Valor | Nombre de Bosch | Equipo |
|---|---|---|
| 0x00 / 0x10 | CONCENTUS_DELEGATE / _CHAIRMAN | Concentus |
| 0x01 / 0x11 | FLUSH_DELEGATE / FLUSH_CHAIRMAN | DDI en modo 4 (simple) / modo 1 (presidente) |
| **0x02** | **FLUSH_DUAL** | **DDI en modo 0, 2 o 3 (doble)** |
| 0x03 / 0x04 / 0x13 | DISC_DELEGATE / DISC_DELEGATE_DUAL / DISC_CHAIRMAN | unidades de discusión |
| 0x06 / 0x07 / 0x16 | WIRELESS_DISC_DELEGATE / _DUAL_DELEGATE / _CHAIRMAN | inalámbricas |
| 0x09 | VOTING_DELEGATE | votación |
| 0x30 / 0x40 / 0x50 | UNIT_ENTRANCE / UNIT_EXIT / UNIT_AMBIENT_MIC | DDI en modos 5, 6 y 7 |
| 0x60–0x63 | DESK/AAEX/DAEX/CIN_INTERPRETER | intérpretes y expansores |
| 0x71 / 0x72 | ACTIVE_DDB / RS232_DDB | datos |
| 0x81–0x84 | INTEGRUS_4/8/16/32_CHANNELS | Integrus |
| 0xE0 / 0xE1 / 0xE2 | FRONTPANEL_CCU / SLAVE_CCU / WAP_CCU | CCU, esclava, punto de acceso |

### 3.6 Las dos mitades de una unidad doble (`bosch.dcn.bcpc.logic`, `Area.UnitConnected`)

```
si Type ∈ {FLUSH_DUAL 0x02, DISC_DELEGATE_DUAL 0x04, WIRELESS_DISC_DUAL_DELEGATE 0x07}:
    id = UniqueId & 511
    si id > 256:  nombre = (UniqueId − 256) + "DualStationRight"
    si no:        nombre = UniqueId + "DualStationLeft"
```

Una DDI en modo doble tiene **una** dirección N (la que le da la CCU al direccionarla). La mitad
**izquierda** es N y la **derecha** es **N + 256**: con ese número llegan sus avisos de micrófono y
pedidos, y con ese número se le abre o cierra el micrófono. El plugin agrega la mitad derecha a la
lista de unidades cuando la CCU informa una unidad doble, y la app la muestra como «DDI N · micrófono
derecha» ([dcn-ddi.md](dcn-ddi.md)).

## 4. Para repetirlo

Los scripts quedaron fuera del repo (eran de una sola vez). Lo necesario para rehacerlo:
`pip install olefile dnfile capstone` en un entorno aparte, los MSI de `dvd/` y esta guía. Los nombres
de tipos y métodos de la sección 3 alcanzan para encontrar cada cosa en cualquier desensamblador de
.NET (ILSpy o dnSpy, por ejemplo).
