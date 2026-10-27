# Plan: internacionalización total

Pedido del 2026-10-27: internacionalizar **todo** el sistema. No solo la interfaz: también
los prompts, los ejemplos, las plantillas, los logs, las voces, los tótems, las páginas
web, la documentación y los modelos locales. Hoy no hay nada: ni un `qsTr`, ni un `tr()`;
todo el texto está escrito en español, directo en el código.

## Lo que hay hoy (inventario)

| Superficie | Tamaño aproximado | Quién lo lee |
|---|---|---|
| Interfaz QML | 156 archivos, unos 2.900 textos visibles | operador |
| C++ de la app | unas 7.000 cadenas con texto en español (avisos, errores, ajustes, resultados de herramientas, logs, prompts) | operador, logs, modelo |
| Núcleo, gateway, launcher, motor, servidor web en C | sin Qt: no pueden usar `QTranslator` | logs, invitados (mensajes del gateway) |
| Páginas web (`assets/web/*`) | 16 interfaces, 23 páginas | invitados, público, presidencia, televisores |
| MCP | 479 descripciones de herramientas y parámetros | el modelo |
| Prompts | `agent/Roles.cpp` (roles y tótem), prompts de oficina en `AgentViewModel.cpp`, `agent/prompt-modelo-local.md`, `docs/prompts/` | el modelo |
| Modelos locales entrenados | `datos/entrenamiento/` (unos 9.000 ejemplos, todo en español) | — |
| Voz | reconocimiento (Nemotron / Parakeet), síntesis (Piper `es_AR-daniela`), ritmo del prompter medido en es_AR | público, operador |
| Tótems | `registro/Conversacion.cpp`: frases, correo dictado («arroba», «punto»), DNI argentino | público |
| Plantillas y guías | `datos/escenarios/` (19 guías), `assets/demo/` (generado con `tools/generar/plantillas.py`), `graficos-plantilla` | operador, público |
| Gráficos al aire | textos fijos («En vivo», votación, quórum), formato de la hora y la fecha | público |
| Documentación y empaquetado | README (ya en inglés y español), `docs/manual`, `LEEME.txt`, «Leeme primero» del `.dmg`, notas del release, pantalla de arranque | operador |

## Decisiones de diseño

### 0. N idiomas: sumar uno es sumar archivos, no código

Ningún idioma está escrito en el código, ni siquiera el inglés. Un idioma es una carpeta
de recursos, y la app descubre los idiomas por lo que encuentra:

- `i18n/ba_<idioma>.ts`: interfaz, C++, núcleo, web y descripciones MCP;
- `assets/prompts/<idioma>/`: prompts y ejemplos;
- `assets/idiomas/<idioma>.json`: el perfil del idioma, con
  - su nombre en ese idioma;
  - la dirección (izquierda a derecha o derecha a izquierda);
  - la tipografía;
  - la voz de síntesis y el modelo de reconocimiento;
  - el ritmo del prompter;
  - las reglas del tótem (correo dictado, confirmación, números);
- guías y plantillas en `datos/escenarios/<idioma>/` y en `assets/demo/<idioma>/`.

`if (idioma == "en")` está prohibido. Lo que cambia entre idiomas va en el perfil, y
`test_i18n.py` lo busca en el código.

**Cobertura por partes.** Un idioma puede estar completo en la interfaz y no tener todavía
voz o tótem, por ejemplo. El selector muestra solo lo que está completo para cada uso:

- el idioma del operador necesita la interfaz y los prompts;
- el idioma del evento necesita además la voz, el tótem y los gráficos.

Cuando a un idioma le falta una pieza, se usa la de un respaldo declarado en el perfil (`pt_BR` → `es` → `en`),
y el log lo avisa.

**Escrituras.**
- **Derecha a izquierda** (árabe, hebreo): `LayoutMirroring` en QML, `dir="rtl"` en la
  web, y los gráficos al aire alineados según el perfil.
- **Tipografías:** hoy el bundle trae Noto Sans y Serif latinas. Cada perfil declara la suya.
  Las de CJK, árabe, devanagari… se bajan con el idioma (como los modelos), no van en el
  bundle base.
- **Plurales:** `%n`. Qt ya conoce las reglas de cada idioma (ruso, árabe, polaco…). En la
  web se usa `Intl.PluralRules`.

### 1. Una sola fuente de traducciones: los `.ts` de Qt

- Cada idioma tiene **un** archivo `i18n/ba_<idioma>.ts` con todas las cadenas: interfaz,
  C++, núcleo y web. Se edita con Qt Linguist o se traduce con un modelo.
- La app con Qt usa `lrelease` y saca un `.qm`, que va adentro del binario (sigue siendo
  portable).
- Para lo que no tiene Qt, `tools/i18n/ts-a-json.py` genera los catálogos desde el mismo `.ts`:
  - **Núcleo, gateway y launcher:** el catálogo JSON lo lee `ba::i18n` (nuevo, en `src/core/i18n`).
    La llamada es `ba::i18n::tr("contexto", "texto")`, con la misma firma que
    `QCoreApplication::translate`.
  - **Páginas web:** un JSON por idioma en `/comun/i18n/<idioma>.json`, más `i18n.js` con `t("texto")`.
  - `lupdate` no ve las cadenas de la web ni las de `ba::i18n`. Para eso hay un extractor
    propio, `tools/i18n/extraer.py`, que las suma al mismo `.ts`.

### 2. El texto original sigue siendo el español

`qsTr("Conferencia")` lleva el español rioplatense tal como está escrito hoy: es la
referencia y no se reescribe el código. Cada idioma nuevo, empezando por el inglés, es
un `.ts`.

- Cuando una palabra es ambigua, se aclara con un comentario para quien traduce
  (`//:` en C++, `//:` en QML) o con un desambiguador. Ejemplo: «Salida» como puerta y
  «Salida» de audio.
- Los textos con datos usan `%1`, `%2`; nunca se concatenan pedazos de frase.
- Los plurales usan `%n`.

### 3. Tres idiomas distintos

| Idioma | Qué cubre | Dónde se elige |
|---|---|---|
| **Del operador** | interfaz, avisos, logs, asistente de escena, manual | *Configuración → Sistema → Idioma*. Al cambiarlo se reinicia, como con las configuraciones |
| **Del evento** | gráficos al aire, tótems, voz, web pública y de presidencia, subtítulos, plantillas que se crean | en el evento (`evento.json` → `idioma`), con el asistente de eventos o al crear el evento |
| **Del invitado** | web del invitado y de la sesión remota | el del navegador, con un selector en la página. Si no hay traducción, el del evento |

Por defecto, el del operador sale del sistema operativo la primera vez, y el del evento
copia el del operador.

### 4. Lo que **no** se traduce, y por qué

Los identificadores son API, no texto. Si se tradujeran, un evento armado en español no
se podría abrir con la app en inglés.

- **Nombres de herramientas MCP y de sus parámetros, nunca.** Los modelos entrenados, los
  prompts, la botonera, Companion y los eventos guardados los usan así; si cambian, se rompe
  todo. Se traducen solo las descripciones. `test_i18n.py` compara la lista de nombres
  contra una referencia y falla si una traducción tocó alguno.
- Claves de JSON y de configuración, argumentos de línea de comandos.
- Nombres de carpetas en disco (`Medios/`, `Config/`, `Logs/`…). La interfaz los
  **muestra** traducidos; en disco quedan iguales.
- Código y comentarios del código.

### 5. Logs: en el idioma del operador, con un código fijo

Cada línea del log que se lee para diagnosticar lleva un código estable, por ejemplo
`[pantalla.retardo]`. Las herramientas (`tools/caidas/analizar.py`, los chequeos de
pruebas) buscan **el código**, no el texto. Así un log en inglés o en portugués se
analiza igual.

- Los mensajes que vienen de terceros (GStreamer, Qt, libwebrtc) quedan como llegan.
- Un ajuste oculto, `log.idioma`, permite forzar el log en otro idioma para pedir ayuda.

### 6. Prompts y ejemplos: archivos por idioma, no cadenas sueltas

Los prompts son textos largos y con estructura. Cada uno pasa a un archivo:

- Los prompts van en `assets/prompts/<idioma>/<rol>.md`: roles, tótem, oficina, modelo
  local, con sus ejemplos.
- Las descripciones de las herramientas MCP van al `.ts`, con contexto `mcp`. Una
  herramienta nueva sin traducción hace fallar el test.
- El asistente recibe el prompt del idioma del operador y la instrucción de responder en ese
  idioma. El tótem recibe el prompt del idioma del evento.
- Los ejemplos de los prompts se escriben para cada idioma, no se traducen palabra por
  palabra. El correo dictado, por ejemplo, en inglés es «at» y «dot».

### 7. Voz y tótems por idioma

- **Catálogo de voces por idioma**: reconocimiento (Nemotron o Parakeet multilingüe, o el
  modelo que corresponda), síntesis (una voz de Piper por idioma) y el ritmo del prompter
  medido para cada voz (hoy 3,7 palabras/s fijo para es_AR). El que falta se baja como los
  otros modelos.
- **Tótem:** las reglas propias de cada idioma salen de `Conversacion.cpp` a un perfil por
  idioma. Son el correo dictado, la frase de confirmación y cómo se dicen los números y las
  fechas.
- **Documentos por país:** el lector de códigos (hoy, el DNI argentino) pasa a una tabla
  con un formato por país, más el pasaporte (MRZ), que es internacional. Es otro eje, el
  país del evento, aparte del idioma.

### 8. Formatos

`QLocale` del idioma del operador o del evento para mostrar números, fechas, horas,
días y monedas. Lo que se pasa entre programas (herramientas, archivos, JSON) sigue en
`C` (ver `control/Botonera.cpp:131`).

### 9. Pseudo-idioma para encontrar lo que se escapó

`xx` es un idioma de mentira que transforma cada cadena traducible, por ejemplo
«Conferencia» → «⟦Çöñfërëñçïä·····⟧»: con acentos y un 40 % más largo. Con la app en `xx`:

- lo que se ve sin `⟦ ⟧` es texto que se escapó;
- lo que se corta o se superpone es un diseño que no aguanta un idioma más largo
  (el alemán y el portugués lo son).

Se usa en las capturas de la GUI de cada fase.

### 10. Tests que lo cuidan

En la línea de `ba_falta`: lo que falta frena.

- `test_i18n.py` (en `make test`) falla si:
  - un `text:`, `title:`, `placeholderText:` o `ToolTip.text:` de QML tiene un literal sin `qsTr`;
  - una cadena del `.ts` de un idioma publicado queda `unfinished`;
  - no coinciden los `%1` o `%n` entre el original y la traducción;
  - falta un prompt en `assets/prompts/<idioma>/`;
  - un catálogo web está incompleto;
  - un log de diagnóstico no tiene código.
- Una lista de excepciones con su porqué: nombres propios, unidades, «dB», «PTZ».
- Pruebas en las que la app arranca en `en` y en `xx` sin avisos de QML.

## Fases

Cada fase termina con commit a master, tests verdes y capturas en `en` y en `xx`. Las
fases mecánicas se reparten entre agentes en paralelo por carpeta (contrato de
`agentes-en-paralelo`, integrando de a una).

| Fase | Qué | Cómo |
|---|---|---|
| **I0. Infraestructura** | `qt_add_translations` en CMake, `ba::i18n` en el núcleo, `ts-a-json.py` y `extraer.py`, selector de idioma (con reinicio), `idioma` en el evento, pseudo-idioma `xx`, `test_i18n.py` (al principio solo avisa) | yo, de una |
| **I1. Interfaz QML** | envolver todo con `qsTr`, sacar frases armadas con `+`, plurales con `%n` | agentes por carpeta de `qml/` |
| **I2. C++ de la app** | view models, `SettingsModel`, `AjustesAvanzados`, errores y avisos visibles, resultados de herramientas que lee el operador | agentes por módulo (`ui/`, `emision/`, `audio/`…) |
| **I3. Logs y lo que no tiene Qt** | códigos de log, `ba::i18n` en núcleo, gateway, launcher y motor; `analizar.py` y las pruebas que buscan por código | agentes por programa |
| **I4. Web** | `i18n.js` y catálogos; idioma del navegador o del evento; selector en invitado y sesión | agentes por interfaz |
| **I5. Asistente** | prompts a `assets/prompts/es/`, descripciones MCP al `.ts`, responder en el idioma del operador | yo, porque es delicado |
| **I6. Voz y tótems** | catálogo de voces por idioma, perfiles del tótem, documentos por país, ritmo del prompter | yo, y se prueba con señales y archivos sintéticos (nada por los parlantes) |
| **I7. Contenido del evento** | gráficos y plantillas de gráficos, guías (`datos/escenarios/<idioma>/`), plantillas de demo (`plantillas.py` con idioma), subtítulos y traducción en vivo, con el idioma del evento como origen | agentes |
| **I8. Documentación y empaquetado** | `docs/manual` en inglés, `LEEME.txt` y «Leeme primero» por idioma (va el del sistema, o los dos), notas del release en inglés y español, pantalla de arranque | agentes |
| **I9. Traducción al inglés** | completar `ba_en.ts`, prompts y ejemplos en inglés; revisión con capturas de cada pantalla | yo y agentes; vos revisás lo dudoso |
| **I10. Modelos locales** | corpus de entrenamiento multilingüe (ejemplos escritos para cada idioma, no traducidos a ciegas) y reentrenar; examen por idioma | al final, junto con el reentrenamiento pendiente |
| **I11. Más idiomas** | cada idioma nuevo es una carpeta de recursos (sección 0), sin tocar código; `tools/i18n/idioma-nuevo.py <código>` arma el esqueleto y `test_i18n.py` dice qué falta para cada uso. Para probar la base con N idiomas de verdad, en I9 se suma uno de derecha a izquierda (árabe o hebreo) junto con el inglés | por idioma |

Orden: I0 primero. I1 a I4 son independientes y van en paralelo. I5 a I8 necesitan la
infraestructura, no las fases anteriores. I9 empieza cuando I1 e I2 estén cerradas, para no
traducir dos veces. Recién ahí `test_i18n.py` pasa de avisar a frenar.

## Estado

**I0 hecha (2026-10-27).** Cómo se usa: [`docs/desarrollo/I18N.md`](../desarrollo/I18N.md).
- **Núcleo:** `src/core/i18n/I18n.{hpp,cpp}` (`traducir`, `En(idioma).traducir`, `llenar`,
  plurales, respaldos, pseudo) y el idioma del sistema por plataforma
  (`platform/IdiomaSistema_*`). El idioma se aplica en los tres procesos.
- **Catálogo único:** `.ts` → `tools/i18n/ts-a-json.py` → `data/i18n/`. Lo leen igual el núcleo, la
  interfaz (`arranque/TraductorQt`, sin `.qm` ni `lrelease`) y la web (`/comun/i18n/` y
  `/comun/i18n.js`).
- **Perfiles:** `assets/idiomas/` con es (el original), en y xx (el pseudo-idioma).
- **Herramientas:** `make i18n-actualizar` (lupdate más `tools/i18n/extraer.py` para la web).
- **Selector:** *Configuración → Sistema y datos → Idioma*, guardado en `launcher.json`
  (también por `settings_set`), con el aviso «Reiniciar ahora».
- **Idioma del evento:** en `Evento` y en la página (diálogo «Cuándo»), en `guion_guardar` y
  `evento_armar` (con validación), en `guion_ver` y en el estado de la web del evento.
- **Tests:** `tests/test_i18n.py` y `src/core/tests/test_i18n.cpp`.

Cambios respecto de lo escrito arriba:
- **El selector muestra también los idiomas incompletos**, con el porcentaje traducido
  («English · 40 % traducido»), para poder probarlos mientras se traducen.
- **Quedan para las fases que siguen:**
  - el espejado derecha a izquierda en QML (`LayoutMirroring`): con el primer idioma de
    derecha a izquierda, en I9;
  - una herramienta MCP para reiniciar el programa: hoy el asistente cambia el idioma con
    `settings_set` y reinicia el operador;
  - `tools/i18n/idioma-nuevo.py`, en I11.
- **Excepción registrada en el test (`PENDIENTES`):** la traducción en vivo parte del
  castellano (`PanelTraduccion.qml`), y eso se arregla en I7.

## Riesgos

- **Volumen:** son miles de cadenas. Lo mecánico (I1, I2) es lo largo, pero es lo más fácil
  de repartir. Lo delicado es I5 e I6.
- **Frases armadas con pedazos:** son comunes en C++ y en QML (`"Grabando " + nombre`). Hay
  que reescribirlas con `%1`, porque el orden cambia según el idioma.
- **Modelos chicos y otro idioma:** el modelo de 9B y los entrenados rinden distinto fuera
  del español. Hay que medir con el examen por idioma antes de dar un idioma por bueno.
- **Reconocimiento de voz:** hay que confirmar qué idiomas reconoce bien cada modelo antes
  de prometer un idioma de evento.
- **Diseño:** las pantallas armadas al píxel para el español pueden no aguantar textos más
  largos. Las encuentra el pseudo-idioma.

## Para decidir

- **Idiomas además del inglés:** ¿portugués (Brasil)? ¿Otros?
- **Variante del español:** queda rioplatense, con voseo, como fuente. ¿Hace falta un
  español neutro (con «tú» o «usted») para otros países? Sería un `.ts` más, `es_419`.
