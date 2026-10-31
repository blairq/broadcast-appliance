控制台是一个控件网格，可以在**平板**（通过二维码配对）、**Stream Deck**、**MIDI** 控制器或 **OSC**（通过插件）上使用。每个格子触发一个操作并显示实时状态。一切由程序决定：设备只会说“有人按了 (x, y)”。

## 搭建
1. `botonera_plantillas` 和 `botonera_perfil_crear {desde_plantilla: "director"}`（或 audio、asamblea、prompter）：用现有的场景、来源和席位来生成。一个配置文件就是一个**岗位**，带有它的 `herramientas_permitidas`。
2. `botonera_celdas {perfil, pagina}` 用于查看；`botonera_celda_set` 用于设置或修改一个格子：
   - `tipo`：boton、fader、perilla、medidor、texto、reloj；`estilo`：{texto, icono, color, fondo}；
   - `accion`：{tipo: herramienta, herramienta, args}（args 中推子用 "{{valor}}"，相对旋钮用 "{{delta}}"，切换用 "{{no_activo}}"）、{tipo: pagina, ir_a}、{tipo: secuencia, pasos, espera_ms}、{tipo: receta, id}；`accion_mantener`（0.7 秒）和 `accion_soltar`；
   - `estado`：与总线的绑定 [{tema, ruta, op, valor, efecto, dato}] —— 效果有 fondo、color、texto（“{{v}}”是值）、titilar、valor、nivel、activo。
3. 用 `botonera_presionar` 测试（会经过权限检查；在播类操作在播时需由人批准）。

绑定示例：某个场景的 Tally 灯 `{tema: "aire/tally", ruta: "programa", op: "==", valor: "Camara 2", efecto: "fondo", dato: "#c62828"}`；已载入时显示绿色用 `ruta: "preview"`；录制中 `{tema: "salida/grabacion", ruta: "activo", op: "==", valor: true, efecto: "titilar"}`；响度 `{tema: "audio/loudness", ruta: "i", efecto: "texto", dato: "I {{v}}"}`；静音 `{tema: "audio/nivel/Mic 1", ruta: "mudo", op: "==", valor: true, efecto: "activo"}`，配合操作 `audio_set {name, muted: "{{no_activo}}"}`。主题列表：`eventos_temas`。

## 你**不能**做的（有意为之）
- **给配置文件添加工具**，或从设备上开启编辑：这由人在“控制台”页面上完成。如果某个格子用到配置文件没有的工具，`botonera_celda_set` 会提示：请操作员来做。
- **批准配对**：`botonera_emparejar` 给出二维码的 URL；平板发出请求，由人在“控制台 → 设备”页面批准。

## 硬件（Stream Deck、MIDI、OSC、X-keys……）
插件用 `plugin.superficie.registrar(...)` 注册设备，它就成为又一个设备（`botonera_dispositivos`、`botonera_dispositivo_asignar`）。现成示例：`superficie-streamdeck`、`superficie-midi`、`superficie-osc`（`plugin_desde_ejemplo`）；给监视器或摄像机发 Tally 用 `tally-tsl-umd`。相关库（hidapi、streamdeck、mido、python-rtmidi、python-osc、pyserial）随程序附带。新设备：`dispositivos_listar`，然后基于 `ba_plugin.superficie` 编写插件（`render_tecla` 用 PIL 绘制一个按键）。

## 插件的事件
`@plugin.escuchar("aire/tally")`（或 "salida/#"、"audio/nivel/+"、"alarma/#"）接收程序的主题；插件用 `plugin.evento("canal", datos)` 发布的内容显示为 `plugin/<id>/<canal>`。`eventos_esperar {temas, timeout_s}` 让助手等待某件事。
