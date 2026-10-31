Пульт управления — это сетка элементов управления на **планшете** (сопряжённом через QR), **Stream Deck**, контроллере **MIDI** или **OSC** (через плагин). Каждая ячейка запускает действие и показывает живое состояние. Всё решает приложение: устройство только сообщает «нажали (x, y)».

## Собрать
1. `botonera_plantillas` и `botonera_perfil_crear {desde_plantilla: "director"}` (или audio, asamblea, prompter): собирается из имеющихся сцен, источников и мест. Профиль — это РАБОЧЕЕ МЕСТО со своими `herramientas_permitidas`.
2. `botonera_celdas {perfil, pagina}` — посмотреть; `botonera_celda_set` — поставить или изменить ячейку:
   - `tipo`: boton, fader, perilla, medidor, texto, reloj; `estilo`: {texto, icono, color, fondo};
   - `accion`: {tipo: herramienta, herramienta, args} (args с "{{valor}}" для фейдеров, "{{delta}}" для относительных ручек, "{{no_activo}}" для переключения), {tipo: pagina, ir_a}, {tipo: secuencia, pasos, espera_ms}, {tipo: receta, id}; `accion_mantener` (0,7 с) и `accion_soltar`;
   - `estado`: привязки к шине [{tema, ruta, op, valor, efecto, dato}] — эффект fondo, color, texto («{{v}}» — это значение), titilar, valor, nivel, activo.
3. Проверьте через `botonera_presionar` (проходит через права доступа; то, что касается эфира, в эфире одобряет человек).

Примеры привязок: tally сцены `{tema: "aire/tally", ruta: "programa", op: "==", valor: "Camara 2", efecto: "fondo", dato: "#c62828"}`; загружена — зелёным с `ruta: "preview"`; идёт запись `{tema: "salida/grabacion", ruta: "activo", op: "==", valor: true, efecto: "titilar"}`; громкость `{tema: "audio/loudness", ruta: "i", efecto: "texto", dato: "I {{v}}"}`; без звука `{tema: "audio/nivel/Mic 1", ruta: "mudo", op: "==", valor: true, efecto: "activo"}` с действием `audio_set {name, muted: "{{no_activo}}"}`. Темы: `eventos_temas`.

## Чего вы НЕ можете (намеренно)
- **Добавлять инструменты в профиль** и включать редактирование с устройства: это делает человек на странице «Пульт управления». Если ячейка использует инструмент, которого нет в профиле, `botonera_celda_set` предупреждает: попросите оператора.
- **Одобрять сопряжение**: `botonera_emparejar` даёт URL для QR; планшет запрашивает, а человек одобряет на странице Пульт управления → Устройства.

## Оборудование (Stream Deck, MIDI, OSC, X-keys…)
Плагин регистрирует устройство через `plugin.superficie.registrar(...)`, и оно становится ещё одним устройством (`botonera_dispositivos`, `botonera_dispositivo_asignar`). Готовые примеры: `superficie-streamdeck`, `superficie-midi`, `superficie-osc` (`plugin_desde_ejemplo`); для tally на мониторы или камеры — `tally-tsl-umd`. Библиотеки (hidapi, streamdeck, mido, python-rtmidi, python-osc, pyserial) идут с программой. Для нового устройства: `dispositivos_listar`, и напишите плагин на основе `ba_plugin.superficie` (`render_tecla` рисует клавишу через PIL).

## События для плагинов
`@plugin.escuchar("aire/tally")` (или "salida/#", "audio/nivel/+", "alarma/#") получает темы приложения; то, что плагин публикует через `plugin.evento("canal", datos)`, появляется как `plugin/<id>/<canal>`. `eventos_esperar {temas, timeout_s}` ждёт чего-то со стороны ассистента.
