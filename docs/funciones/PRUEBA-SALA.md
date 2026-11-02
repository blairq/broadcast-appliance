# Prueba de sala (simulacro antes del evento real)

Antes de un evento con mucha gente, el operador prueba si **la máquina y la red aguantan N
personas**: entran participantes «de prueba» por el **camino real** de los invitados
(`/api/join` + `/ws`, con video y voz), se mide todo y, al terminar, se van todos. Del
evento queda solo el informe.

Página **Sala → Prueba de sala** (columna derecha) o las herramientas MCP `prueba_sala_*`:
las dos usan el mismo modelo (`src/app/ui/PruebaSalaViewModel`).

## Cómo se usa

1. Abrí el evento que vas a probar (el informe va a su lista de chequeo).
2. Elegí cuántos participantes (1 a 64), cuántos minutos y cuántos hablan a la vez.
3. **Revisar la máquina** dice si la guardia deja arrancar (memoria disponible, carga).
4. El **enlace**: la tarjeta dice si la última medición del túnel alcanza para los N y
   ofrece medir ([ENLACE.md](ENLACE.md)). Con hinchada la carga no pasa por el enlace (es
   esta máquina), así que no frena: avisa. El informe final lleva el enlace.
5. **Arrancar la prueba**:
   - con **hinchada** (el probador de carga que viene en el programa, `bin/64bit/hinchada/`):
     esta máquina genera a los N. Cada uno manda un patrón con **su número** grande y una
     voz sintética (frases y respiros); nadie suena por los parlantes. La primera vez,
     hinchada graba su video de prueba (unos minutos) en `Runtime/hinchada/grabado.hgr`;
   - **sin hinchada** (o desde otras máquinas): la tarjeta muestra la URL de la sala, la
     **credencial de prueba** y la línea de hinchada lista para copiar. Sirve cualquier
     generador que entre como un invitado usando la credencial como PIN.
6. Mirá en vivo: cuántos entraron y con video, fallidos, cortes, subida/bajada, CPU de la
   app y del gateway, memoria disponible y cuánto tarda la **HD al dar la palabra** (la
   grilla los muestra en baja; al darle la palabra a uno pasa a alta con el fundido).
7. **Terminar la prueba** (o se termina sola: por tiempo, porque el probador terminó, por
   la guardia o porque la sala la cerró).

## Qué hace con los de prueba

- **Entran directo**, sin lobby (si hay lugar; si no, esperan como cualquiera y nunca sacan a
  nadie). Las personas reales siguen pasando por el lobby.
- Quedan **marcados `prueba`**: el rótulo «prueba» en el lobby, en la lista de adentro y en
  la grilla de personas («nombre · prueba»).
- **Fuera del padrón, la votación y el quórum** (son invitados de la conferencia, nunca
  integrantes), fuera del acta (tiempo de palabra), de las novedades del asistente, de la
  atención automática del lobby y de las alarmas de calidad de red (su red va al informe).
- Si el cupo de la sala es menor que N, **se sube** al empezar y **vuelve** al terminar.

## La credencial de prueba

- La emite el gateway cuando la app abre la prueba (`POST /api/v1/prueba-sala`): 32
  caracteres aleatorios. Vale **90 s** y la app la renueva cada 30 s mientras dura la
  prueba: si el programa se cae, en menos de un minuto y medio el gateway la **revoca solo**,
  echa a los de prueba y apaga la entrada directa.
- No pasa por el tope de intentos de PIN por IP (el probador entra muchas veces desde la
  misma IP); un PIN equivocado sí.
- Al terminar se revoca (`DELETE` o `POST /api/v1/prueba-sala/cerrar`) y los tokens que
  se emitieron con ella dejan de servir.

## «Entrada directa para todos»

Un interruptor **visible**, solo durante la prueba, con advertencia y confirmación: deja
entrar a **cualquiera** con el enlace y el PIN sin pasar por el lobby (para probar con
personas reales además de las generadas). No toca la configuración del lobby: se apaga
**solo** al terminar la prueba, al vencer la credencial o al cerrar el programa.

## La guardia

El 2026-11-01 una prueba de carga agotó la memoria de la Mac y tiró el kernel. Por eso:

| Momento | Regla |
|---|---|
| Para arrancar | memoria disponible ≥ 6000 MB, carga ≤ 8, y que con N (≈ 80 MB cada uno con el probador local, la mitad con uno externo) la memoria quede arriba de 4500 MB. Sin poder medir la memoria, no arranca. |
| Durante (cada 1 s) | se **corta sola** si la memoria disponible baja de 3000 MB, si ba-app pasa de 1200 hilos o si la carga pasa de 40. |

La memoria disponible es la que el sistema da sin presión (macOS: libre + inactiva +
especulativa + purgable; Linux: `MemAvailable`; Windows: `ullAvailPhys`). En Windows no hay
carga (no se usa). Las reglas son puras (`src/app/sala/PruebaSalaReglas`, `test_prueba_sala_reglas`).

## El informe

- **En vivo** (la tarjeta y `prueba_sala_estado`): pedidos, entraron, con video, fallidos,
  cortes (se cayó y la sala le guardó el lugar), tiempo hasta tener video, la HD (p50, p95,
  máx.), la última muestra y los picos.
- **Final**: además la serie (de a 5 s), cada participante y por qué terminó. Se guarda en
  `Eventos/<evento>/Pruebas/prueba-sala-<fecha>.json` y una línea en la **lista de chequeo**
  del evento (ítem `prueba_sala`: hecho si todos tuvieron video, sin cortes y sin la guardia).
  Con hinchada, su propio informe (HTML y JSON) queda en `Pruebas/hinchada-<fecha>/`.
- El ancho de banda es el del gateway (lo que suben los participantes / lo que les baja);
  la CPU, la de los procesos de la app y del gateway (en % de la máquina).

## Si el programa se cierra en el medio

hinchada se corta al cerrar (es un proceso hijo). El gateway revoca la credencial sola al
vencer. Lo que queda (el cupo subido) lo deshace la app al volver a arrancar
(`Runtime/prueba-sala-en-curso.json`).

## MCP

| Herramienta | |
|---|---|
| `prueba_sala_arrancar` | `{participantes, minutos?, hinchada?, hablan?}` (con la guardia) |
| `prueba_sala_parar` | termina y limpia |
| `prueba_sala_entrada_directa` | `{on}` (confirmalo con el operador) |
| `prueba_sala_estado` | en vivo, acceso para el generador externo y la última |
| `prueba_sala_revisar` | `{participantes?, hinchada?}`: ¿la guardia dejaría arrancar? |
| `prueba_sala_informe` | el informe completo de la última |

## API del gateway (127.0.0.1, la usa la app)

| Método | Ruta | |
|---|---|---|
| GET | `/api/v1/prueba-sala` | estado (con la credencial) |
| POST | `/api/v1/prueba-sala` | `{duracionS, directa?}` abre/renueva · `{directa}` el interruptor |
| DELETE / POST | `/api/v1/prueba-sala` · `/api/v1/prueba-sala/cerrar` `{motivo}` | termina: revoca, echa a los de prueba, apaga la entrada directa |

Eventos a la app: `prueba-sala` (estado) y `prueba-sala-hd` (`{id, ms}`: llegó la HD de uno
de prueba). El modelo es `src/gateway/PruebaSala` (`test_prueba_sala`).

## hinchada en el programa

Viaja en `bin/64bit/hinchada/` si se la indica al configurar (`-DBA_HINCHADA_DIR=` o
`make deps-hinchada`, que copia el paquete de `../hinchada/dist/hinchada-<plataforma>`).
Sin ella, la prueba funciona con carga externa. Hace falta un hinchada con `--modo grabado` y
`--hablan` (el paquete de antes no los tiene: `make -C ../hinchada paquete` y otra vez
`make deps-hinchada`; el paso lo revisa). El video grabado (`grabado.hgr`, ~100 MB) es uno
solo para todas las configuraciones: `BroadcastAppliance Datos/Runtime/hinchada/`.
