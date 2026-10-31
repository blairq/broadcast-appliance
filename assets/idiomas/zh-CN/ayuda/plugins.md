**绝不要因为设备少见、或者现有工具覆盖不到某个需求，就回答“做不到”：去写一个插件。** 插件是一个运行在独立进程中的 Python 程序（即使它崩溃，推流照常进行，它会自动重启），可以给系统加上任何需要的功能。由你来写；操作员不编程。

**位置**：每个插件是 `Datos/Plugins/<id>/` 中的一个文件夹（`plugin_listar` 在 `carpeta` 中给出路径）：`plugin.json`（清单）、`plugin.py`（代码）、`_libs/`（它的 pip 库）、`datos/`（它保存的内容），日志在 `Logs/plugins/<id>.log`。它们位于数据中，而不在程序里：更新后依然保留。示例随程序附带（`plugin_listar` → `ejemplos`）；在界面中，**插件**页面（侧栏）→“从示例新建”。不要手动往那个文件夹里写文件：用 `plugin_escribir`（它会校验 id 和路径）。

典型情况：
- 一台 PTZ“只能用厂商应用控制”的 USB 摄像机（Logitech Rally、Jabra、Huddly、OBSBOT……）：它表现为一个网络摄像头（UVC），PTZ 走标准 UVC 控制、厂商自有的 **UVC 扩展单元**（XU）或 HID。
- 360 / 鱼眼摄像机（Kandao、Insta360、Ricoh）：一张或两张 180° 的图像；需要拆分、展开或拼接，有时还附带数据（有人在哪个方向说话）。
- 协同工作的多台设备（三台会议室摄像机检测谁在说话）：每台设备一个插件发布事件，另一个“导播”插件汇总并控制工作室。
- 任何有自有 API（HTTP、WebSocket、UDP、串口）的设备：控制台、Tally 灯、计数器、传感器。
- 用 Python（numpy、OpenCV、**GStreamer**）处理视频或音频：视频图中的一个卡片。`video_modulos` 里没有的效果、滤镜或视频分析：见下面的“视频”。
- 自动化（“发生 X 时，做 Y”）触发器（`alert_types`）覆盖不到的事：监听工作室的事件并调用工具。
- 素材库中没有的按需动画图形（带逻辑、外部数据或逐帧绘制）：`@plugin.grafico`（`help {"topic": "graficos"}`，示例 `ejemplo-grafico`）。
- 不是阿根廷 DNI 的**身份证件**（驾照、其他国家的证件、带二维码的证件）：`@plugin.documento`（见下文，示例 `documento-aamva`）。签到一体机会在其 `documento` 字段中提供它。背面有 MRZ 区的身份证（ICAO TD1）默认已由 `mrz-td1` 读取；如果插件的 `formatos` 包含它或为空，插件也会收到背面的几行（格式“MRZ”，三行以换行分隔，已校验）。

## 做法
1. **有什么**：`dispositivos_listar`（摄像机和麦克风，带可直接用于 pipeline 的 `gst`；以及带 vid:pid 的 USB/HID/串口设备）。如果不知道怎么跟设备通信：查找协议（如果你有 WebSearch 就用）和/或写一个探索用的插件（见下文）。
2. **从模板开始**：`plugin_listar`（`ejemplos` 及其 `categoria`）→ `plugin_ver {"id": "<ejemplo>"}`，如果它和要做的事相似，就 `plugin_desde_ejemplo {"ejemplo", "id"}` 再修改需要的部分。模板：`ejemplo-video-gst`（用 GStreamer 做视频效果、滤镜或分析）、`ejemplo-automatizacion`（监听工作室并执行操作）、`ejemplo-equipo-red`（通过 TCP 文本协议通信的设备，带模拟器）、`ejemplo-basico`（插件能做的一切：卡片、音频滤镜、输入、面板、控制）。设备类：`ejemplo-ptz-uvc`、`ejemplo-360`、`ejemplo-director`，以及开箱即用的（VISCA、TSL、Stream Deck、MIDI、OSC、Bosch DCN）。
3. **编写**：`plugin_escribir {"id", "manifiesto": {"nombre", "descripcion", "dependencias": [...], "config": {...}}, "archivos": {"plugin.py": "..."}}`。
4. **在不影响在播的情况下测试**：`plugin_probar {"id"}` → 报告（声明的内容、卡片的每帧耗时及其改变了什么、输入的帧率、控制、事件、带 Python 错误的日志）。如果 `ok` 为 false 或有地方不对，修好再测。`controles: [{"nombre", "args"}]` 调用控制；`ptz: true` 让设备短暂移动一下。
5. **启用**：`plugin_activar {"id"}`。它的输入会自动出现（清单中的场景或 «Plugins»），它的 PTZ 成为该输入的 PTZ（`ptz_*` 和控制台照常可用），它的卡片用 `plugin_tarjeta` 放置（或在 `video_pipeline` 中用 `{"tipo": "plugin", "plugin", "tarjeta", "parametros"}`），并用 `video_tarjeta_ajustar {"id": "<id de la tarjeta>", "params": {"parametros": {"fuerza": 0.5}}}` 调整（或直接传参数：`{"fuerza": 0.5}`）。不在播的输入只有在有人查看时才会绘制：要确认的话，截图两次（第一次可能是旧帧）。
6. **实时确认**：`video_estado {"entrada"}`、`imagen_preguntar`、`plugin_registro`、`plugin_control {"id"}`（其媒体的信息）、`plugin_eventos`。
7. **告诉操作员**它做什么、怎么用。之后如果修改代码：`plugin_escribir` + `plugin_reiniciar`。

## SDK（`import ba_plugin as ba`）
```python
import ba_plugin as ba
plugin = ba.Plugin()                     # plugin.config (dict de plugin.json), plugin.datos (carpeta propia)

# ENTRADAS (video y/o audio). a) un pipeline de GStreamer que corre la app (lo más barato, sin copias):
plugin.entrada("Rally", gst="avfvideosrc device-index=1", escena="Sala")          # +gst_audio="osxaudiosrc ..."
# b) cuadros hechos en Python (numpy RGBA (alto, ancho, 4) uint8; audio float32 (n, canales)):
e = plugin.entrada("Pelota", video=True, audio=True)
e.enviar_video(arr, fps=30); e.enviar_audio(muestras, tasa=48000, canales=2)    # en un hilo de @plugin.al_iniciar

# TARJETAS: se cambia el cuadro EN EL LUGAR (memoria compartida); rápido (numpy vectorizado, nada de bucles por píxel)
@plugin.tarjeta("negativo", descripcion="...", parametros={"fuerza": 1.0})
def negativo(cuadro, params):            # cuadro.array() -> (alto, ancho, 4) RGBA del tamaño del lienzo
    a = cuadro.array(); a[..., :3] = 255 - a[..., :3]
@plugin.tarjeta("ganancia", medio="audio", parametros={"db": -6})
def ganancia(bloque, params): bloque.array()[:] *= 10 ** (params["db"] / 20)   # (muestras, canales) float32

# PTZ de una entrada (de este plugin o una que ya exista): velocidades -1..1
@plugin.ptz("Rally")
class Ptz:
    capacidades = {"panTilt": True, "zoom": True, "home": True, "presets": 8}
    def mover(self, pan, tilt, zoom): ...
    def parar(self): ...
    def home(self): ...
    def ir_a_preset(self, n): ...; def guardar_preset(self, n): ...   # también enfocar(v), auto_foco(b)

@plugin.control("modo", descripcion="...", parametros={"modo": "auto|manual"})   # plugin_control
def modo(args): return {"ok": True}

# GRÁFICOS ANIMADOS (help {"topic": "graficos"}): aparecen en Gráficos y Rótulos como plugin-<id>-<nombre>
@plugin.grafico("marcador", ancho=1000, alto=420, parametros=[{"clave": "puntos", "tipo": "datos", "porDefecto": "Sí: 3"}])
def marcador(valores): return {"declarativo": {...}}          # o {"qml": "..."}; o def f(lienzo, ms, valores) con numpy

# PANEL: la interfaz del operador (la dibuja la app; plugin_panel la maneja igual). Los valores se guardan solos.
plugin.panel([
    ba.seccion("Seguimiento"),
    ba.interruptor("seguir", "Seguir al que habla", defecto=True),
    ba.deslizador("suavidad", "Suavidad", 0, 1, defecto=0.5),
    ba.opcion("modo", "Modo", ["auto", "manual"]),
    ba.boton("calibrar", "Calibrar", al_tocar=calibrar),        # también texto(), numero()
    ba.indicador("caras", "Caras vistas"), ba.medidor("confianza", "Confianza", 0, 1),
])
plugin.valores["suavidad"]                                      # lo que puso el operador
@plugin.al_cambiar("modo")
def al_modo(valor): ...
plugin.mostrar("caras", 3)                                      # indicadores en vivo (se puede llamar en cada cuadro)

plugin.evento("hablando", {"angulo": 37, "persona": 2})        # canal de datos (con hora)
@plugin.escuchar("*/hablando")                                  # "kandao-1/hablando", "*/x", "id/*" (los propios también llegan)
def al_hablar(datos, de): ...

plugin.llamar("scene_switch", {"name": "Exterior"})             # CUALQUIER herramienta del sistema (las tuyas)

# DOCUMENTOS DE IDENTIDAD (docs/funciones/DOCUMENTOS.md): la app lee el código de barras de la cámara (zxing) y,
# si no es un tipo de fábrica (dni-ar), te pasa {formato, texto, bytes}. Devolvés los datos normalizados o None
# (no es tuyo). Queda como el tipo «plugin:<id>/licencia» (documento_tipos; se elige en el campo del tótem).
@plugin.documento("licencia", titulo="Licencia de conducir (EE.UU.)", formatos=["PDF417"], pais="US",
                  como_se_dice="tu licencia", ejemplos=[{"formato": "PDF417", "texto": "@\n...ANSI 636000..."}])
def licencia(codigo):                    # codigo["texto"] (str), codigo["bytes"] (bytes), codigo["formato"]
    if "ANSI " not in codigo["texto"]: return None
    return {"numero": "...", "apellido": "...", "nombres": "...", "nacimiento": "1990-03-15",   # fechas en ISO
            "vencimiento": "...", "sexo": "F", "extra": {"estado": "CA"}}   # sin `numero` = no lo leyó
# plugin_probar corre los `ejemplos` (SINTÉTICOS: nunca datos de una persona real) y muestra qué devolvió.
# Los datos son sensibles: no los mandes a ningún servicio de afuera ni los imprimas enteros en el registro.
plugin.aviso("texto", "warning"); plugin.estado("conectado a la Rally"); plugin.secreto("clave")  # claves cifradas
@plugin.al_iniciar / @plugin.al_terminar / @plugin.cada(segundos) / @plugin.al_cambiar_config
plugin.correr()                                                 # al final
```
设备（`from ba_plugin import dispositivos as d`；依赖 `pyusb`、`libusb-package`、`hidapi`、`pyserial`）：
- `d.usb_listar()` → `[{"vid": "046d", "pid": "0881", "fabricante", "producto", "serie", "bus", "direccion", "uvc": true}]`（vid/pid 为十六进制文本）；`d.hid_listar()` → `[{vid, pid, producto, fabricante, uso, ruta}]`；`d.serie_listar()` → `[{puerto, descripcion, vid, pid, serie}]`。
- `uvc = d.UVC.abrir("046d", "0881")`：`uvc.info()`（摄像机终端、带 GUID 的扩展单元、范围）、`uvc.mover(pan, tilt, zoom)` / `uvc.parar()`（标准 UVC）、`uvc.ir_a(pan=, tilt=, zoom=)`，以及厂商自有的：`uvc.unidades()`、`uvc.xu_explorar(unidad)`（哪些选择器有响应）、`uvc.xu_leer(unidad, selector)`、`uvc.xu_escribir(unidad, selector, bytes)`。它不会从系统那里抢走画面：可以边看边移动。
- `h = d.hid_abrir(vid, pid)`：`h.write(bytes)`、`h.read(64, 100)`。

## 视频：视频图中没有的效果、滤镜或分析
先 `video_modulos {"buscar": ...}`：如果 GStreamer 已经有了，用一个 `gst` 卡片就行。如果没有（自有效果、少见的校正、数人数、检测运动、读取记分牌），**就由你把它写成插件卡片**：它就是视频图中的“plugin”卡片（模块 `baplugin`），像其他卡片一样运行在输入的链中，代码运行在插件的进程里（即使它卡死，画面照常通过，推流继续）。

在卡片里可以用 numpy，以及**从 Python 使用完整的 GStreamer**（`from ba_plugin import gst as bagst`；程序自带绑定）：模块链和**用 Python 编写的自有 GStreamer 元素**。最小示例：
```python
import ba_plugin as ba
from ba_plugin import gst as bagst
plugin = ba.Plugin()

@bagst.elemento("baumbral", propiedades={"nivel": 128})     # un GstBase.BaseTransform de verdad
def umbral(a, props):                                          # a: (alto, ancho, 4) RGBA, sin copiar
    gris = a[..., :3].mean(axis=2) > props["nivel"]
    a[..., :3] = (gris * 255).astype("uint8")[..., None]

@plugin.tarjeta("umbral", parametros={"nivel": 128})
def tarjeta(cuadro, params):
    c = bagst.cadena("baumbral name=u ! videobalance contrast=1.2")   # una por tarjeta puesta; se arma una vez
    c.procesar(cuadro)                                                 # el resultado queda en el cuadro
    c.poner("u", nivel=int(params["nivel"]))                           # cambiar en vivo sin rearmar

plugin.correr()
```
不用 GStreamer 也可以：`@plugin.tarjeta` 加上对 `cuadro.array()` 的 numpy 操作。分析**不**改变画面：每隔几帧在 `cuadro.array()[::8, ::8]` 上测量，在其面板中 `plugin.mostrar`，并 `plugin.evento("movimiento", {...})`（带迟滞；绝不要每帧发一个事件）。

完整流程：`plugin_desde_ejemplo {"ejemplo": "ejemplo-video-gst", "id": "mi-efecto"}`（或 `plugin_escribir`）→ `plugin_probar {"id"}`（每个卡片的每帧耗时，在 320x180 下测得：1080p 下约为 30 倍；必须远低于一帧的时长）→ `plugin_activar` → `plugin_tarjeta {"id", "tarjeta", "entrada"}`（在视频图中显示为 «Plugin · <tarjeta>»；在“视频”页面中可以像其他卡片一样查看、移动和调整）→ `video_tarjeta_ajustar {"id": "<id de la tarjeta>", "params": {"nivel": 90}}` → 用 `foto_fuente` 查看。1080p 下的开销：经过一条链约 1 ms，`videobalance` 约 8，`gaussianblur` 约 35（太多了：缩小画面或换一个），uint16 且连续内存的 numpy 1-3 ms（用 float 或对整帧做 `a[..., :3]`，约 20）。

## 做法示例
- **少见的 PTZ**：用摄像机建一个 `gst` 输入（`dispositivos_listar` 给出 `avfvideosrc device-index=N`）+ 一个说 UVC/XU/HID 的 `@plugin.ptz`。先试标准 UVC（`uvc.mover`）；如果不动，探索 XU（做一个“explorar”控制，返回 `uvc.info()` 和 `xu_explorar`），并查找该型号的协议。
- **把一个画面拆成多个输入**（一帧里两个鱼眼、录像机的 2x2 画面）：只用一个输入，在它的图中放 `recorte` + `salida` 卡片（每个 salida 都是一个有名字的来源）。不要把同一台设备打开两次。
- **展开鱼眼 / 拼接 360**：用 OpenCV（`opencv-python-headless`）写插件卡片：映射表只计算**一次**（`cv2.fisheye.initUndistortRectifyMap` 或用自建映射表的 `cv2.remap`），每帧应用 `cv2.remap`。如果太重，就让输入的尺寸小一些。
- **多台相同设备**：每台设备一个插件（用 `plugin_desde_ejemplo` 或 `plugin_escribir` 复制），各带自己的 `config`（序列号、IP）和 `escena`；或者一个插件声明 N 个输入。
- **谁在说话 / 导播**：每个设备插件发布 `plugin.evento("hablando", {...})`（注意：如果有一个正在运行的导播插件在监听 `*/hablando`，它也会收到；测试时用别的通道或其 `config.de`）；导播插件用 `@plugin.escuchar("*/hablando")` 做决定（每个镜头的最短时长，不在一句话中间切），并用 `plugin.llamar(...)`：`scene_switch`、用 `video_tarjeta_ajustar` 调整合成器（图层）、用 `ptz_*` 让另一台摄像机对准。问操作员是要自动的还是要确认的（`operator_ask`）。
- **设备的音频**：用 `gst_audio` 或 `enviar_audio` 建输入；用 `plugin_tarjeta {"fuente"}` 在任何来源上放音频卡片。
- **自动化**（模板 `ejemplo-automatizacion`）：`@plugin.escuchar("aire/escena")`（主题：`eventos_temas`）和 `plugin.llamar(...)`；在 plugin.json 中写 `"herramientas": [...]`，只列出它可以调用的工具。做一个遍历各种情况的“simular”控制，就能用 `plugin_probar {"controles": [...]}` 测试（对系统的调用会出现在报告中，但不会执行）。
- **通过网络通信的设备**（模板 `ejemplo-equipo-red`）：在 `al_iniciar` 的线程中连接，带超时和逐渐加长等待的重连，指令带响应，设备主动发来的内容作为 `plugin.evento`，内部再带一个设备模拟器（`modo: simulador`），没有设备也能测试。

## 规则
- 任何东西都不能永远阻塞：一切都要有超时；循环放在 `al_iniciar` 的线程中，并检查 `plugin.andando`。
- 卡片必须快（一帧的时长是画布的 1000/fps ms：30 fps 时 33 ms，60 fps 时 16 ms；而且与视频串行）：用向量化的 numpy；如果超时，就输出上一帧。如果插件只需要**测量**（颜色、亮度、谁在场），就每隔几帧在缩小版（`a[::8, ::8]`）上测量，并用一个 gst 卡片应用计算结果（对一个 `videobalance` 执行 `plugin.llamar("video_tarjeta_ajustar", ...)`）：在每台摄像机上用 Python 处理整帧，会让机器负载过高、界面卡顿。
- `print` 输出到日志（`plugin_registro`）；需要操作员看到的内容用 `plugin.estado(...)`。
- **凡是操作员需要操作的插件，都要有 `plugin.panel`**（可调的：滑块、开关、选项；可执行的：按钮；正在发生的：指示器和电平表）。人类和你一样使用这个系统：不要搞“通过聊天告诉我”那一套。`@plugin.control` 留给助手带参数调用的操作（它也会显示为按钮）。
- 库写在 `dependencias` 中（pip，下载到插件的文件夹：便携，更新后依然保留）。**numpy 已随程序附带**（声明它没有任何代价）；第一次下载大的库（opencv）可能需要几分钟。
- 设备的密钥（用户名、密码、厂商 API 的 token）：绝不通过聊天，也不放在 `config` 中。在 plugin.json 中声明（`"secretos": [{"clave": "clave", "etiqueta": "Clave de la cámara"}]`），用 `plugin_credenciales {"id"}` 索取（底部栏上的安全表单），在插件中用 `plugin.secreto("clave")`（还没有时为 None：让它等待，并用 `plugin.estado` 说明）。
