每个配置都是一个文件夹，里面有它的**全部**内容：场景和设置（Config）、媒体、图形、插件、成员照片、缩略图、活动和录制。**什么都不共享**：在一个配置里改插件或场景不会影响另一个。属于本机（所有配置共用）的只有模型、凭据、日志和缓存。

- `configuraciones_listar`：有哪些配置、当前启用的是哪个、它们的描述、大小和来源；以及程序自带的演示配置。
- `configuracion_crear {"nombre", "desde", "descripcion"}` 新建一个（不会启用）。`desde`：`vacia`（默认）、`actual`（复制正在使用的配置）、另一个配置的名称、`demo:<nombre>`（程序自带的演示）或 `evento:<carpeta>/<inicio|cierre|ultima>`（某次活动所用的配置：它的设置快照加上当前配置的媒体）。录制文件不复制，除非指定 `con_grabaciones`。
- `configuracion_cambiar {"nombre"}` 切换到另一个配置：**程序会重启**并使用那个文件夹（不复制任何东西）。正在进行的一切（推流、录制、会议）都会中断：除非操作员要求，否则绝不要在直播中这样做。
- `configuracion_editar {"nombre", "renombrar_a"|"descripcion"|"borrar": true}`：重命名、添加描述或移到回收站（`Configuraciones/.papelera`）。当前启用的配置不能重命名或删除。

**演示配置**：`demo:Demo parlamentaria` 是一个通用的议会系统，可以直接试用：示例港市议会（15 名成员和主席，姓名均为虚构），包含会场、摄像机、模拟的会议中央单元、大会和辩论场景、控制台和议程。

**便携**：所有路径都以相对于配置的形式保存（`Media/logo.png`，在 pipeline 中也是：`location=Media/x.png`）。整个数据文件夹可以原样复制到另一台机器。用相对路径创建来源时，会相对于正在使用的配置解析。

在界面中：设置 › 系统 › 配置（使用、复制、重命名、删除、从演示创建）。
