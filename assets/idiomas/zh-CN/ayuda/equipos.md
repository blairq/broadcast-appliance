统一的设备目录，按类型声明了可执行的操作。这是回答“连了什么？”和“对某台设备做 X”的通用途径。

## 做法
1. `equipos_escanear {"detector"?}`（red、local、ndi、usb；不指定则全部）：每台设备都有固定的 `id`（«red:192.168.1.90»、«local:video:<id de la camara>»、«local:audio:<id del microfono>»、«local:salida:<id>»、«usb:046d:0881:…»）、`tipo`、`nombre`、是否 `conectado` 以及它的 `acciones`。`equipos_listar` 给出已找到的设备，不重新搜索。
2. `equipo_ver {"id"}`：关于它所知道的一切，以及每个操作及其参数。
3. `equipo_accion {"id", "accion", "args"}`。

## 有用的操作
- 网络摄像机：`pedir_clave`（安全表单：绝不要通过聊天要密码）、`ficha`（ONVIF + 分析 + 计划）、`funciones_onvif`（哪些轴可以移动以及怎么移动、限位、对焦、home、预置位、辅助功能，以及当前位置）、`onvif`（一个原始操作：`servicio` ptz|media|imaging|device，`cuerpo` 为 Body 的 XML；{perfil}、{config_ptz} 和 {fuente} 会被替换）、`http`（品牌自己的 API：Axis VAPIX `/axis-cgi/com/ptz.cgi?query=limits`、Hikvision ISAPI……）、`agregar`、`activar_ptz`。
- 本机的摄像机：`agregar`（作为视频输入，使用它的名称和首选格式）。麦克风：`agregar`（音频输入）。NDI：`agregar`。

## 设备名称（本机的）
摄像机、采集卡、麦克风、声卡、NDI 发送端和网络摄像机使用操作员在“设备与装置”中起的名字：`equipo_renombrar {"id", "nombre"}`（留空：恢复为系统名称）。名字属于**本机**，不属于活动，并绑定在设备的固定 id 上。视频和音频输入、调音台和输出都这样显示，新建的输入也以此命名。`equipo_configurar {"id", "config": {"nota", "modo"}}`（`modo`：摄像机的首选格式）。本机见过、而现在不在的设备仍以缺席状态留在列表中（`estado` «ausente»）：使用它们的输入会等待它们，并自动恢复；`equipo_olvidar` 删除一台缺席的设备。
- 带 UVC 摄像头的 USB 设备：`explorar_uvc`（标准 PTZ 和品牌的扩展单元：编写插件的第一步）。

## 驱动与隔离
本机的每个驱动或设备提供者（GStreamer 的：WASAPI、Media Foundation、Core Audio、AVFoundation、Pulse、V4L2、DeckLink、插件；以及 Windows 上的每个 ASIO 驱动）都在一个单独的进程中限时测试：即使它卡死或崩溃，程序仍继续运行。连续失败 4 次（设置 `drivers.fallasCuarentena`）就会被**隔离**：不再自动加载，启动时和设备变化时都不会。`drivers_listar` 给出每个驱动的状态和 `motivo`（例如 JackRouter：«no contesta: ¿está corriendo el servidor de JACK?»）。`driver_probar {"id"}` 再测试一次（在排除原因之后）；`drivers_revisar` 重新测试全部。这是**按机器**的：新机器启动时测试全部；熟悉的机器只测试新的（或版本变化了的）。

## 诊断不响应的 PTZ
`funciones_onvif` 说明摄像机声明了什么；`onvif` 加 `<tptz:GetStatus><tptz:ProfileToken>{perfil}</tptz:ProfileToken></tptz:GetStatus>` 说明它在哪个位置；试一次移动（`RelativeMove`、`AbsoluteMove`），再看一次。如果协议接受了而位置没变，用 `http` 试试品牌的 API；如果还不行，问题在摄像机本身（机械故障或其配置中的限位）。
