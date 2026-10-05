# DCN-DDI: la interfaz de los micrófonos de banca

La **DCN-DDI (Dual Delegate Interface)** es la cajita que va bajo la banca (debajo de la mesa, en el
apoyabrazos o en el ducto) y que convierte los paneles empotrados (micrófono, botón, parlante, voto)
en «unidades» de la red DCN. Desde la CCU, y por lo tanto desde la app, una DDI se ve igual que
cualquier otra unidad: tiene un `wUnitId` y un tipo.

Esta guía sale del manual de instalación del DVD de Bosch
(`dvd/manuales/instalacion-y-operacion/IUI_DCN_en.pdf`, secciones 3.14, 6.12, 7.1.2, 7.12, 7.13
y 11.2.14; también está en castellano, `IUI_DCN_es.pdf`) y de las definiciones de tipos del Open
Interface 4.0 (`dvd/manuales/open-interface/OI_en-4.0.pdf`, apéndice de constantes). El bus interno
entre la CCU y las unidades no se documenta acá: la app no lo necesita, porque habla con la CCU por
el Open Interface ([open-interface.md](open-interface.md)).

> El equipo está **discontinuado**. Por eso conviene relevar la sala y anotar cómo está configurada
> cada DDI (la planilla del final de esta guía): una DDI usada o reacondicionada se configura con
> los mismos interruptores y entra en lugar de la rota sin tocar nada en la app (ver «Reemplazar una
> DDI»).

## 1. Qué tiene

| N.º | Parte | Para qué |
|---|---|---|
| 1 | Cable DCN | La conecta a la red DCN (troncal de la CCU) |
| 2 | Selector de modo | Uno de los 8 modos de la tabla del §3 |
| 3 | Toma de intercomunicador | Auricular LBB3555/00 |
| 4 | Entradas de voto y control (RJ11) | DCN-FMICB (botón de micrófono), DCN-FPRIOB (prioridad), DCN-FV / DCN-FVCRD (voto, con o sin lector de tarjeta) |
| 5 | Tapa | Da acceso a los controles internos (§4) |
| 6 | 2 entradas de audio (DIN 8 polos 262°) | Micrófonos (normalmente con el panel DCN-FMIC y un DCN-MICL/MICS enchufable) o señal de línea |
| 7 | 2 salidas de audio (plug 3,5 mm) | Parlantes, normalmente DCN-FLSP |
| 8 | Toma DCN | Para seguir la cadena hacia la próxima unidad (loop-through) |

- **Consumo:** 4,5 W, ya incluidos todos los paneles empotrados que se le conectan.
- **Medidas:** 35 × 100 × 170 mm, 500 g.

### Entrada de audio (DIN 8 polos)

| Pin | Señal |
|---|---|
| 1 | Entrada +  |
| 2 | Común del micrófono |
| 3 | Entrada − |
| 4 | LED de micrófono abierto (máx. 2 mA, de 4 (−) a 7 (+)) |
| 5 | LED de pedido de palabra (máx. 7 mA, de 5 (−) a 7 (+)) |
| 6 | Botón de micrófono (pulsador momentáneo de 6 a 7) |
| 7 | +12 V CC (máx. 20 mA) |
| 8 | Control del anillo de LED |

Con esto se puede armar un micrófono de banca propio (cuello de ganso cualquiera, pulsador y dos
LED) si no se consigue un DCN-FMIC: la DDI no se entera.

### Niveles (sección 11.2.14)

| Entrada | Ganancia | Nominal | Máximo |
|---|---|---|---|
| Micrófono | 0 / 6 / 12 / 18 dB | −46 / −40 / −34 / −28 dBV | −16 dBV |
| Línea | 0 / 6 / 12 / 18 dB | −18 / −12 / −6 / 0 dBV | +12 dBV |
| Salida 3,5 mm | — | −5 dBV | +7,5 dBV |

Tolerancia ±3 dB en todos los casos.

## 2. Direccionamiento (sección 7.1.2)

Cada unidad activa necesita una **dirección**, que se la da la CCU. Una DDI sin dirección tiene
**prendido el LED rojo** que está junto al interruptor de desinicialización (adentro, con la tapa
sacada).

### Borrar la dirección

- **De toda la sala:** en el menú de la CCU, ítem **8K De-Initialize**, valor *Yes*. Se prenden
  todos los LED de todas las unidades.
- **De una sola DDI:** apretar el interruptor **de-init** menos de medio segundo, con un clip; nunca
  con algo punzante.

### Darle una dirección

1. Apretar el botón de micrófono de **una** unidad por vez. Cuando la CCU le da la dirección, se le
   apagan los LED.
2. Una DDI también se direcciona con el **botón de voto 3** de un DCN-FV conectado. En modo
   micrófono ambiente (§3), con el DCN-FMICB conectado.

> **No apretar dos botones a la vez:** la CCU direcciona de a una unidad.

**Lo que implica para la app:** el `wUnitId` que ve la app es esa dirección. Si se desinicializa la
sala entera y se vuelve a direccionar en otro orden, **cambian los números**: hay que volver a
asignar las unidades a las bancas (Legislatura → Conferencia → Unidades → «Aprender»). Si se
direcciona en el mismo orden de siempre (por ejemplo, banca 1, 2, 3…), los números quedan iguales y
no hay que tocar nada. Por eso conviene direccionar siempre en orden de bancas.

## 3. Modos (selector de modo, sección 7.12)

| Pos. | Modo | Qué es para la CCU | Tipo en el OI (`byUnitType`) |
|---|---|---|---|
| **0** (fábrica) | Doble delegado | Unidad de **2 delegados**, cada uno con su micrófono y su parlante | `DCNC_UNIT_DUAL_MIC` 0x02 |
| 1 | Presidente | Unidad de presidente (1 persona; admite 2 micrófonos y botón de prioridad DCN-FPRIOB) | `DCNC_UNIT_FLUSH_CHR_NODISPLAY` 0x11 |
| 2 | Doble delegado con un micrófono | 2 delegados que **comparten** un micrófono; cada uno lo abre con su botón | `DCNC_UNIT_DUAL_MIC` 0x02 |
| 3 | Doble delegado con parlantes silenciados | Igual que el 0, pero se callan **los dos** parlantes cuando cualquiera de los micrófonos está abierto | `DCNC_UNIT_DUAL_MIC` 0x02 |
| 4 | Delegado simple | Unidad de 1 delegado; un DCN-FMICB extra hace de botón auxiliar (evento programable por el OI) | `DCNC_UNIT_FLUSH_DEL_NODISPLAY` 0x01 |
| 5 | Unidad de entrada | Registra la asistencia en la puerta del recinto (con lector DCN-FVCRD) | `DCNC_UNIT_ENTRANCE` 0x30 |
| 6 | Unidad de salida | Completa la asistencia en la salida | `DCNC_UNIT_EXIT` 0x40 |
| 7 | Micrófono ambiente | La entrada se manda a la sala cuando **todos** los demás micrófonos están cerrados; el botón no lo abre ni lo cierra | `DCNC_UNIT_AMBIENT_MIC` 0x50 |

La columna de tipos sale de las constantes del OI 4.0 (tipo + subtipo: delegado 0x00, presidente
0x10; empotrado 0x01, empotrado doble 0x02). La app lee el tipo de cada unidad con
`dcn_unidades` y marca como **presidente** las de tipo 0x1x: nunca les cierra el micrófono desde la
botonera sin confirmación (ver [open-interface.md §8](open-interface.md)).

### Cómo se reparten los micrófonos en el modo doble

- En los modos 0 y 3, al habilitar la entrada 1 se deshabilita la salida 1, y lo mismo con la 2: el
  parlante del que habla se calla (anti-acople). En el modo 3 se callan los dos.
- Para que un parlante quede **siempre activo**, se puentean los puntos de soldadura:
  - **X13**: el parlante izquierdo;
  - **X12**: el derecho.
- En el modo 2 (un micrófono compartido) hay que **cerrar el punto de soldadura del DCN-FMIC** que
  deshabilita el anillo verde del micrófono (sección 7.13). Si no, el anillo muestra mal el estado:
  por ejemplo, queda verde (pedido) mientras el micrófono está abierto en rojo.

> **[A VERIFICAR EN LA SALA] Cómo numera la CCU las dos mitades de una DDI doble.** El manual dice
> que «el sistema ve a la DDI como dos dispositivos separados, pero le da una sola dirección». El
> Open Interface no explica cómo se distinguen las dos mitades en `MM_C_MICRO_ON_OFF` y en los
> pedidos. Hay dos posibilidades:
>
> - **Dos `wUnitId` distintos** (uno por mitad). Es lo que esperamos, porque el OI trata a
>   `DCNC_UNIT_DUAL_MIC` como «unidad instalable» de un asiento. La app ya funciona así: cada
>   número va a una banca.
> - **Un `wUnitId` para las dos.** Entonces las dos bancas de esa DDI compartirían micrófono en la
>   app.
>
> **Prueba:**
> 1. En una DDI en modo 0, «Aprender» la banca izquierda y apretar su botón.
> 2. «Aprender» la banca derecha y apretar el otro botón.
> 3. Si `dcn_unidades` muestra dos números, está todo bien.
> 4. Con `dcn_traza {"activa": true}` se ven las tramas: guardarlas y anotarlas en esta guía.

## 4. Controles internos (con la tapa sacada)

| N.º | Control | Valores |
|---|---|---|
| 1 | Interruptor **de-init** | Borra la dirección (§2). El LED rojo de al lado indica «sin dirección» |
| 2 | Potenciómetro de entrada | Ajuste fino de sensibilidad, ±3 dB |
| 3 | Tipo de entrada | **Arriba:** balanceada con fantasma (fábrica). **Centro:** balanceada sin fantasma. **Abajo:** desbalanceada |
| 4 | Sensibilidad de entrada | 0 / 6 / 12 / 18 dB |
| 5 | Nivel de señal | **Izquierda:** línea. **Derecha:** micrófono (fábrica) |

Los controles 2 a 5 están repetidos para cada una de las dos entradas.

## 5. Paneles que se le conectan

| Panel | Va en | Para qué |
|---|---|---|
| DCN-FMIC | Entrada de audio (cable CT 6 polos ↔ DIN 8 polos, viene con el panel) | Toma para el micrófono enchufable DCN-MICL (largo) / DCN-MICS (corto). Tiene una salida de nivel para un DCN-FCS (evita acople con los auriculares) |
| DCN-FMICB | Entrada de voto/control (RJ11) | Botón de micrófono con anillo de LED |
| DCN-FPRIOB | Entrada de voto/control | Botón de prioridad del presidente: cierra a todos y abre el suyo. Solo en modo 1 |
| DCN-FV / DCN-FVCRD | Entrada de voto/control | Botonera de voto; la CRD trae lector de tarjeta. El botón 3 sirve también para direccionar |
| DCN-FLSP | Salida de audio 3,5 mm | Parlante de banca |
| LBB3555/00 | Toma de intercomunicador | Auricular de intercom (presidente u operador) |

## 6. Cómo queda en la app

- **Cada mitad de una DDI doble es una banca.** En Legislatura → Conferencia → Unidades se asigna
  como cualquier unidad, con «Aprender» o a mano (`dcn_unidad_asignar`). Ver el aviso del §3.
- **Presidente (modo 1):** aparece como unidad de presidente. La botonera la protege y `dcn_cerrar_todos`
  la deja abierta salvo que se le pase `incluir_presidente`.
- **Entrada/salida (modos 5 y 6):** no tienen micrófono, así que no se asignan a bancas. Alimentan
  la asistencia (`dcn_asistencia`) cuando la asistencia es por entrada/salida
  (`bySeatAttend = AT_C_ENTRANCE_EXIT`).
- **Micrófono ambiente (modo 7):** no se asigna a ninguna banca. La CCU lo abre sola cuando todo está
  cerrado: si en la mezcla se escucha «la sala» entre orador y orador, es esto.
- **Modo 3 o puentes X12/X13:** no cambian nada en la app; solo cambian lo que se escucha en la sala.

## 7. Relevar la sala (planilla)

Por cada DDI, con la tapa sacada, anotar en una planilla:

| Banca(s) | `wUnitId` (izq./der.) | Modo | Entrada 1: tipo/nivel/sens. | Entrada 2: tipo/nivel/sens. | X12/X13 | Paneles conectados | Observaciones |
|---|---|---|---|---|---|---|---|
| 1 y 2 | 1 / 2 | 0 | arriba / mic / 12 dB | arriba / mic / 12 dB | abiertos | FMIC×2, FMICB×2, FLSP×2, FV×2 | |

La columna `wUnitId` se completa sola desde la app: Legislatura → Conferencia → Unidades muestra
cada unidad con su tipo y su banca, y `dcn_unidades` la devuelve por MCP.

## 8. Reemplazar una DDI

1. Configurar la de repuesto **igual** que la de la planilla: modo, los tres conmutadores y el
   potenciómetro de cada entrada, y los puntos X12/X13 si estaban puenteados.
2. Conectarla en lugar de la rota. Si la de repuesto venía de otra sala, apretar su **de-init**: un
   LED rojo prendido significa que no tiene dirección.
3. Apretar el botón de micrófono de su banca: la CCU le da una dirección. Si es la misma que tenía
   la rota, no hay nada más que hacer. Si es otra, se ve en Unidades como «unidad sin banca»: hay
   que asignarla a la banca con «Aprender».

El Open Interface también sirve para hacer este reemplazo con la sesión andando. Es el ejemplo D.1.2
del manual, «Replacing defective units during operation», con `SI_C_SELECT_UNIT` y la asignación de
asiento. Todavía no está en la app: queda para la fase 3 de [open-interface.md](open-interface.md).

**Repuestos compatibles:**
- cualquier DCN-DDI (es el mismo modelo en todas las versiones de software de la CCU);
- los paneles DCN-F* de la línea empotrada;
- micrófonos con el conexionado del §1, si no se consigue el DCN-FMIC.
