# Bosch DCN Next Generation por Open Interface (puerto serie)

Plan para controlar desde la app la central (CCU) de un sistema de conferencia **Bosch DCN Next
Generation**, la generación que se maneja por **RS-232**, con el protocolo **Open Interface**:

- micrófonos de banca;
- lista de pedidos de palabra y lista de oradores;
- tiempos;
- votación;
- asistencia;
- mensajes a las unidades;
- y, si hace falta, cámaras.

Todo esto se integra con lo que la app ya tiene:

- el **Cuerpo**, que dice quién se sienta en cada banca;
- el **Recinto** (la grilla de bancas);
- la **fila de oradores** (Bancas);
- **Seguir al micrófono**;
- **Quórum y votaciones**;
- el **PTZ**;
- la **botonera**.

> Estado: **fase 1 en curso**. Hecho: plugin `assets/plugins/bosch-dcn` (transporte serie Full y TCP
> **no bloqueantes**, capa de mensajes, cliente de la CCU con cola de pedidos, simulador, estado y
> eventos; pruebas en `assets/plugins/bosch-dcn/tests`), `dcn_unidad` en el Cuerpo (y en la planilla),
> herramientas MCP `dcn_*` (las de micrófonos en las guardas de aire), página **Legislatura →
> Conferencia** (grilla con el estado de cada banca, pedidos, oradores, unidades con «Aprender», conexión
> y tramas) y Bancas «al aire» / «terminar» por la central; botonera «Micrófonos de bancas» con caras
> (la foto de cada integrante, subida a mano o capturada con la cámara de su banca), borde que titila,
> insignia de turno y atenuado, en la tablet, el Satellite y el Stream Deck. **Fase 2 hecha:** votación
> (preparar, abrir, pausar, cerrar, parciales, nominal por banca, «Pasar a Quórum»), asistencia (con la
> opción de que Quórum cuente esos presentes) y tiempos de palabra en las unidades (reloj y avisos a un
> minuto y al terminar, solos desde la fila de oradores). Falta: fase 3. Nada se probó con una CCU real.
> [A VERIFICAR] el voto nominal llega por número de DELEGADO: sin base de delegados se toma el delegado
> de cada unidad de la asistencia o de los pedidos, y si no hay dato, delegado = unidad.
> Lo marcado **[A VERIFICAR]** sale de lecturas parciales del manual y hay que confirmarlo con el
> equipo o con el PDF completo de la versión 2.x.

---

## 1. Fuentes

| Documento | Qué aporta | Dónde |
|---|---|---|
| *DCN Next Generation Open Interface Release 2.0* (Bosch, 2005, 210 págs.) | **Transporte serie** («Full» y «Simple»): escape, encabezado, ACK/NAK, latido, checksum | Leído por partes en Yumpu; falta el PDF completo |
| *DCN NG Open Interface Release 4.5/4.7* (Bosch, 2020) | **Capa de mensajes y catálogo de funciones** con sus códigos (Apéndice B), estructuras, errores (Apéndice C) y ejemplos (Apéndice D). Transporte por TCP 9451 | `docs/referencias/bosch-dcn-ng-open-interface-4.7.pdf` (no se versiona: es de Bosch) |
| *DCN NG Open Interface Release 4.1* (Bosch, 2013) | Lo mismo; menciona RS-232 para CCU individual (SCCU) | `docs/referencias/bosch-dcn-ng-open-interface-4.1.pdf` |
| *DCN NG Installation and User Instructions* | Configuración del puerto RS-232 de la CCU (modos, velocidades) | ManualsLib |

**Supuesto de diseño.** La capa de mensajes (tipos, identificadores de función, estructuras) es la
misma en 2.x y 4.x: el 4.x documenta «cambios respecto de 2.68», o sea que la base es compartida.
Lo que cambia es el **transporte** (serie o TCP). Por eso el plugin separa:

- **transporte**: serie Full, serie Simple o TCP;
- **mensajes DCN**: comunes a todos.

**Licencia.** La CCU tiene que tener habilitado el Open Interface (**LBB4187/00**). Se activa con la
«Download and Licensing Tool» del DVD del sistema. Si falta, la CCU contesta un error de licencia
(Apéndice C) y la app tiene que decirlo con esas palabras.

---

## 2. Conexión física

- **Puerto:** el RS-232 de la CCU, que va a un adaptador USB-serie en la PC.
  - Cable **1 a 1** (macho del lado de la CCU, hasta unos 2 m), con **control de flujo por hardware
    RTS/CTS**; las demás señales están puenteadas adentro de la CCU (Open Interface 2.0 §2.2).
- **Configuración en la CCU:** el puerto RS-232 se pone en el modo del Open Interface (modo «Full»; el
  otro modo, «Camera control», es para conmutadores de video).
  - Velocidades que se pueden elegir: **9600, 19200, 57600 o 115200**; 8N1. Se eligen con las llaves
    S500 de la placa.
  - **De fábrica, el puerto 1 de la CCU es Full a 115200** (manual de instalación DCN NG, tabla S500;
    presentación de Bosch: «Port 1, 115200, Full»). El puerto 2 viene en 19200 para control de cámaras.
- **Controladores en simultáneo:** puede haber una PC de control de Bosch (DCN-SW) conectada por
  Ethernet. Para la CCU, esa es el controlador **primario** y la app el **secundario**.
  - Pueden convivir y manejar partes distintas.
  - Si los dos tocan lo mismo (por ejemplo, los micrófonos), gana el último. La app tiene que
    mostrar los cambios que vienen de afuera (notificaciones `*_ON_PC`).

---

## 3. Transporte serie «Full» (Open Interface 2.x)

### 3.1 Paquete

```
[ESC][TIPO][SEC]  [ MENSAJE DCN (solo en paquetes de datos) ]  [CORR][CHK]
 encabezado (3 bytes)                                           checksum (2 bytes)
```

- **ESC**: byte de escape, **0xC8** en el protocolo Full (Open Interface 2.0 §4.2.1: «The escape byte
  holds the value 0xC8»; confirmado en el `CCU_DDTK.exe` del DVD de Bosch). El 0x1B (ESC ASCII) de los
  manuales 4.x es el del protocolo **Simple**.
- **TIPO** (ASCII):

| Byte | Paquete | Con mensaje |
|---|---|---|
| `'$'` (0x24) | datos | sí |
| `'@'` (0x40) | ACK | no |
| `'#'` (0x23) | NAK (error de checksum o paquete roto) | no |
| `'?'` (0x3F) | latido: lleva la secuencia del *próximo* paquete a mandar | no |

- **SEC**: número de secuencia de 0 a 127 que avanza en cada paquete de datos. El ACK o NAK repite la
  secuencia del paquete que confirma.
- **CORR**: byte de corrección, 0x00 o 0x13.
- **CHK**: suma de todos los bytes del encabezado, el mensaje y CORR, módulo 256 e invertida bit a
  bit.
  - Se calcula con CORR = 0x00.
  - Si el resultado da igual a ESC, se pone CORR = 0x13 y se recalcula (sumar 0x13 y volver a
    invertir). Así **el checksum nunca vale ESC**.
- **ESC dentro de los datos:** un 0xC8 del largo o de los datos viaja **duplicado** (0xC8 0xC8).
  **El checksum suma los bytes tal como viajan, con los dos 0xC8.** El manual 2.0 §4.2.3 dice que el
  duplicado no entra en la suma, pero el código de Bosch hace lo contrario. Lo verifiqué desensamblando
  `SCS.DLL` (Serial Communication Services) del software de PC DCN-PC 3.1x (el que habla con la CCU por
  serie, dentro del MSI del DVD):
  - emisor (segmento 4, 0x0345–0x048b): la suma arranca en ESC+'$'+SEC (`sec + 0xEC`), cada 0xC8 se
    escribe dos veces y se suma dos veces; si la suma da 0x37 va CORR = 0x13; CHK = ~suma;
  - receptor (0x0856–0x0975): el primer 0xC8 queda pendiente sin sumar; el segundo suma 0xC8 y después
    el byte se procesa y vuelve a sumar.
  El plugin hace lo mismo (prueba `test_full_igual_que_scs_dll` con una referencia escrita desde el
  desensamblado, unidad 200 = 0xC8 incluida). El checksum nunca necesita escape (por eso existe CORR).
- **Antes de los datos, un latido:** la CCU no acepta datos hasta recibir un latido; el plugin lo manda
  apenas abre el puerto. Corte por falta de latido: 10 s. Mensaje de 5000 bytes como máximo.

### 3.2 Reglas de envío

- Entre un byte y el siguiente de un mismo paquete pasan **menos de 50 ms**. Si se pasa, se descarta
  lo recibido hasta el próximo ESC.
- **Un solo paquete de datos en vuelo** por sentido: no se manda otro hasta recibir su ACK.
- Si **no llega el ACK en 2 s**, se reenvía, con un máximo de **2 reenvíos**. Después se cancela y
  se reporta «la CCU no contesta».
- Si llega un NAK, se reenvía enseguida (cuenta como reenvío).
- Si llega un paquete con checksum mal, se contesta NAK con su secuencia.
- Si llega un paquete de datos repetido (la misma secuencia que el anterior), se contesta ACK y
  **no** se procesa de nuevo.
- **Latido:** si no hay tráfico, se manda `'?'` cada *T* segundos.
  - Si no llega nada de la CCU en *3T*, el enlace está caído.
  - *T* y los reintentos se negocian con `MDSC_COMMUNICATION_PARAMS`: latido de 0 a 1000 s,
    reintentos de 0 a 10.

### 3.3 Transporte serie «Simple»

`1B 24 0D` + mensaje + 1 byte de checksum (suma de encabezado y mensaje, módulo 256, invertida). Sin
secuencia, sin ACK, sin latido y sin escape. Si la CCU recibe un checksum mal contesta MDSC_NAK de largo 0.
El enlace se vigila con `SC_C_CHECK_LINK` cada 5 s. Ejemplo real de Bosch (MM_C_SPK_APPEND de la unidad
0x1C): `1b 24 0d 03 04 00 30 00 1c 00 60`, que es lo que arma el plugin (hay prueba).

### 3.4 Alternativa por red (CCU2 con Open Interface 4.x)

Para cuando se cambie la central:

- TCP al puerto **9451**.
- Cada mensaje va con un encabezado de 8 bytes: `DWORD tipo` y `DWORD largo` (incluye el
  encabezado), con los bytes menos significativos primero.
- Tipos:
  - `MESSAGETYPE_OIP_KeepAlive` = 0x00447027 (latido);
  - `MESSAGETYPE_OIP_ResponseProtocolError` = 0x00447020;
  - `MESSAGETYPE_OIP_Dcn`, que lleva adentro el mensaje DCN de la sección 4.

El `byData` lleva el mensaje DCN **entero, con el wFnId adelante** (y el wError en las respuestas): así lo
arma el `CCU_DDTK.exe` del DVD (largo = datos + 16, copia desde el wFnId). La CCU corta si no recibe nada
en 15 s. El plugin lo trae como `transporte: tcp`, con la misma capa de mensajes.

---

## 4. Capa de mensajes DCN (común a todos los transportes)

Todos los números van **con el byte menos significativo primero**. Tipos de dato: `BOOLEAN` y `BYTE`
de 1 byte, `WORD` de 2, `DWORD` de 4. Los textos son `CHAR[]` terminados en 0.

```c
typedef struct { BYTE byType; WORD wLength; BYTE byData[]; } T_MESSAGE;   // largo de los datos: hasta 8000 bytes

// byType:
#define MDSC_NAK                  2
#define MDSC_REMOTEPROCEDURE_REQ  3   // app → CCU
#define MDSC_REMOTEPROCEDURE_RSP  4   // CCU → app (siempre, una por pedido)
#define MDSC_NOTIFY               5   // CCU → app (solo de aplicaciones "arrancadas")
#define MDSC_COMMUNICATION_PARAMS 15  // parámetros del enlace (latido, reintentos)

// REQ: { WORD wFnId; <parámetros> }
// RSP: { WORD wFnId; WORD wError; <respuesta> }   // si wError != 0, no usar la respuesta
// NTF: { WORD wFnId; <datos> }
```

- **Identificador de función:** `wFnId = MKWORD(n, APP) = (APP << 8) | n`. En el cable va primero `n`
  y después `APP`.
- **Un pedido por vez:** después de cada pedido se espera su respuesta antes de mandar otro, como pide
  el manual. Las notificaciones pueden llegar en el medio.

| Aplicación | APP | Prefijo | Para qué la usa la app |
|---|---|---|---|
| Gestión de micrófonos | 0 | `MM_` | **Núcleo**: micrófonos, pedidos de palabra, oradores, tiempos |
| Votación | 1 | `VT_` | Votación nominal oficial para Quórum |
| Interpretación simultánea | 2 | `IN_` | Estado de canales y cabinas (solo monitoreo) |
| Base de delegados | 3 | `DB_` | Tarjetas y PIN; asistencia por tarjeta |
| Intercom | 7 | `IC_` | (No se usa) |
| Mensajes a unidades | 10 | `MD_` | Mensajes en el visor de las unidades, LED auxiliares |
| Pantallas de texto/estado | 12 | `LD_` | Texto en pantallas alfanuméricas de la sala |
| Configuración del sistema | 16 | `SC_` | Unidades conectadas, versión, batería y señal (inalámbricas) |
| (SI) | 17 | `SI_` | — |
| Control de cámaras | 21 | `CC_` | Asignar cámara y preset a cada unidad (opcional, ver 7.3) |
| Asistencia y control de acceso | 0x09xx | `AT_` | Presentes y ausentes para Quórum |

**Arrancar una aplicación.** Hay que llamar a su función *START* (`MM_C_START_MM`,
`VT_C_START_APP`…) para recibir sus notificaciones. Para dejarlas de recibir, a la *STOP*. La
respuesta trae `wNrOfInstances`, la cantidad de controladores enganchados.

- `MM_C_START_MON_MM` arranca la gestión de micrófonos **solo para mirar**: no se puede cambiar nada.
  Es lo que usa la app en modo «solo escuchar».

---

## 5. Catálogo de operaciones

La columna **Fase** dice cuándo se implementa: **1** = núcleo de micrófonos, **2** = votación y
asistencia, **3** = el resto. «REQ» es un pedido de la app; «NTF», un aviso de la CCU.

### 5.1 Configuración del sistema (SC, APP 16)

| Función | Código | Dir. | Uso | Fase |
|---|---|---|---|---|
| `SC_C_START_APP` / `SC_C_STOP_APP` | 0x1007 / 0x1008 | REQ | Recibir avisos de unidades que se conectan o desconectan | 1 |
| `SC_C_GET_CCU_VERSIONINFO` | 0x1006 | REQ | Versión de firmware y hardware para el diagnóstico | 1 |
| `SC_C_GET_CCU_CONFIG` | 0x100C | REQ | Unidades conectadas `{wUnitId, byUnitType}`: delegado, presidente, intérprete… | 1 |
| `SC_C_GET_CCU_CONFIG_PROPERTY` | 0x1033 | REQ | Lo mismo con propiedades (si tiene botón de prioridad, etc.) | 1 |
| `SC_C_GET_UNIT_IDS` | 0x1037 | REQ | Lista de identificadores de unidad | 1 |
| `SC_C_CONNECT_UNIT` / `SC_C_DISCONNECT_UNIT` | 0x1009 / 0x100A | NTF | Una unidad se enchufó o se fue: la banca se marca «sin unidad» | 1 |
| `SC_C_CCU_MODE_CHANGE` | 0x1010 | NTF | La CCU cambió de modo (por ejemplo, a mantenimiento) | 1 |
| `SC_C_CCU_REBOOT` | 0x100F | NTF | La CCU se reinició: hay que resincronizar todo | 1 |
| `SC_C_CHECK_LINK` | 0x1012 | REQ | Verificar que el enlace anda | 1 |
| `SC_C_REQ_SERIAL_NR` / `SC_C_SERIAL_NR` | 0x1035 / 0x1038 | REQ/NTF | Números de serie para el inventario | 3 |
| `SC_C_CONNECT_SLAVE_CCU` / `SC_C_DISCONNECT_SLAVE_CCU` | 0x100D / 0x100E | NTF | Varias CCU encadenadas | 3 |
| `SC_C_BATTERY_*`, `SC_C_SIGNAL_*`, `SC_C_LOW_BATTERY*`, `SC_C_UNIT_SIGNAL_QUALITY*` | 0x1027–0x102F, 0x1044–0x1045, 0x1039–0x103A | REQ/NTF | Unidades inalámbricas: batería y señal, como alarmas en Salud | 3 |
| `SC_C_*ENCRYPTION_ENABLED` | 0x103F–0x1041 | REQ/NTF | Estado del cifrado de la red inalámbrica | 3 |
| `SC_C_SET_EXPANDER_*` / `*_CHANGED` | 0x108A–0x108F | REQ/NTF | Ruteo, ganancia y control automático de ganancia de los expansores de audio | 3 |

Tipos de CCU (`SC_C_DCN_CCU` = 1, `CCUB` = 2, `CCU2` = 3, `CCUB2` = 4) y roles (`SC_C_STANDALONE`,
`MASTER`, `SLAVE`): se muestran en el diagnóstico.

### 5.2 Gestión de micrófonos (MM, APP 0): el núcleo

**Conceptos de la CCU:**

| Nombre de la CCU | En la app | Qué es |
|---|---|---|
| **SPK** (*speakers list*) | «Hablando» | Unidades con la palabra, con micrófono prendido o no |
| **RTS** (*request to speak*) | «Pidieron la palabra» | Lista ordenada de pedidos de palabra; cada elemento es `{wUnitId, wDelegateId}` |
| **CR** (*comment request*) | Pedidos de réplica | Pedidos para comentar sobre el orador actual (en las unidades dice «Response») |
| **CS** (*comment speakers*) | Réplica | Los que están comentando |
| **NBK** (*notebook*) | Micrófonos especiales | Lista de micrófonos de presidencia y especiales con su tipo |

**Parámetros generales** (`MM_T_CCU_GLOBAL_SETTINGS`):

- `wOperationMode`: modo de operación, ver la tabla de abajo.
- `wActiveMics`: máximo de micrófonos abiertos.
- `wMaxRTSListLen`: largo máximo de la lista de pedidos.
- `bAllowCancelRequests`: si el delegado puede cancelar su pedido.
- `bAllowMicroOff`: si el delegado puede apagar su micrófono.
- `wAttentionTone`: tono de atención.
- `bAmbientMicCtrl`: micrófono ambiente.
- `bPrioCancelAll`: si la prioridad del presidente cancela todo.

**Modos de operación** (`wOperationMode`):

| Valor | Constante | En castellano | Uso típico |
|---|---|---|---|
| 0 | `MM_C_OPERATOR_WITH_REQ_LIST` | **Operador con lista de pedidos** | **El que queremos**: el delegado pide, el operador (la app o la botonera) da la palabra |
| 1 | `MM_C_DELEGATE_WITH_REQ_LIST` | Delegado con lista | El micrófono se abre solo cuando hay lugar, por orden |
| 2 | `MM_C_DELEGATE_WITH_OVERRIDE` | Delegado con reemplazo | Al prender uno se apaga el más viejo |
| 3 | `MM_C_DELEGATE_WITH_VOICE` | Por voz | Se abre al hablar |
| 4 | `MM_C_OPERATOR_WITH_COMMENT_LIST` | Operador con réplicas | Operador más lista de réplicas |
| 5 | `MM_C_DELEGATE_WITH_PUSHTOTALK` | Pulsar para hablar | Mientras se aprieta |
| 8 | `MM_C_OPERATOR_NO_AC` | Operador, sin habilitación del delegado | |
| 9 | `MM_C_VOICE_NO_AC` | Voz, sin habilitación | |

**Funciones que pide la app (REQ):**

| Función | Código | Parámetros → Respuesta | Uso | Fase |
|---|---|---|---|---|
| `MM_C_START_MM` / `MM_C_STOP_MM` | 0x001E / 0x001F | — → `wNrOfInstances` | Recibir avisos de micrófonos (control) | 1 |
| `MM_C_START_MON_MM` / `MM_C_STOP_MON_MM` | 0x0045 / 0x0046 | — → `wNrOfInstances` | Solo mirar | 1 |
| `MM_C_GET_SETTINGS` / `MM_C_SET_SETTINGS` | 0x0020 / 0x0021 | `MM_T_CCU_GLOBAL_SETTINGS` | Leer o cambiar modo, cantidad de activos, etc. | 1 |
| `MM_C_SET_MIC_OPER_MODE` | 0x0034 | `WORD modo` | Cambiar el modo de operación | 1 |
| `MM_C_SET_ACTIVE_MICS` | 0x0035 | `WORD cantidad` | Máximo de micrófonos abiertos | 1 |
| **`MM_C_SET_MICRO_ON_OFF`** | **0x0022** | `{WORD wUnitId; BOOLEAN bMicroOn}` → — | **Abrir o cerrar el micrófono de una banca.** Un delegado que se prende entra a SPK si hay lugar; uno que se apaga **sigue en SPK** (para sacarlo, `MM_C_SPK_REMOVE`). Presidente: solo prende o apaga | **1** |
| `MM_C_SHIFT` | 0x0023 | — | **«Siguiente»**: pasa el primero de RTS a SPK (y saca al más viejo de SPK, según el modo) | 1 |
| `MM_C_SPK_GET` | 0x002E | — → `MM_T_CCU_SPKLIST {wNrOfSpk; {wUnitId,bMicroOn}[]}` | Leer quiénes hablan | 1 |
| `MM_C_SPK_APPEND` / `MM_C_SPK_REMOVE` / `MM_C_SPK_CLEAR` | 0x0030 / 0x0031 / 0x002F | `MM_T_SPK {wUnitId}` | Agregar, sacar o vaciar la lista de oradores | 1 |
| `MM_C_RTS_GET` | 0x0025 | — → `MM_T_CCU_RTSLIST {wNrOfRts; {wUnitId,wDelegateId}[]}` | Leer los pedidos de palabra, en orden | 1 |
| `MM_C_RTS_SET` | 0x0024 | lista RTS completa | Reordenar todos los pedidos de una vez | 1 |
| `MM_C_RTS_APPEND` / `MM_C_RTS_INSERT` / `MM_C_RTS_REMOVE` / `MM_C_RTS_CLEAR` | 0x003E / 0x0028 / 0x0027 / 0x0026 | `MM_T_RTS` / `MM_T_RTS_INSERT {buscar, nuevo}` | Agregar, intercalar, sacar o vaciar pedidos (desde la app) | 1 |
| `MM_C_NBK_GET` / `MM_C_NBK_SET` / `MM_C_NBK_REMOVE` / `MM_C_NBK_CLEAR` | 0x002B / 0x002A / 0x002D / 0x002C | `MM_T_NBK {wUnitId, wMicroType}` | Micrófonos especiales | 3 |
| `MM_C_CR_GET` / `MM_C_CR_REMOVE` / `MM_C_SHIFT_CR` | 0x0042 / 0x0040 / 0x0041 | | Réplicas | 3 |
| `MM_C_CS_GET` / `MM_C_CS_REMOVE` | 0x0044 / 0x0043 | | Réplicas al aire | 3 |
| `MM_C_SET_SPEECHTIME_SETTINGS` | 0x003B | `{wSpeechTimeLimit; bTimerOn; bHoldOnChairPriority; bShowRemainingTime; bLedFollowMicLed}` | Tiempo de uso de la palabra en las unidades (se puede sincronizar con el Orden del día) | 2 |
| `MM_C_LAST_MINUTE_WARNING` / `MM_C_TIME_FINISHED_WARNING` | 0x003C / 0x003D | `{wUnitId}` | Aviso en la unidad: «queda 1 minuto» o «se terminó el tiempo» | 2 |

**Avisos de la CCU (NTF):** llegan después de `MM_C_START_MM` o `MM_C_START_MON_MM`, tanto por
cambios hechos en las unidades como por la PC de control.

| Aviso | Código | Datos | Qué hace la app |
|---|---|---|---|
| `MM_C_MICRO_ON_OFF` | 0x0001 | `MM_T_MICRO_ONOFF_ON_PC {wUnitId; wMicroId; wPrioId}` | Micrófono prendido o apagado: la banca se pinta «abierta» o «cerrada» y se publica `conferencia/microfono` |
| `MM_C_CHAIR_MICS_ON` | 0x0002 | | Prioridad del presidente: todos callados |
| `MM_C_TIMER_ON_OFF` | 0x0003 | | Reloj de la palabra prendido o apagado |
| `MM_C_RTS_SET_ON_PC` / `_CLEAR_` / `_REMOVE_` / `_INSERT_` / `_REPLACE_` | 0x0004–0x0008 | `MM_T_RTS…` | Cambió la lista de pedidos: se actualiza la cola y el borde que titila |
| `MM_C_RTS_FIRST_ON_PC` | 0x0014 | `MM_T_RTS` | **El primero de la lista cambió**: es el «le toca» |
| `MM_C_NBK_SET_ON_PC` / `_CLEAR_` / `_REMOVE_` | 0x0009–0x000B | | Micrófonos especiales |
| `MM_C_SPK_SET_ON_PC` / `_CLEAR_` / `_APPEND_` / `_REMOVE_` / `_INSERT_` / `_REPLACE_` | 0x000C–0x0011 | `MM_T_SPK…` | Cambió la lista de oradores. `SPK_APPEND_ON_PC` es el caso típico: «delegado prendió y entró a SPK» |
| `MM_C_SET_MIC_OPER_MODE_ON_PC` | 0x0012 | `WORD` | Cambió el modo: si deja de ser «operador», se avisa que la botonera ya no manda |
| `MM_C_SET_ACTIVE_MICS_ON_PC` | 0x0013 | `WORD` | Cambió el máximo de abiertos |
| `MM_C_SET_SETTINGS_ON_PC` | 0x0015 | settings | Cambiaron los parámetros generales |
| `MM_C_CR_*_ON_PC`, `MM_C_CS_*_ON_PC` | 0x0016–0x001C | | Réplicas |

**Errores MM** (Apéndice C), que la app traduce al castellano:

| Error | Mensaje en la app |
|---|---|
| `MM_E_SPEAKERS_LIST_FULL` | «ya hay N micrófonos abiertos (el máximo)» |
| `MM_E_UNIT_NOT_CONNECTED` | «la unidad de la banca X no está conectada» |
| `MM_E_ILLEGAL_MIC_OPER_MODE` | «la CCU no está en modo operador» |
| `MM_E_NOT_IN_SPL_OR_NOB` | — |
| `MM_E_INSERT_SPEAKERS_LIST_FAILED` | — |
| `MM_E_ILLEGAL_MICRO_TYPE` | — |
| `MM_E_OPEN_CLOSE_FAILED` | — |

### 5.3 Votación (VT, APP 1): fase 2

| Función | Código | Uso |
|---|---|---|
| `VT_C_START_APP` / `VT_C_STOP_APP` | 0x0101 / 0x0102 | Engancharse a la votación (`VT_T_COMCONTROL {bResultNotify; …; wViewTimeAfterStop}`) |
| `VT_C_SET_GLOBAL_SETTINGS` | 0x0109 | `VT_T_GLOBAL_SETTINGS`: LED, quórum presente, reloj, abstención automática, voto ponderado, cuenta del primer voto |
| `VT_C_DOWNLOAD_SUBJECT` | 0x0107 | `VT_T_SUBJECT_REC {wVotingNumber; szVotingSubject; szLegendSubject}`: el tema, desde el Orden del día |
| `VT_C_SET_VOTINGPARAMS` | 0x010A | `VT_T_VOTINGPARAMS {wVotingMenu; wNrOfAnswerOptions; bOpenVoting; wInterimResultType; bCompressedResults}` |
| `VT_C_START_VOTING` / `VT_C_HOLD_VOTING` / `VT_C_RESTART_VOTING` / `VT_C_STOP_VOTING` | 0x0103 / 0x0105 / 0x0106 / 0x0104 | Abrir, pausar, reanudar y cerrar la votación |
| `VT_C_GET_RESULTS` | 0x010C | `VT_T_RESULT_REC {wVotingNumber; dwNrOfPresent; dwNrOfNotVoted; dwNrOfVotes[]; …; byDelegateVotes[]}`: totales y **voto por unidad** (nominal) |
| `VT_C_RESULTSNOTIFY` (NTF) | 0x0117 | Resultados parciales y finales en vivo |
| `VT_C_GET/SET/START_ATTENTION_TONE` | 0x0118–0x011A | Tono de atención |

**Integración.** Los resultados entran a **Quórum** como «resultado oficial de la CCU». El voto por
unidad se convierte en voto por integrante con el mapa unidad↔banca. Sirven para el CSV nominal, el
acta y los gráficos de votación (T43).

### 5.4 Asistencia y acceso (AT, 0x09xx): fase 2

| Función | Código | Uso |
|---|---|---|
| `AT_C_START_AT_APP` / `AT_C_STOP_AT_APP` | 0x0901 / 0x0902 | Engancharse (`AT_T_APPCONTROL`: control o monitoreo) |
| `AT_C_STORE_SETTING` | 0x0903 | `AT_T_SETTINGS`: presencia por banca o en la entrada, tarjeta, PIN |
| `AT_C_ACTIVATE` | 0x0904 | `AT_T_ACTIVATE {bAttendanceOn; bAccessOn}` |
| `AT_C_HANDLE_IDENTIFICATION` | 0x0905 | Identificaciones (tarjeta o PIN) |
| `AT_C_GET_INDIV_REGISTRATION` / `AT_C_SEND_INDIV_REGISTRATION` (NTF) | 0x0906 / 0x090A | Presente o ausente **por delegado** (`AT_T_DEL_ATTEND {wUnitId; wDelegateId; byAttend}`) |
| `AT_C_SEND_TOTAL_REGISTRATION` (NTF) | 0x090B | Totales `{wAttend; wLeave}` |

**Integración.** Los presentes de la CCU alimentan el padrón de **Quórum** («presentes según la
CCU»). Si difieren de los presentes de la app, se muestran los dos y decide el operador.

### 5.5 Mensajes, pantallas y otros: fase 3

| Aplicación | Funciones (código) | Uso |
|---|---|---|
| MD (10) | `MD_C_SEND_MESSAGE_TO_UNITS` (0x0A00): texto LCD, duración y lista de unidades; `MD_C_CLEAR_MESSAGE_ON_UNITS` (0x0A01); `MD_C_START/STOP_MON_MD` (0x0A02/03); `MD_C_AUX_LED_CONTROL` (0x0A04: `{wUnitId; byLedMask}`); `MD_C_REQ_BUTTON_ON_OFF` (0x0A0A) | Avisos en la unidad del orador («queda 1 minuto», «redondee»), LED auxiliares de tally |
| LD (12) | `LD_C_START/STOP_LD_APP` (0x0C0C/0D), `LD_C_SEND_ANUM_DATA` (0x0C07), `LD_C_CLEAR_DISPLAY_NR` (0x0C0B), `LD_C_STORE_DISPLAY_SETTING` (0x0C0E) | Texto en pantallas de la sala (orador actual, punto del orden del día) |
| IN (2) | `IN_C_START/STOP_MON_IN` (0x0236/37), `IN_C_CHAN_STATUS`, `IN_C_DESK_UPDATE`, `IN_C_BOOTH_UPDATE`, `IN_C_FLOOR_DISTRIBUTION`, `IN_C_SPEAKSLOWLY_SIGN`, `IN_C_HELP_SIGN`… | Solo monitoreo: canales de interpretación activos; avisos «hable más despacio» y «ayuda» de las cabinas, que van a la app |
| DB (3) | `DB_C_START/STOP_APP` (0x0301/02), `DB_C_DOWNLOAD_CCU` (0x0306), `DB_C_CCU_APPLY_ONE` (0x0305), `DB_C_CLEAR_CCU` (0x0304), `DB_C_MAINT_CCU` (0x0303) | Cargar delegados (nombre, tarjeta, PIN) desde el **Cuerpo** a la CCU: una sola fuente de nombres |
| IC (7) | `IC_C_START/CLOSE_IC_APP`, `IC_C_SET/CLEAR_LINKS` | No se usa |

### 5.6 Control de cámaras de la CCU (CC, APP 21): opcional

`CC_C_START/STOP_CAMERA_APP` (0x1501/02), `CC_C_SET_CAMERA_ACTIVITY` (0x1503),
`CC_C_SET/GET_GLOBAL_SETTINGS` (0x1504/0x150A), `CC_C_SET_CAMERA_ASSIGNMENT` (0x1506: por unidad
`{wCameraNumber; byPreposNumber; szSeatText_1; szSeatText_2}`), `CC_C_CLEAR_CAMERA_ASSIGNMENTS`
(0x1505), `CC_C_SET_CAMERA_ID` / `CLEAR_CAMERA_IDS` (0x1509/08), `CC_C_SEND_DATA` /
`CC_C_RECEIVE_DATA` (0x1507/0x1510: tramas crudas hacia y desde el conmutador).

**Decisión.** Las cámaras las maneja **la app** (PTZ, presets con foto, interlock de aire). La CC de
la CCU queda **apagada** (`CC_C_SET_CAMERA_ACTIVITY false`) para que no compita. Opcionalmente, la
app puede **escribir** las asignaciones en la CCU (`CC_C_SET_CAMERA_ASSIGNMENT` desde el preset de
cada banca), por si la sala se usa algún día sin la app.

---

## 6. Secuencia de conexión y resincronización

1. **Abrir el puerto** (velocidad, 8N1) y mandar latidos `'?'` hasta que la CCU conteste: enlace
   arriba.
2. Mandar `MDSC_COMMUNICATION_PARAMS` (tipo 15, solo serie): `{wFnId = 0x0001; wHeartbeatTime;
   wNrRetries; wRetryTime}` con 5 s, 2 y 2 s; la CCU contesta el mismo tipo con wFnId 0x0002 y los
   valores que quedaron (2.0 §3.2.4).
3. `SC_C_GET_CCU_VERSIONINFO` y `SC_C_START_APP`. Después `SC_C_GET_CCU_CONFIG`, que da el mapa de
   unidades conectadas y su tipo.
4. Micrófonos: `MM_C_START_MM` (o `MM_C_START_MON_MM` en modo «solo escuchar») y después
   `MM_C_GET_SETTINGS`, `MM_C_SPK_GET`, `MM_C_RTS_GET` y, si se usan, `MM_C_NBK_GET`, `MM_C_CR_GET` y
   `MM_C_CS_GET`.
5. Fase 2: `VT_C_START_APP` y `AT_C_START_AT_APP` si están habilitadas en la configuración.
6. Publicar el estado completo en el bus (`dcn/estado`) y marcar «sincronizado».

**Si se cae** (no contesta, `SC_C_CCU_REBOOT` o el adaptador se desenchufó):

- se reintenta la conexión cada 5 s;
- al volver, se repite todo **desde el paso 1**;
- nunca se dan por buenas las listas viejas.

Mientras tanto, la app muestra **«sin control de la CCU»** en rojo. La sala sigue andando sola: la
CCU no depende de la app.

---

## 7. Modelo de datos e integración con la app

### 7.1 Unidad ↔ banca

La CCU habla de **unidades** (`wUnitId`); la app, de **bancas** (Cuerpo y Recinto).

- **Mapa:** campo nuevo `dcn_unidad` en cada integrante del Cuerpo (`Config/cuerpo.json`). También
  admite una planilla CSV (columna `unidad_dcn`).
- **Aprender:** en la GUI, «Aprender unidades»:
  1. se elige una banca de la grilla;
  2. alguien aprieta el botón de micrófono de esa banca en la sala;
  3. el primer `MM_C_MICRO_ON_OFF` o `RTS_*_ON_PC` que llega asigna esa unidad a esa banca.
  - Sigue con la banca siguiente, para recorrer la sala en dos minutos.
- **Mostrar:** las unidades conectadas sin banca y las bancas sin unidad.
- **Presidente:** las unidades de tipo presidente se marcan; su micrófono nunca se cierra desde la
  botonera sin confirmación.

### 7.2 Estado que mantiene el plugin

Se publica en el bus como `plugin/bosch-dcn/dcn/estado`:

```json
{
  "enlace": "ok|caido|sincronizando", "ccu": {"tipo": "DCN-CCU", "version": "…"},
  "modo": "operador_con_lista", "max_abiertos": 4, "max_pedidos": 50,
  "unidades": [{"unidad": 12, "tipo": "delegado|presidente|…", "conectada": true, "banca": "5"}],
  "abiertos": ["5", "7"],
  "hablando": [{"banca": "5", "microfono": true}],
  "pedidos":  [{"banca": "9", "delegado": 31, "turno": 1}, {"banca": "2", "turno": 2}],
  "siguiente": "9",
  "replicas": {"pedidos": [], "hablando": []},
  "tiempo": {"limite_s": 300, "reloj": true},
  "votacion": {"estado": "cerrada|abierta|pausada", "numero": 12, "parciales": {…}},
  "asistencia": {"presentes": 38, "ausentes": 7}
}
```

**Eventos puntuales** (el sufijo va después de `plugin/bosch-dcn/`):

| Evento | Datos | Quién lo usa |
|---|---|---|
| `conferencia/microfono` | `{banca, abierto}` | El **evento estándar** que ya usan Seguir al micrófono (fuente «conferencia»), la fila de oradores y el Recinto |
| `conferencia/pedido` | `{banca, turno, accion: alta\|baja}` | |
| `conferencia/siguiente` | `{banca}` | |
| `conferencia/modo` | | |
| `dcn/enlace` | | |
| `votacion/parcial`, `votacion/resultado` | | |
| `asistencia/cambio` | | |

### 7.3 Con lo que ya existe

| Módulo de la app | Integración |
|---|---|
| **Seguir al micrófono** (`legislatura/SeguirMicrofono`) | Ya escucha `plugin/+/conferencia/microfono`. Con la DCN conectada, la fuente «conferencia» es la recomendada (más precisa que el nivel de audio) |
| **Bancas / fila de oradores** | La **cola de pedidos de la app se espeja con la RTS** de la CCU: un pedido en la CCU aparece en la fila, y quitarlo de la fila lo saca de la RTS. **«Al aire» de una banca** = PTZ al preset + escena armada + zócalo + reloj + **`MM_C_SET_MICRO_ON_OFF true`** (reemplaza el gancho `microfono` de Bancas, que hoy solo desmutea una fuente de audio). «Terminar» = micrófono apagado + `MM_C_SPK_REMOVE` |
| **Recinto** (grilla) | Cada banca muestra el estado del micrófono (abierto, pidió con su turno, «le toca», sin unidad). El menú suma «Abrir micrófono» y «Cerrar micrófono» |
| **Orden del día** | El tiempo asignado al punto se manda a las unidades (`MM_C_SET_SPEECHTIME_SETTINGS`). Último minuto y tiempo cumplido van a la unidad del orador (`MM_C_LAST_MINUTE_WARNING`, `MM_C_TIME_FINISHED_WARNING`) |
| **Quórum** | Votación (VT) y asistencia (AT) como fuente oficial. Diferencias con lo de la app, a la vista |
| **PTZ** | Sin cambios: la banca ya tiene su preset (con foto, T41) |
| **Zócalos** | `{orador}` sale del integrante de la banca abierta, ya validado |
| **Acta** | Lo hace la app: registra quién habló, desde y hasta qué hora (por los avisos de micrófono), y los resultados de las votaciones de la CCU con su hash |

---

## 8. Reglas de operación y seguridad

- **Lo que hace una persona manda.** Los clics de la botonera, la GUI y las órdenes de una persona por
  MCP abren y cierran micrófonos al toque.
- **Lo automático solo propone.** Seguir al micrófono, reglas y asistente sin humano: proponen; no
  abren micrófonos solos, salvo que el operador lo habilite explícitamente (`auto_abrir`, apagado de
  fábrica). Pasa por las guardas de aire del MCP (`McpAire`): `dcn_microfono`, `dcn_siguiente`,
  `dcn_cerrar_todos` y `dcn_modo` son herramientas **de aire**.
- **Modo en vivo.** Cambiar el modo de operación o el máximo de abiertos, y desconectar, piden
  CONFIRMAR y un motivo (igual que los ajustes protegidos).
- **Presidente.** Cerrar el micrófono del presidente pide confirmación siempre. «Cerrar todos» **no**
  toca al presidente, salvo con `incluir_presidente: true` y confirmación.
- **Solo escuchar.** Configuración `solo_escuchar: true`: usa `MM_C_START_MON_MM` y nunca manda
  pedidos que cambien nada. Es para la primera prueba en la sala o cuando el operador de Bosch quiere
  el control exclusivo.
- **Límite de pedidos.** Un pedido en vuelo por vez, como manda el protocolo. Además, como mucho 10
  pedidos por segundo desde superficies, para que un botón trabado no inunde el enlace.
- **Si la CCU no está en modo operador**, los botones de abrir siguen andando cuando la CCU los
  acepta. Si contesta `MM_E_ILLEGAL_MIC_OPER_MODE`, se muestra «la CCU no está en modo operador:
  cambiá el modo (Conferencia → Modo) o pedíselo al operador de sonido».
- **Registro.** Cada orden a la CCU queda en `Logs/dcn.log`: quién, desde dónde (GUI, botonera:
  aparato, MCP: rol), qué, resultado. Con `traza: true`, también las tramas crudas en hexadecimal.

---

## 9. Herramientas MCP

Categoría `conferencia`. El rol asistente tiene las de lectura y las de propuesta; las de aire pasan
por la guarda. Todas devuelven errores en castellano, con la banca y no la unidad.

| Herramienta | Argumentos | Devuelve / hace | Aire |
|---|---|---|---|
| `dcn_estado` | — | El estado completo de 7.2 | no |
| `dcn_conectar` | `{puerto?, velocidad?, protocolo?: full\|simple, transporte?: serie\|tcp, host?, solo_escuchar?}` | Conecta o reconecta (también desde la GUI) | no (pide CONFIRMAR en vivo) |
| `dcn_desconectar` | — | Suelta la CCU (la sala sigue andando) | no (CONFIRMAR en vivo) |
| `dcn_puertos` | — | Puertos serie disponibles (nombre, descripción, USB VID:PID) | no |
| `dcn_microfono` | `{banca \| unidad, abierto: bool}` | **Abre o cierra el micrófono de una banca** | **sí** |
| `dcn_siguiente` | — | `MM_C_SHIFT`: da la palabra al primero de la lista | **sí** |
| `dcn_cerrar_todos` | `{incluir_presidente?}` | Cierra todos los micrófonos abiertos (SPK) | **sí** |
| `dcn_pedidos` | — | La lista de pedidos en orden, con banca, nombre y turno | no |
| `dcn_pedido` | `{banca, accion: agregar\|quitar\|primero\|antes_de, referencia?}` | Edita la lista de pedidos (`RTS_APPEND`/`REMOVE`/`INSERT`) | no |
| `dcn_pedidos_limpiar` | — | `MM_C_RTS_CLEAR` | no (CONFIRMAR) |
| `dcn_oradores` | — | Lista de oradores (SPK) con micrófono sí o no | no |
| `dcn_orador_quitar` | `{banca}` | `MM_C_SPK_REMOVE` | sí |
| `dcn_modo` | `{modo: operador\|delegado_lista\|reemplazo\|voz\|pulsar\|…, max_abiertos?, max_pedidos?}` | Cambia el modo de operación y los límites | **sí** |
| `dcn_tiempo` | `{limite_s?, reloj?: bool, aviso?: ultimo_minuto\|terminado, banca?}` | Tiempos y avisos en la unidad | no |
| `dcn_unidades` | — | Unidades conectadas, tipo, banca asignada; bancas sin unidad | no |
| `dcn_unidad_asignar` | `{banca, unidad}` / `{aprender: banca}` | Asigna, o espera el próximo botón | no |
| `dcn_mensaje` | `{texto, bancas?: [..] \| todas, segundos?}` | Mensaje en el visor de las unidades (MD) | no |
| `dcn_votacion` | `{accion: preparar\|abrir\|pausar\|reanudar\|cerrar, tema?, opciones?, abierta?, numero?}` | Votación en la CCU (fase 2) | no (CONFIRMAR para abrir y cerrar) |
| `dcn_votacion_resultado` | `{numero?}` | Totales y nominal, ya convertidos a integrantes | no |
| `dcn_asistencia` | — | Presentes y ausentes según la CCU (fase 2) | no |
| `dcn_traza` | `{activa: bool}` / `{ultimas: n}` | Tramas crudas para diagnóstico | no |

Agregar estas herramientas a `McpEtiquetas` (sinónimos: «dale la palabra a», «abrí el micrófono
de», «cerrá todos», «quién pidió la palabra», «siguiente orador»), al tema de ayuda `conferencia` y
a la plantilla de la botonera.

**Implementación.** Las herramientas las declara **el plugin** con `@plugin.control(...)`. La app
las expone como herramientas MCP de primera clase, con nombre propio, a través de un registro de
herramientas de plugin **[falta en el núcleo: hoy existe `plugin_control {id, nombre, args}`;
agregar el registro de herramientas con nombre y esquema, que también pide la allowlist de T51]**.
Hasta entonces se llaman con `plugin_control {"id": "bosch-dcn", "nombre": "microfono", …}`.

---

## 10. Interfaz gráfica

### 10.1 Página «Conferencia» (riel, junto a Recinto y Personas)

- **Barra superior:**
  - estado del enlace (punto verde, ámbar o rojo, más «sincronizado hace 3 s»);
  - CCU y versión;
  - modo actual (combo, con CONFIRMAR en vivo);
  - micrófonos abiertos «2 de 4»;
  - **«Cerrar todos»**;
  - interruptor **«Solo escuchar»**.
- **Centro: la grilla del Recinto** (el mismo componente `RecintoGrilla`) con el estado de la CCU
  sobre cada banca:
  - **micrófono abierto**: fondo rojo con «● AL AIRE»;
  - **pidió la palabra**: ámbar con el número de turno;
  - **le toca** (primero en la lista): **borde rojo que titila**;
  - **en la lista de oradores con el micrófono apagado**: borde blanco;
  - **presidente**: borde dorado;
  - **sin unidad o desconectada**: gris con un ícono de enchufe.
  - Un toque abre o cierra el micrófono. El menú del toque largo trae: al aire con cámara, quitar
    pedido, mandar mensaje, ver integrante.
- **Derecha: dos listas.**
  - **«Pidieron la palabra»**: se reordena arrastrando (`MM_C_RTS_SET`), con los botones «Dar la
    palabra» y «Quitar».
  - **«Hablando»**: con el reloj de cada uno y «Terminar».
  - Abajo, **«Siguiente ▶»** (`MM_C_SHIFT`).
- **Pestaña «Unidades»:** la tabla unidad ↔ banca, «Aprender unidades» (asistente paso a paso),
  unidades sin banca y bancas sin unidad, batería y señal de las inalámbricas.
- **Pestaña «Votación»** (fase 2): preparar (tema desde el Orden del día), abrir, pausar, cerrar,
  parciales en vivo, resultado oficial y «Pasar a Quórum».
- **Pestaña «Conexión»:**
  - puerto (lista de `dcn_puertos`), velocidad, protocolo Full o Simple, byte de escape (avanzado),
    transporte serie o TCP;
  - «Probar conexión»;
  - **traza en vivo** (tramas en hexadecimal con su decodificación: «REQ MM_C_SET_MICRO_ON_OFF
    unidad 12 → ON · RSP OK 18 ms»);
  - contadores de reenvíos, NAK y checksum mal.

### 10.2 En lo que ya existe

- **Dirección:** una fila compacta con «Hablando: Banca 5 · María Pérez (2:13)», «Le toca: Banca 9» y
  el botón «Siguiente».
- **Recinto (página PTZ):** el estado del micrófono en la grilla y «Abrir o cerrar micrófono» en el
  menú.
- **Salud:** alarmas «CCU sin enlace», «unidad desconectada en Banca X», «batería baja».
- **Plugins:** la tarjeta del plugin `bosch-dcn` con su panel (`plugin.panel`): puerto, velocidad y
  estado.

---

## 11. Botonera: «Micrófonos de bancas» con caras

Lo que pidió el operador: **una botonera para decidir a qué bancas se les da audio, con la cara de
cada uno en el botón**. Sirve igual en la tablet (web), en el Stream Deck (plugin
`superficie-streamdeck`) y en Companion (Satellite).

### 11.1 Comportamiento

| Gesto | Acción |
|---|---|
| **Toque en una banca** con el micrófono cerrado | **Le da audio**: `dcn_microfono {banca, abierto: true}`. Si ya está el máximo abierto, la CCU contesta lista llena: el botón titila en ámbar 1 s y la botonera muestra «máximo N abiertos» |
| **Toque en una banca abierta** | Le corta el audio: `dcn_microfono {banca, abierto: false}` (sigue en la lista de oradores) |
| **Mantener apretado** (más de 0,6 s) | **Al aire completo**: `banca_al_aire {banca}` (cámara, escena armada, zócalo, reloj y micrófono) |
| Toque en la banca del **presidente** abierta | Pide confirmación (segundo toque dentro de 5 s, como las celdas de aire) |
| **«Siguiente ▶»** | `dcn_siguiente`: le da la palabra al primero de la lista de pedidos |
| **«Cerrar todos»** | `dcn_cerrar_todos` (con confirmación) |
| **«Limpiar pedidos»** | `dcn_pedidos_limpiar` (con confirmación) |
| **Página ▶** | Si las bancas no entran en una página (Stream Deck de 15 teclas): página siguiente, por filas del recinto |

### 11.2 Lo que muestra cada botón

En orden de prioridad: el primero que se cumple manda en el fondo; el borde y la insignia se suman.

| Estado de la banca | Fondo | Borde | Insignia | Fuente del dato |
|---|---|---|---|---|
| Micrófono **abierto** | **Rojo** (`#c62828`) con «● AIRE» | — | tiempo hablando «2:13» | `abiertos` / `MM_C_MICRO_ON_OFF` |
| **Le toca** (primero de la lista de pedidos) | Cara normal | **Rojo titilando** (1 Hz) | «1» | `siguiente` / `MM_C_RTS_FIRST_ON_PC` |
| **Pidió la palabra** (en la lista, no primero) | Cara normal | Ámbar fijo | turno «2», «3»… | `pedidos` |
| En la lista de oradores con micrófono apagado | Cara atenuada | Blanco fijo | — | `hablando[].microfono == false` |
| **Presidente** | Cara | Dorado fino (siempre) | «P» | tipo de unidad |
| **Sin unidad** o desconectada | Gris, cara en blanco y negro | — | enchufe | `unidades[].conectada` |
| Enlace con la CCU **caído** | Toda la página atenuada | — | «sin CCU» en la celda de estado | `enlace` |
| Normal | Cara | — | número de banca | — |

**Siempre:** número de banca arriba a la izquierda y apellido abajo, con elipsis.

**La cara** es la `foto` del integrante en el Cuerpo (Fotos/cuerpo/<id>.jpg), que se carga de dos
maneras (Legislatura → Cuerpo, o `legislador_foto`):

1. **subida a mano** (una imagen: se recorta cuadrada);
2. **capturada con la cámara de su banca**: la PTZ va al preset de la banca (si no está al aire) y se
   toma lo que se ve.

Sin foto, el botón muestra el número de banca y el apellido.

### 11.3 Lo que hay que agregar a la botonera (núcleo)

Hoy una celda tiene `texto, icono (emoji), color, fondo, titilar, activo, valor, nivel`. Para esto
hace falta:

1. **`imagen`** en el estilo y como efecto de ligadura. Es una referencia, no un archivo:
   - `ptz:<camara>/<posicion>`, `rostro:<banca>` o `cuerpo:<banca>`;
   - la app la resuelve a un archivo;
   - el gateway la sirve en `/botonera/img/<hash>.jpg` con el mismo token de la tablet, sin listar
     directorios;
   - la web la pone de fondo de la celda;
   - el Satellite la compone en el bitmap de la tecla;
   - `sup.render_tecla()` la dibuja para el Stream Deck.
2. **`borde`** (color) y **`borde_titilar`** (bool): efectos nuevos, independientes de `fondo` y
   `titilar`, para que titile **solo el borde**. En teclas sin borde (Stream Deck), el
   `render_tecla` dibuja un marco de 6 px.
3. **`insignia`**: texto chico en una esquina (turno, «P», tiempo).
4. **`atenuar`** (bool): para «sin unidad» y «enlace caído».
5. **Celdas generadas desde una lista del bus.** Hoy las plantillas se arman una vez. Para esta
   página conviene un tipo de página **dinámica**: `{"generar": {"de": "recinto", "plantilla_celda": {…}}}`,
   que se rearma sola cuando cambia el Recinto o el Cuerpo, con `{{banca}}` y `{{nombre}}` en las
   acciones y ligaduras.
   - Mientras tanto: plantilla **«Micrófonos de bancas»**, que se genera desde el Recinto como la
     plantilla «Recinto» actual y se vuelve a generar con un botón.
6. **Mantener apretado:** acción `al_mantener` (además de `accion`), con umbral configurable.
   Satellite y Stream Deck ya mandan apretar y soltar por separado.

**Ligaduras de una celda de banca** (ejemplo de la plantilla generada):

```json
{
  "x": 3, "y": 1, "tipo": "boton",
  "estilo": {"texto": "5\nPérez", "imagen": "rostro:5"},
  "accion": {"tipo": "alternar", "tema": "plugin/bosch-dcn/dcn/estado", "clave_activo": "abiertos contiene 5",
             "si": {"tipo": "herramienta", "herramienta": "dcn_microfono", "args": {"banca": "5", "abierto": false}},
             "no": {"tipo": "herramienta", "herramienta": "dcn_microfono", "args": {"banca": "5", "abierto": true}}},
  "al_mantener": {"tipo": "herramienta", "herramienta": "banca_al_aire", "args": {"banca": "5"}},
  "estado": [
    {"tema": "plugin/bosch-dcn/dcn/estado", "clave": "abiertos", "op": "contiene", "valor": "5", "efecto": "fondo", "a": "#c62828"},
    {"tema": "plugin/bosch-dcn/dcn/estado", "clave": "siguiente", "op": "==", "valor": "5", "efecto": "borde", "a": "#ff1744"},
    {"tema": "plugin/bosch-dcn/dcn/estado", "clave": "siguiente", "op": "==", "valor": "5", "efecto": "borde_titilar", "a": true},
    {"tema": "plugin/bosch-dcn/dcn/estado", "clave": "pedidos[banca=5].turno", "op": "existe", "efecto": "insignia", "a": "{{v}}"},
    {"tema": "plugin/bosch-dcn/dcn/estado", "clave": "unidades[banca=5].conectada", "op": "==", "valor": false, "efecto": "atenuar", "a": true}
  ]
}
```

Esto también pide al núcleo, además de lo de 11.3:

- operador `contiene` en las ligaduras (hoy hay `==`, `existe`, comparaciones);
- claves con filtro `lista[campo=valor].campo`;
- acción `alternar`, que elige según el estado actual.

**Última fila fija:**

- `Siguiente ▶`;
- `Cerrar todos`;
- texto «Abiertos 2/4»;
- texto con quien habla;
- `Limpiar pedidos`;
- `TAKE`.

---

## 12. El plugin `bosch-dcn`

```
assets/plugins/bosch-dcn/
  plugin.json          id, nombre, descripción, config, dependencias: [pyserial] (ya vendoreado)
  plugin.py            arranque, panel, controles (@plugin.control), eventos, bucle de reconexión
  dcn/transporte.py    SerieFull, SerieSimple, Tcp (misma interfaz: enviar(msg) / recibir() -> msg)
  dcn/mensajes.py      T_MESSAGE, REQ/RSP/NTF, empaquetar y desempaquetar con struct ('<'), tabla de códigos
  dcn/funciones.py     constantes del Apéndice B (wFnId, modos, errores) y estructuras por función
  dcn/estado.py        modelo de 7.2 + aplicar notificaciones + resincronizar
  dcn/simulador.py     una CCU falsa (unidades, modos, listas, votación) por pty o TCP, con los dos ESC
  tests/               checksum y corrección 0x13, relleno de ESC, ACK/NAK/reenvío, tramas de ejemplo
                       del Apéndice D, secuencia de arranque contra el simulador
```

**`config`:**

```json
{"transporte": "serie", "puerto": "", "velocidad": 19200, "protocolo": "full",
 "escape": "0xC8", "host": "", "puerto_tcp": 9451, "latido_s": 5, "reintentos": 2,
 "solo_escuchar": true, "auto_abrir": false,
 "votacion": false, "asistencia": false, "traza": false}
```

`solo_escuchar` viene **prendido de fábrica**: la primera vez solo escucha. El operador lo apaga
cuando ve que la sala responde bien.

- **Simulador:** con `transporte: "simulador"` el plugin arranca una CCU falsa. Con ella se puede
  probar la página, la botonera y las herramientas MCP sin el equipo, y sirve para las pruebas
  automáticas.
- **Sin dependencias nativas:** `pyserial` ya está en el bundle (portable). En Windows el puerto es
  `COMn`; en Mac, `/dev/cu.usbserial-*`; en Linux, `/dev/ttyUSB*`.
  - Los adaptadores con chip FTDI o CH340 no necesitan instalar nada en Mac ni en Linux. En Windows
    se documenta qué driver usa cada adaptador; no se instala nada desde la app.

---

## 13. Pruebas

1. **Unitarias** (sin equipo):
   - checksum y corrección;
   - relleno de ESC;
   - ida y vuelta de cada estructura;
   - decodificación de las tramas de ejemplo del manual;
   - máquina de ACK, NAK y reenvío con pérdidas inyectadas;
   - latido y caída;
   - resincronización.
2. **Integración con el simulador:** arranque completo, abrir y cerrar con el máximo de abiertos,
   lista de pedidos (agregar, reordenar, siguiente), cambio de modo desde «afuera» (notificaciones
   `*_ON_PC`), reinicio de la CCU, desconexión del USB.
3. **App:**
   - la botonera web con el simulador (caras, borde titilando, insignias);
   - Seguir al micrófono con fuente «conferencia»;
   - Bancas «al aire» abre el micrófono;
   - votación hacia Quórum.
4. **En la sala, con la CCU real** (lista para el día de la prueba):
   1. Licencia del Open Interface habilitada y puerto RS-232 de la CCU en el modo correcto, con la
      velocidad anotada.
   2. Conectar con **solo escuchar**: llegan los avisos al prender un micrófono en una unidad. Anotar
      el **byte de escape real** y si los ESC de los datos van duplicados (traza).
   3. Aprender las unidades de toda la sala.
   4. Apagar solo escuchar y probar desde la GUI: abrir, cerrar, máximo de abiertos, «siguiente»,
      reordenar pedidos.
   5. Con la PC de Bosch conectada al mismo tiempo: los cambios de un lado se ven del otro.
   6. Desenchufar el USB en medio de una sesión simulada: la sala sigue y la app se resincroniza al
      volver.
   7. Votación de prueba completa y comparación con el resultado de la PC de Bosch.

---

## 14. Fases y tareas

| # | Tarea | Fase |
|---|---|---|
| D1 | Transporte serie Full (+ TCP) y capa de mensajes, con pruebas unitarias | 1 |
| D2 | Simulador de CCU (pty y TCP) | 1 |
| D3 | Plugin `bosch-dcn`: conexión, resincronización, estado y eventos (`conferencia/microfono`…) | 1 |
| D4 | Mapa unidad↔banca (`dcn_unidad` en el Cuerpo) y «Aprender unidades» | 1 |
| D5 | Herramientas MCP de micrófonos, pedidos, modo y estado; registro de herramientas de plugin con nombre en el núcleo; guardas de aire | 1 |
| D6 | Página «Conferencia» (grilla con estados, listas, conexión, traza) y la fila en Dirección | 1 |
| D7 | Botonera: `imagen`, `borde`, `borde_titilar`, `insignia`, `atenuar`, `al_mantener`, `contiene`, `alternar`, filtros en claves; en web, Satellite y Stream Deck | 1 |
| D8 | Caras: recorte de rostro sobre la miniatura del preset y la foto del Cuerpo; plantilla «Micrófonos de bancas» | 1 |
| D9 | Bancas «al aire» abre el micrófono por la CCU; la cola de la app espejada con la RTS | 1 |
| D10 | Tiempos de palabra con el Orden del día; avisos en la unidad (MD y `*_WARNING`) | 2 |
| D11 | Votación (VT) hacia Quórum, CSV nominal y acta | 2 |
| D12 | Asistencia (AT) hacia el padrón de Quórum | 2 |
| D13 | Delegados del Cuerpo hacia la CCU (DB); pantallas de sala (LD); interpretación (IN, monitoreo); inalámbricas (batería y señal) en Salud | 3 |
| D14 | Asignaciones de cámara opcionales en la CCU (CC) desde los presets de la app | 3 |
| D15 | Protocolo Simple | 3 |

---

## 15. Lo que hay que verificar o decidir

**Resuelto** con el manual Open Interface 2.0 completo, el manual de instalación DCN NG y el DVD de
Bosch del usuario (`Customer DVD`: DDTK con `CCU_DDTK.exe`, `FNID.DEF` y `OIQRC.pdf`; `docs/OI_en.pdf`
4.0): byte de escape, ESC en los datos, `MDSC_COMMUNICATION_PARAMS`, protocolo Simple, velocidad de
fábrica y control de flujo, y el `byData` por red. Todos los códigos de función del plugin coinciden con
`FNID.DEF` (cero diferencias).

**Quedan:**
1. ~~El largo del arreglo de totales~~: son **25** (`VotingResultMarshaler.Unmarshal` del ensamblado
   `Bosch.Dcn.OpenInterface.Logic` de DCN-SWSMV 3.1x crea y lee 25 Int32; `FNID.DEF` está mal). En el
   formato comprimido, el nibble bajo es el delegado impar y el alto el par; 14 = no votó, 15 = sin
   asignar. El plugin igual acepta 24 si el largo del mensaje lo indica.
2. El voto nominal llega por número de **delegado**: sin base de delegados se toma de la asistencia o de
   los pedidos de palabra, y si no, delegado = unidad. Verificarlo en la sala.
3. **Versión de firmware** de la CCU de la sala.
4. **¿En qué modo de micrófonos trabaja la sala?** Para que la botonera decida, tiene que ser modo
   operador (0 o 4).
5. **¿Votación y asistencia van por la CCU o por la app?** Definir cuál es la fuente oficial para el acta.
6. Si hay operador de Bosch con su PC al mismo tiempo, **quién manda los micrófonos**.

---

## Apéndice: funciones sin documentar (del software de Bosch)

Salen de los enums del ensamblado .NET `Bosch.Dcn.OpenInterface.Logic` (DCN-SWSMV 3.1x, en el DVD del
usuario), cruzados por CÓDIGO con `FNID.DEF` y los tres manuales: no aparecen en ninguno. Muchas están
en las clases `…Cpc…` (las que usa el software de control de Bosch), así que puede que la CCU solo las
acepte del controlador principal o con otra licencia: **hay que probarlas en la sala** antes de usarlas.
Los parámetros de cada una no están: habría que sacarlos de su `…Marshaler` en el mismo ensamblado.

| App | Código | Nombre | Tipo | Para qué serviría |
|---|---|---|---|---|
| MM | 0x0047 | MM_C_NEXT_TO_SPEAK_ON_PC | aviso | quién es el próximo en hablar |
| MM | 0x003F | MM_C_MICSOFF_AFTER_SHIFT | pedido | cerrar los micrófonos después de «siguiente» |
| MM | 0x0036 | MM_C_SET_MAX_RTS_LEN | pedido | máximo de pedidos (sin tocar el resto de los ajustes) |
| MM | 0x0037 | MM_C_SET_MIC_SETTINGS | pedido | ajustes de micrófono |
| MM | 0x0038 | MM_C_SET_ATTENTION_TONE | pedido | tono de atención |
| MM | 0x0039 | MM_C_SET_AMBIENT_CTRL | pedido | micrófono ambiente |
| MM | 0x0029, 0x0032, 0x0033 | MM_C_RTS_REPLACE, MM_C_SPK_INSERT, MM_C_SPK_REPLACE | pedido | editar las listas de pedidos y oradores |
| VT | 0x0110–0x0113 | VT_C_START_VOTING_REQ, VOTING_STOPPED, VOTING_ONHOLD, VOTING_RESTARTED | aviso | el presidente maneja la votación desde su unidad |
| VT | 0x0114, 0x0115 | VT_C_PREV_VOTING_REQ, VT_C_NEXT_VOTING_REQ | aviso | el presidente pide la votación anterior o la siguiente |
| VT | 0x0108, 0x0116 | VT_C_SET_ROLL_CALL, VT_C_NEXT_ROLL_CALLER | pedido/aviso | votación por llamado nominal (quién vota ahora) |
| VT | 0x010B, 0x010D–0x010F | VT_C_SET_VOTINGPARAMS2, CHANGEHALLDISPLAY, GET_TIMER_VALUES, GET_RESULTS_ANSWER | pedido | parámetros extendidos, pantallas de sala, reloj, resultados por respuesta |
| VT | 0x011B | VT_C_ATTENTION_TONE_NOTIFY | aviso | tono de atención de la votación |
| AT | 0x0907–0x0909, 0x090C, 0x090D | STORE_APP_ACCESS, GET_SETT_ALLOWED, GET_UNIT_ACCESS_STATUS, SETTINGS_CHANGE_ALLOWED, ACCESS_STATUS_UPDATE | pedido/aviso | control de acceso por unidad |
| SC | 0x1003, 0x1004, 0x1025 | SC_C_PC_CONNECT, SC_C_PC_DISCONNECT, SC_C_RECONNECT | pedido/aviso | sesión del PC con la CCU |
| SC | 0x1021–0x1024, 0x1026 | START/STOP_ENTER_LICENSE, ENTER_LICENSE, SET/GET_AUTHORIZATION_CODE | pedido/aviso | licencias (no usar desde la app) |
| SC | 0x101C, 0x101D, 0x1034, 0x1013 | FPGA_VERSION_INFO, GET_MMP_VERSION_INFO, GET_SLAVE_CCU_IDS, CCU_CHAN_ASSIGN_CHANGE | pedido/aviso | diagnóstico, CCU esclavas |
| SC | 0x1017, 0x1019, 0x101A | SET_UPDATA_CONFIG, SET_LCD_CONV_TABLE, SET_LEAD_BYTE_DATA | pedido | configuración del visor de las unidades |
| SI | 0x1102, 0x1106, 0x1108, 0x1123–0x1125 | SET_CHANNEL, DEF_LANGUAGE, UNIT_SORT_ORDER, GET/SET_INDIVIDUAL_CHANNELS, GET_CHANNEL | pedido | canales de idioma por unidad, idioma de fábrica, orden de unidades |
| IN | 0x024B, 0x024C | IN_C_AUTH_DESK_FEATURES, IN_C_SET_MICRO_ON_OFF | pedido | pupitres de intérprete |
| DB | 0x0308–0x030A | SET_SCREENLINECFG, GET_CCU_LANGUAGES, GET_CONTENT_ID | pedido | base de delegados, idiomas de la CCU |
| IC | 0x0704, 0x070A | IC_CALL_REQUEST, IC_SET_IC_UNIT | pedido | intercomunicador |
| CC | 0x150B | CC_C_STORE_CHARACTERSET | pedido | juego de caracteres del control de cámaras |
| DL | 0x1201–0x120C | START_CCU_SW, START_DOWNLOAD, …, FONT_DOWNLOAD | pedido | **actualizar el firmware de la CCU: no tocar nunca desde la app** |
