# Pruebas: la página

La página **Pruebas** (barra lateral, grupo Sistema; `ui_show_page {"page": "pruebas"}`) junta
todo lo de probar el sistema antes del evento:

| Pestaña | Qué | Detalle |
|---|---|---|
| **Prueba de sala** | el simulacro de un evento con N participantes de prueba; **Generar para otro equipo** | [PRUEBA-SALA.md](PRUEBA-SALA.md) |
| **Capacidad** | cuántos invitados aguanta esta máquina, otra PC o la conexión | [CAPACIDAD.md](CAPACIDAD.md) |
| **Prueba de asamblea** | integrantes simulados por la sesión remota | [PRUEBA-ASAMBLEA.md](PRUEBA-ASAMBLEA.md) |
| **Enlace** | el túnel: cuánto anda y cuánto necesita el evento | [ENLACE.md](ENLACE.md) |
| **Historial** | todas las pruebas de esta máquina: abrir, comparar, exportar | abajo |

La columna derecha de la página Sala y la pestaña Sesión remota de Asamblea tienen un acceso
corto; Configuración → Sistema → Capacidad lleva acá.

## Probar entre dos PC

Para que la máquina medida no comparta la CPU con el generador, o para medir una PC desde
otra, el programa se levanta en las dos. Hay dos formas, y se pueden combinar con varias
PC que generan.

### A) La otra PC prueba a esta (el emparejamiento de Capacidad)

Es la de siempre de [Capacidad](CAPACIDAD.md#un-sistema-remoto), ahora con la prueba de sala:

1. **En A (la que se mide):** Pruebas → Capacidad → «Que otro equipo pruebe este»: hasta
   cuántos invitados y cuántos minutos → **Recibir una prueba…** → **Copiar el código**
   (`BACAP1.…`, red local; o el de internet si hay pinggy).
2. **En B (la que genera):** Pruebas → Capacidad → «Un sistema remoto»: pegá el código; elegí
   **Rampa** (cuántos aguanta) o **Prueba de sala** (N durante unos minutos) y qué se
   **decodifica**; **Probar el otro equipo…**.
3. Se ven en vivo, en las dos, los gráficos de **CPU y red de los dos lados**: en A, su CPU (la
   máquina, la app, el gateway) y su red, y la CPU y la red de B; en B, lo mismo. Si la CPU de
   la que genera llega al 100 %, los números son del generador, no de A.
4. Al terminar, el informe queda en el **historial** de las dos: en B, la prueba (con la ficha
   de A y la serie); en A, «recibida» (su serie y quién generó, con su ficha).

**Varias PC que generan:** cada una pega el mismo código de A (y genera su parte: si A abrió
200, dos PC de 100). A muestra cada generadora con su CPU.

Por dónde viaja: B le pide a A su medida cada segundo (`GET /api/prueba-sala/medida`, con la
credencial de prueba en una cabecera) y en el mismo pedido manda lo suyo (su CPU, la de su
hinchada, su red, lo que decodifica y su ficha: cabecera `X-BA-Generador`). Nada pasa por el
MCP (que solo escucha en 127.0.0.1), ni hay tokens en las URL.

### B) Esta reparte la prueba de sala entre varias (el coordinador de hinchada)

Para la **prueba de sala del evento** (con su informe en el evento y su lista de chequeo):

1. **En A:** Pruebas → Prueba de sala → **Repartir entre varias máquinas** (cuántas otras y si A
   también genera) → **Arrancar la prueba** → **Copiar el código** (`BAGEN1.…`).
2. **En B (y C…):** Pruebas → Prueba de sala → **Generar para otro equipo** → pegá el código →
   **Empezar a generar…**. Sin el programa, la línea de hinchada que muestra A.
3. A muestra cuántas se sumaron y la CPU de cada generadora. Detalle y requisitos (el puerto
   7890 en el firewall de A): [PRUEBA-SALA.md](PRUEBA-SALA.md#con-dos-o-más-pc).

La prueba real entre dos PC conviene con cable: el informe dice si cada placa es Wi-Fi o cable.

## El historial

Cada prueba deja su informe en `<datos>/Pruebas/<fecha>-<máquina>-<tipo>.json` (los de
capacidad de antes siguen en `<datos>/Capacidad/` y se ven igual). Es de la **máquina**: el
evento sigue recibiendo su línea en la lista de chequeo y su copia en `Eventos/<evento>/Pruebas/`.

Cada informe guarda:

- **cuándo** (fecha y hora), el **tipo** (`sala`, `local`, `remoto`, `recibida`, `internet`,
  `asamblea`) y la **versión** del programa;
- los **parámetros**: participantes, minutos, cuántos hablan, qué se decodifica,
  cuántas máquinas generaron;
- la **ficha** de cada máquina: la local y la remota o las generadoras (nombre, sistema, CPU con
  su modelo y núcleos —en Apple Silicon, los de rendimiento y los de eficiencia—, memoria, GPU,
  cada placa de red con su velocidad y si es Wi-Fi o cable). Sin direcciones IP ni MAC;
- los **resultados**: la capacidad con margen y la máxima, lo que limitó;
- las **estadísticas y las series** (de a 5 s): CPU de la máquina, de la app, del gateway y del
  probador, memoria, red que entra y sale, la grilla, el audio, de los dos lados.

**Nada privado**: credenciales, códigos, claves de coordinación, tokens y PIN se sacan antes de
guardar.

En la pestaña: la lista (lo más nuevo primero), **Abrir** (fichas, parámetros y los gráficos de
la serie), elegir dos y **Comparar** (lado a lado), **Exportar** la lista en CSV o la abierta en
JSON. Por MCP: `pruebas_historial` (`{limite?, comparar?: [a, b], csv?}`) y `pruebas_ver`
(`{archivo}`).

Pendiente: el **commit** del programa no viaja en el binario (solo la versión); y de las
generadoras del coordinador de hinchada (forma B) se guarda su CPU, no su ficha.
