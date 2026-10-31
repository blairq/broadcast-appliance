**会场活动**的标准流程：现场观众、发言人、推流和录制，如有要求还包括远程嘉宾和观众提问。按顺序进行；每一步都说明用什么工具。会改变活动或会播出的事情要**先提议再动手**，不要编造数据：没人告诉你的，就去问。

**0. 需要询问的**（在搭建任何东西之前）：活动名称；日期、开始时间和**时区**（如果有人从别处连线）；地点；时长和各部分（段落）及其时间；每部分由谁发言，以及其**姓名和职务**（按画面上显示的样子）；是否有**远程嘉宾**；**观众是否提问和投票**；**在哪里推流**（YouTube、Twitch、Facebook、其他服务器）以及是否录制；等候音乐和结束音乐；要播放的视频；是否需要给出席者**签到**；是否需要提词器或字幕。

**1. 配置**（所有需要设置好的东西）：
| 内容 | 用什么 |
|---|---|
| 活动及其串联单：带时间的段落和提示点（场景、带时间的发言人、视频、音乐、图形、提词器、备注、等待） | `evento_armar {nombre, fecha, inicio, huso, invitados, preguntas, bloques}`；然后 `guion_bloque` / `guion_cue` |
| 场景：每台摄像机、全景、共享屏幕、嘉宾 | `scene_define`、`scene_list`（help escenas） |
| 活动的音乐和视频（由提示点引用） | `media_list`、`media_find`（help medios） |
| 每位发言人的字幕条（姓名和职务写对） | 在图形类提示点中使用图形 `zocalo`；`grafico_listar`、`grafico_probar` |
| 会场麦克风和电平 | `audio_input_add`、`audio_measure`、`audio_loudness`（help audio） |
| 推流目标 | 账号：`account_setup_guide`、`accounts_status`；自有服务器：`stream_destino_agregar` |
| 录制（磁盘和质量） | `record_disco`、`record_ajustes` |
| 活动网页（每个段落一页，自动更新） | `evento_web {accion: generar}`；观众参与时用 `evento_web {accion: sitio, participacion: true}`；在哪里观看直播：`sitio {vivos}` |
| 远程嘉宾（如果有） | `room_link`、`room_pin`；用 `room_set` 设置等候室；谁在：`invitado_chequeo` |
| 观众提问（如果有） | 观众页面（二维码）；用 `preguntas_ver` / `pregunta_moderar` 审核 |
| 出席者和签到（如果有） | `padron_anotar`；在入口用 `padron_puerta` |
| 开头和结尾：测试信号、“即将开始”垫片画面、结束垫片画面、“开场前”和“结束”段落 | `evento_preparar {musica_espera, musica_cierre, bajada, cierre}` |

**2. 检查**（自动的部分）：`evento_revisar`（场景、文件、磁盘、推流目标、无信号的输入）→ `evento_ensayo_rapido`（走完整个串联单）→ `evento_verificar`（此类活动的完整检查清单）。按 `como` 的说明修正每个“缺失”项，再运行 `evento_verificar`，直到什么都不缺。

**3. 请对方检查**：`evento_verificar` 会把只有人亲眼看、亲耳听才能确认的项目留在活动的检查清单里（状态“a_mano”）。**把整份清单给**委托你做活动的人看，请他逐项确认；他每确认一项，就用 `evento_chequeo {item, hecho: true, nota}` 勾选。没有确认的项目一律不要勾选。

**4. 彩排**（逐项测试，不播出）：
- **音乐：** `audio_escuchar {name}` 把它送到操作员的耳机；让他听一下，说音量是否合适。
- **视频：** 用 `scene_preview` 预览视频所在的场景，用 `foto_fuente` 查看；连同其音频。
- **图形：** 用每个字幕条和垫片画面的真实数据运行 `grafico_probar`（看图片；姓名和职务要写对）。
- **网页：** `evento_web {accion: ver}`，再用 `qr_mostrar` 显示二维码，在手机上打开。
- **测试录制：** `record_start`，录一分钟，`record_stop`，然后请对方回放。
- **测试嘉宾**（如果有）：`invitado_prueba`：看得到、听得到，也听得到我们。
- **测试提问**（如果有）：请某人用手机发一个问题，然后审核它。
- **测试推流：** 测试信号在播时，`stream_start` 和 `stream_salud`；请对方在平台上观看（彩条、走动的时间、测试音、是正确的活动）。如果离开始还很久，`stream_stop`。
- **完整走一遍：** 打开彩排运行 `evento_correr`（见活动的帮助），查看场景的实际顺序。

**5. 准备就绪：** `evento_verificar` 没有缺失项且一切都已确认；然后 `evento_preparar {al_aire: true}` 把测试信号放到节目上。告诉对方已经准备好了，以及当天还要做什么。

**6. 当天，从测试信号到结束：**
1. **提前 30 到 15 分钟：** `evento_iniciar`；串联单的“开场前”段落以测试信号开始（`evento_correr`）；`stream_start` 和 `record_start`。确认平台上能看到、能听到（等待提示点会提出这个要求）。
2. **提前 10 到 5 分钟：** `evento_correr` → “即将开始”垫片画面配等候音乐。
3. **到点了：** `evento_correr` 进入第一个段落。
4. **进行中：** 用 `evento_correr` 推进；字幕条要及时；提问要经过审核；关注 `stream_salud` 和 `audio_loudness`。
5. **结束：** “结束”段落会放音乐和结束垫片画面；结束后，`stream_stop`、`record_stop`、`evento_terminar` 和 `evento_informe`（每部分的实际时长，供下次参考）。网页会自动切换到结束状态。
