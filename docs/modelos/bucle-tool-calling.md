# El bucle de tool calling: cómo se entrena hoy el modelo reactivo

El modelo local (Qwen3.5-4B + LoRA) contesta cada pedido del operador con una
llamada en el formato nativo de *tool calling* de Qwen3.5 (herramienta y
argumentos), con `falta_dato` cuando el pedido no alcanza, con `derivar` cuando
no es suyo, o con texto cuando es charla. Este documento cuenta **el proceso
completo tal como corre ahora (v6b → v7)**: qué scripts se usan, en qué orden,
qué archivos tocan, cómo se mide, y lo que se aprendió en el camino.

Lo que no se repite acá:

- el método general, las dos placas (FSDP2, prefijo compartido, recálculo por
  capa) y la exportación: [entrenamiento-avanzado.md](entrenamiento-avanzado.md);
- la geometría (CDNV) y la metodología de curar con datos apartados:
  [bucle-corpus.md](bucle-corpus.md);
- STE-ES, el castellano controlado de las frases: [../ste-es.md](../ste-es.md).

## Antes de nada: el perfil

**Todo se corre con `BA_PERFIL=vivo6`.** El perfil
(`datos/perfiles/vivo6.json`) define qué herramientas ve el modelo, en qué orden
y agrupadas cómo, con qué nombres y descripciones. Sin la variable,
`lib_prompt` usa el perfil viejo «reactivo» (153 herramientas en vez de 108,
otro orden), y **nada falla: solo se entrena o se mide otra cosa**. Pasó el
15/10/2026: `tc-evaluar` daba 0 % con un modelo bueno (recortaba la frase del
usuario con el largo de la otra cabeza) y `tc-preferencias` armó pares con un
prompt que el modelo nunca vio.

Desde entonces hay controles:

| dónde | qué compara | si no coincide |
|---|---|---|
| `tc-bucle.py` | que `BA_PERFIL` esté puesto | no arranca |
| `tc-evaluar.py` | la huella del prompt actual contra `huella.json` del corpus y `huella-prompt.json` del adaptador | frena con «¿falta BA_PERFIL=…?» |
| `tc-preferencias.py` | contra la huella del adaptador | frena |
| `entrenar-rapido.py` | contra `huella.json` de los datos (formato tc) | frena |

La huella del prompt de vivo6 es `51559fd7c42f585f` desde el 16/10/2026
(`lib_tc.huella()`; antes `72dee9f28a580f86`: cambiaron 4 fichas en el código,
ver abajo); el bucle la anota al arrancar.

## Los nombres de las herramientas (normalizados el 16/10/2026)

Hay **un solo nombre** por herramienta, el mismo en la app, en `docs/mcp.json`,
en los datos (anotaciones, banco, examen, semillas, STE-ES, etiquetas) y en lo
que ve y escribe el modelo. Los nombres anteriores siguen andando en la app como
`Tool::antes`: cada herramienta declara los suyos al definirse, y los 27 que se
cambiaron en bloque desde el perfil están en `src/app/mcp/McpRenombres.cpp`
(los carga `McpCore::agregarNombreAnterior` después de registrar todas).

- Para renombrar: elegir el nombre en `renombres` del perfil y correr
  `tools/dev/renombrar-herramientas.py --perfil vivo6 --aplicar`. Reemplaza en
  código, pruebas, ayuda, datos y scripts, y regenera la tabla de la app.
  Después pasar el par a `renombres_anteriores` y dejar `renombres` vacío.
- Los archivos sin versionar de `datos/entrenamiento/` (el examen y el corpus de
  solo nombres) el script no los ve: renombrarlos en el lugar, sin regenerar
  el examen.
- `renombres_anteriores` ({viejo: real}) es lo que usa `lib_tc.a_real` para leer
  lo que escriben los modelos entrenados antes.
- `docs/mcp.json` se regenera con `tools/docs/mcp-inventario.py --desde-codigo`
  (sin la app andando); no arrastra los nombres viejos como herramientas aparte.

Al normalizar, el prompt de vivo6 cambió en 4 fichas que el código ya tenía y el
inventario no (las posiciones y presets PTZ y la toma de la banca que trajo
master; `speaker_time` con el signo). Los modelos de antes (v5, v6b, v7) se miden
en un worktree del commit de su corpus: los controles de huella no los dejan
medir con el prompt nuevo.

## El mapa

```
datos/tc-anotaciones.json ──► tc-corpus.py ──► corpus (entrenar/validar/prueba/charla-prueba)
   (plantillas + args)            │                         │
        ▲                         │                         ▼
        │                         │               entrenar-rapido.py  (2 × 3090, FSDP2)
        │                         │                         │
        │                         ▼                         ▼
        │                  estados sintéticos       adaptador LoRA
        │                  (lib_tc.TIPOS)                   │
        │                                                   ▼
        │                                  tc-evaluar.py: prueba · charla · banco · examen
        │                                                   │  (puntaje → ¿nuevo mejor?)
        │                                                   ▼
        │                                  autoexamen: plantillas entrenadas, estados nuevos
        │                                                   │  (fallos)
        │                                                   ▼
        └──── tc-geometria.py (3 filtros) ◄── agente (consignas/tc-arreglar.md)
```

Eso es **una vuelta** de `tools/entrenar/tc-bucle.py`. Corre `--vueltas N`
(3) o hasta dos vueltas seguidas sin mejorar; cada vuelta queda en
`datos/bitacora-tc.jsonl` y el mejor adaptador en `<trabajo>/mejor`.

## Los datos

| archivo | qué es | quién lo escribe |
|---|---|---|
| `datos/tc-anotaciones.json` | por herramienta, las **plantillas** (`«bajale 4 dB a {audio}»`) con sus argumentos o su `falta` | `tc-anotar`, `tc-reasignar`, `tc-parejo`, `tc-contraste` y el agente del bucle; versionado |
| `datos/perfiles/vivo6.json` | el perfil: herramientas, grupos, renombres, descripciones | a mano |
| `datos/ste-es.json` | el diccionario de STE-ES | `ste_es.py armar` |
| `datos/derivaciones-admin.jsonl` | pedidos que hace el modelo grande, con su herramienta real: fuente de `derivar` | la app |
| `~/entrenamientos/tc/charla-v3/` | charla rioplatense con las respuestas del Qwen base | `tc-charla.py` |
| `datos/orientacion.jsonl` | **el banco** (1.423 frases): solo mide | curado |
| `datos/entrenamiento/examen.jsonl` | **el examen** (105 casos): solo mide | curado |
| `datos/bitacora-tc.jsonl` | una línea por vuelta: medidas, geometría, huella del corpus | `tc-bucle` |
| `datos/tc-vecinas-pendientes.json` | las herramientas vecinas que el modelo confunde, con ejemplos | `tc-preferencias` |
| `datos/tc-compuestas-fuera.json` | anotaciones sacadas por pedir dos cosas a la vez | registro |

**Regla que no se rompe: nunca se entrena con el banco ni con el examen**, ni
con nada que se les parezca (ver el filtro de parecidas abajo). Los pares de
contraste salen de la geometría, no del banco.

## Una vuelta, etapa por etapa

### 1. El corpus — `tc-corpus.py`

Cada anotación se **rinde** con estados del aparato sintéticos
(`lib_tc.estado_sintetico`): un estado distinto cada vez, con escenas, audio,
fuentes, personas, gráficos, carruseles, cámaras, recetas, bancas, orden del
día… y la frase rellena con la forma en que el operador nombra cada entidad.
El modelo tiene que **copiar el nombre del estado**, no recordarlo.

Qué arma, en orden:

1. **Anotaciones que entran** (`anotaciones_entrenables`): solo las que validan
   (esquema, tipo de entidad, STE-ES) y sin copias dentro de una herramienta
   (`lib_tc.sin_copias`, 70 % de palabras compartidas pidiendo lo mismo). Las
   ~640 que se descartan son de herramientas fuera del perfil.
2. **Prueba aparte**: un 15 % de las plantillas (por hash de la frase) no se
   entrena; se rinde dos veces para `prueba.jsonl`.
3. **Herramientas**: cada plantilla de entrenamiento, `--k 4` estados.
4. **Nombre ausente** (`ausente`, `--ausentes 1`): la misma plantilla con una
   entidad que **no** está en el estado; la respuesta es `falta_dato` con la
   pregunta de `NO_ESTA` («No encuentro «Martín» en el audio: ¿cuál es?»).
5. **Falta dato** de las anotaciones (frases incompletas), con techo del 10 %.
6. **Derivar** (11 %): las frases ajenas del corpus de nombres, las de
   `derivaciones-admin` y las del **borde** (anotaciones de herramientas que el
   perfil sacó a propósito: lo más parecido a lo propio que no es propio).
7. **Charla** (~31 %) y saludos del oficio: texto, sin llamada.
8. **Fuera lo parecido al banco o al examen** (`filtro_parecidas`): toda fila
   que comparte el 60 % de las palabras con contenido con una frase del banco o
   del examen **con la misma respuesta**.

Deja `entrenar.jsonl`, `validar.jsonl`, `prueba.jsonl`, `charla-prueba.jsonl` y
`huella.json` (cuántas de cada tipo, huella del prompt y de las anotaciones).

Corpus v7: 12.933 ejemplos — herramienta 50 %, falta 7,4 % (de eso 4,1 %
nombres ausentes), derivar 11 %, charla 31 %.

### 2. Entrenar — `entrenar-rapido.py`

```
entrenar-rapido.py --fsdp --recalcular --base Qwen/Qwen3.5-4B --datos <corpus> --salida <adaptador> \
    --epocas 2 --eval-cada 200 --lote 16 --micro 2
```

LoRA r=32 sobre todas las lineales, lr 2e-4 coseno, lote efectivo 16, pérdida
solo sobre la respuesta. Lo que lo hace rápido (detalle en
[entrenamiento-avanzado.md](entrenamiento-avanzado.md#las-dos-placas-como-una-fsdp2-desde-la-v4-de-tool-calling)):

- **prefijo compartido**: las fichas de las 108 herramientas (~11.300 tokens)
  son iguales en todos los ejemplos; se calculan una vez por paso y los
  sufijos (frase + respuesta) van sobre su cache. Contra Unsloth Studio, que
  procesa el prefijo en cada ejemplo, es ~13× menos cómputo;
- **dos placas como una**: torchrun con FSDP2, el prefijo repartido por
  secuencia entre las placas;
- **recálculo por capa** del prefijo en el backward.

Se cuida solo: el proceso que se lanza **supervisa** torchrun; si una placa se
queda sin memoria lo relanza desde el último estado con micro-lotes a la mitad
(en la v6b y la v7 la placa 1 no entra con micro 2: sigue con micro 1, ~13 s
por paso, ~6 h por vuelta). Guarda `estado/estado.pt` cada 10 minutos y en cada
evaluación.

Opciones tomadas de Unsloth, **apagadas por defecto** (verificadas con
`verificar-gradiente.py` en las dos placas y los dos modos, todavía sin medir
memoria y velocidad con el 4B):

| opción | qué hace |
|---|---|
| `--a-ram si` | la entrada de cada capa del prefijo, en RAM hasta su backward (ojo: con DPO se quedó sin memoria ANTES que sin ella; sin investigar) |
| `--entropia-fusionada si` | pérdida y gradiente de la cabeza por tandas, sin guardar logits (`entropia_fusionada.py`); no va con dpo/kto |
| `--conv-triton si` | la convolución corta de la atención lineal con el kernel de fla (arregla un bug de fla 0.5.2 con tensores no contiguos) |
| `--compilar si` | `torch.compile` de normas y MLP de cada capa |

### 3. Medir — `tc-evaluar.py`

Cuatro conjuntos, ninguno entrenado; el bucle corre dos procesos a la vez
(prueba + charla en la placa 0, banco + examen en la 1):

| conjunto | qué mide |
|---|---|
| **prueba** | plantillas apartadas con estados nuevos, generación libre: formato, herramienta, **argumentos exactos**, `falta_dato` cuando corresponde |
| **charla** | NLL de las respuestas del base que no se entrenaron, y cuántas veces contesta charla con una llamada |
| **banco** | elección de herramienta con el nombre restringido a las posibles + derivar: cobertura, precisión, derivado, acierto (muestra de 500) |
| **examen** | lo mismo sobre los 105 casos |

El **puntaje** que decide el mejor (`tc-bucle.puntaje`):
`args_exactos + precision_banco + 0,5·(cobertura + derivado) + 0,5·falta_dato + 0,5·acierto_examen`.

Ojo con «argumentos exactos»: compara el texto libre letra por letra
(«¿Se acepta…?» contra «¿se acepta…?» cuenta como error). No se cambió para
seguir comparando con las vueltas anteriores; `tc-preferencias` ya lo trata
bien (ver abajo).

### 4. Autoexamen, geometría y arreglos

- **Autoexamen**: las plantillas **entrenadas** rendidas con estados nuevos
  (400 casos). Si ahí falla, no aprendió a copiar del estado o el patrón.
- **Geometría** (`tc-geometria.py`): cada frase es un vector dentro del modelo
  (el estado oculto donde se decide el nombre), cada herramienta una nube; la
  CDNV entre nubes dice qué pares se pisan y qué plantillas caen en el grupo de
  otra. Va a la bitácora.
- **Arreglar**: un agente (`agente.py` + `consignas/tc-arreglar.md`) recibe por
  herramienta sus anotaciones, los errores y su vecina; corrige la anotación o
  escribe nuevas. Lo nuevo pasa por el validador y por **tres filtros** de la
  geometría: cae en su grupo (o corrige un error visto), suma variedad, no
  acerca a la vecina.

## Curar el corpus fuera del bucle

| script | para qué |
|---|---|
| `tc-anotar.py` | semillas → plantillas con argumentos (agente + validador) |
| `tc-reasignar.py` | cada anotación a la herramienta que de verdad hace lo que pide (grupos que se pisan) |
| `tc-parejo.py` | cada herramienta entre `--min` y `--max` frases (20–25), todas distintas |
| `tc-contraste.py` | frases que separan a las vecinas que el modelo confunde, sin salir de 18–25 |
| `tc-geometria.py` | el diagnóstico CDNV (`--json`), la poda y los filtros |
| `ste_es.py` | el diccionario STE-ES y el verificador de frases |
| `tc-charla.py` | la charla general (respuestas del Qwen base) |
| `tools/dev/renombrar-herramientas.py` | renombres entre el entrenamiento y la app |

## Preferencias (DPO / KTO) — experimental

Para reforzar un adaptador ya entrenado con los errores **del propio modelo**.

1. **Los pares** — `tc-preferencias.py --modelo <adaptador> --salida <dir>`
   rinde las plantillas entrenadas con estados nuevos y nombres ausentes (las
   mismas anotaciones y filtros que el corpus), el modelo contesta voraz y 4
   veces con temperatura 0,8 (en lote), y cada respuesta se juzga como en
   `tc-evaluar`, con el texto libre comparado sin mayúsculas ni puntuación.
   Deja:
   - `dpo/` (buena del corpus contra cada mala distinta) y `kto/` (marcadas);
   - **anclas**: por cada par que empuja a *preguntar*, uno al revés sobre un
     caso que ya sale bien (la mala es preguntar por un nombre que sí está);
   - `seleccion/prueba.jsonl`: los casos de las plantillas de validar, bien y
     mal, para elegir el checkpoint sin tocar prueba, banco ni examen;
   - `vecinas.json` y `resumen.json` (errores por clase y por herramienta).
2. **Entrenar** — `entrenar-rapido.py --objetivo dpo|kto --adaptador-inicial
   <sft>` (`preferencias.py`: las fórmulas de TRL). La referencia (log p con
   el modelo de partida) se calcula una vez y queda en disco para retomar. En
   DPO cada respuesta hace su backward sola con su peso exacto (la buena y la
   mala juntas no entraban en la placa 1). `--nll-buena α` suma α·NLL por token
   de la buena (DPO+NLL, «RPO»).

Lo que dieron (sobre la v6b vuelta 2):

| | SFT v2 | DPO 1 | DPO 2 |
|---|---|---|---|
| pares | — | 333 | 445 (con anclas), DPO+NLL, lr 1e-5, 1 época |
| prueba: falta dato | 17/24 | 24/24 | 20/24 |
| prueba: herramienta / args | 93,0 / 92,1 | 45,8 / 36,2 | 93,0 / 91,0 |
| banco: acierto (derivado) | **87,2 (91,2)** | 25,6 (10,3) | 78,6 (78,0) |
| examen | **73,3** | 51,4 | 72,4 |

- **DPO 1** aprendió a preguntar todo: la mitad de los pares decía «preguntá en
  vez de llamar» y ninguno lo contrario.
- **DPO 2** (con anclas) ya no pregunta de más, pero el mismo desbalance
  apareció en **derivar**: 66 pares con la mala en `derivar` y ninguno a favor.
- **Ninguno se usa.** Cada clase de error que se empuja necesita su contrapeso
  (anclas también para derivar, y casos de derivar en la selección). La
  ganancia posible parecía chica (2–3 casos de falta dato); se volvió al corpus.

## Exportar a Unsloth Studio

`exportar-unsloth-studio.py --corpus <corpus> --salida <dir>` deja el corpus en
parquet con la columna `messages` (chatml), la config con los mismos
hiperparámetros y un `LEEME.md`. Studio no reparte entre placas ni comparte el
prefijo: es para probar, no para entrenar en serio.

## Recetas

```bash
# lanzar un bucle (deja <trabajo>/bucle.pid y bucle.log)
BA_PERFIL=vivo6 nohup .venv-entrenar/bin/python tools/entrenar/tc-bucle.py \
    --trabajo ~/entrenamientos/tc-v7 --vueltas 3 > ~/entrenamientos/tc-v7/bucle.log 2>&1 &

# pararlo sin perder nada (las placas guardan estado en ~30 s)
tools/entrenar/tc-parar.sh ~/entrenamientos/tc-v7

# retomarlo
BA_PERFIL=vivo6 tools/entrenar/tc-bucle.py --trabajo ~/entrenamientos/tc-v7 --retomar --vueltas 3

# medir un adaptador a mano, igual que el bucle
BA_PERFIL=vivo6 CUDA_VISIBLE_DEVICES=0 .venv-entrenar/bin/python tools/entrenar/tc-evaluar.py \
    --modelo <adaptador> --corpus <corpus> --conjuntos prueba,charla --json a.json &
BA_PERFIL=vivo6 CUDA_VISIBLE_DEVICES=1 .venv-entrenar/bin/python tools/entrenar/tc-evaluar.py \
    --modelo <adaptador> --corpus <corpus> --conjuntos banco,examen --limite-banco 500 --json b.json

# verificar el gradiente de las dos placas después de tocar cache_hibrido/repartido
.venv-entrenar/bin/torchrun --nproc_per_node 2 tools/entrenar/verificar-gradiente.py --pesos enteros
.venv-entrenar/bin/torchrun --nproc_per_node 2 tools/entrenar/verificar-gradiente.py --pesos repartidos

# pares de preferencia con los errores de un modelo
BA_PERFIL=vivo6 .venv-entrenar/bin/python tools/entrenar/tc-preferencias.py \
    --modelo ~/entrenamientos/tc-v6b-pref/sft-base --salida ~/entrenamientos/tc-v6b-pref/pares
```

## Dónde quedan las cosas

```
~/entrenamientos/tc-v7/           el bucle en curso
    bucle.log, bucle.pid
    vuelta-N/{corpus, adaptador, medidas.json, autoexamen*, arreglos*, geometria.json}
    mejor/                        el mejor adaptador + medidas.json + puntaje.json
~/entrenamientos/tc-v6b/mejor     el mejor de la v6b (vuelta 2)
~/entrenamientos/tc-v6b-pref/     DPO: sft-base (copia de la v6b), pares, intento1, dpo2
~/entrenamientos/unsloth-studio/  exportaciones para el Studio
```

## Resultados hasta ahora

| | v5 | v6b v0 | v6b v1 | **v6b v2** |
|---|---|---|---|---|
| banco: acierto | 87,8 | 86,2 | 85,4 | **87,2** |
| banco: cobertura / precisión | 92,7 / 78,1 | 86,2 / 81,1 | 85,4 / 81,0 | **90,2 / 82,9** |
| examen: acierto | 71,4 | 73,3 | 72,4 | **73,3** |
| prueba: herramienta / args | — | 91,1 / 89,3 | 91,0 / 91,5 | **93,0 / 92,1** |
| prueba: falta dato | — | 18/24 | 15/24 | 17/24 |
| charla: llamadas | 0 | 0 | 0 | 0 |
| puntaje | — | 3,335 | 3,280 | **3,378** |

## Lo que se aprendió en la v6b (y cambió para la v7)

1. **El error grande era el nombre ausente.** Con estados nuevos el modelo
   fallaba el 36 % de los pedidos que nombran algo que no está en el estado
   (inventaba una entidad conocida en vez de preguntar) y solo el 3 % de las
   frases incompletas. Eran el 1,4 % del corpus. → `--ausentes 1` (4,1 %).
2. **Un catálogo chico se memoriza.** Con 18 escenas y 10 fuentes el modelo
   no necesita leer el estado. → el doble de nombres (34 escenas, 22 audios,
   20 fuentes, 16 personas, 13 gráficos, 8 carruseles).
3. **Escenas con nombre de concepto confunden hasta a un operador.** «La
   placa» es el cartel de `aire_placa`, «la tanda» la publicidad, «la
   cortina» la música, «el cierre» un momento del evento; con escenas que se
   decían así, «cortá directo al cierre» no se entendía. → Mapa, Móvil y
   Separador en lugar de Placa, Tanda y Cortina; «la escena de cierre». Ninguna
   forma dicha pisa a otra entidad ni a una palabra de las anotaciones
   (revisado con un script).
4. **Dos pedidos en una frase no se entrenan.** «¿Sigue grabando? Pará» pide
   consultar y parar; el modelo contesta una llamada. De 77 anotaciones con dos
   oraciones, 71 eran un pedido con contexto («¿cómo se ve Laura? Traeme su
   imagen») y quedaron; 6 salieron (`datos/tc-compuestas-fuera.json`).
5. **El perfil se pone a mano** y ahora se controla (arriba).
6. **DPO necesita contrapesos** en cada eje que empuja (arriba).

## Pendiente

- Las 28 vecinas de `datos/tc-vecinas-pendientes.json`: las debería encontrar
  el autoexamen; si siguen después de la vuelta 0 de la v7, a mano.
- Medir memoria y velocidad de las opciones de Unsloth con el 4B y prender las
  que ganen; ver por qué `--a-ram` gastó más con DPO.
- «Argumentos exactos» en `tc-evaluar` con el texto libre normalizado (rompe la
  comparación con las vueltas anteriores: decidir cuándo).
- Si se vuelve a DPO: anclas para derivar y casos de derivar en la selección.
