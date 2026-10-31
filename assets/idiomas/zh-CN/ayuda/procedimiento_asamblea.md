**大会会议**（市议会、理事会、董事会、业主委员会）的标准流程：成员就座席位、法定人数、议程、表决，以及（如果有）远程成员。会议**不是一份由提示点组成的串联单**：发言权的变化没有事先排好的顺序，由发言队列掌控。按顺序进行，**先提议再动手**，不要编造数据。每个部分的细节：help asamblea、conferencia 和 sesion。

**0. 需要询问的：** 议事机构和本次会议的名称；日期、时间和**时区**；**成员名单**，含席位、姓名、职务和党团（最好是电子表格）；哪些人**有表决权**；**法定人数规则**和多数规则；**议程**（议题和议案）；是否有**会议中央单元**（席位上的麦克风），还是不用它；是否有**远程成员**；在哪里推流、是否录制；是否有国歌、默哀或其他固定环节。

**1. 配置：**
| 内容 | 用什么 |
|---|---|
| 把活动设为大会，以及它的串联单（只包括有固定顺序的部分：开场前、开场、结束） | `evento_armar {nombre, fecha, inicio, huso, asamblea: true, gente, bloques}` |
| 议事机构：每位成员及其席位、职务和党团（已核验） | `cuerpo_importar`（电子表格）、`integrante_editar`、`integrante_validar` |
| 会场：席位网格 | `recinto_editar {filas, columnas, auto: true}` |
| 每个席位的镜头 | `ptz_posicion_crear` 和 `banca_toma {banca, camara, posicion}`；用 `enfocar {que: "banca N"}` 测试 |
| 席位、全景、远程发言者和辩论的**场景** | `asamblea_escenas_base`（展示方案；`crear: true`） |
| 会议中央单元（如果有） | `dcn_conectar`；每个席位对应其单元（help conferencia） |
| 远程成员（如果有） | `sesion_credencial {banca}`（链接和 PIN）、`sesion_acceso_opciones`；主席控制台：`sesion_consola {accion: crear}` |
| 名册，标明谁有表决权 | `evento_armar {gente: [{…, vota: true}]}` 或 `padron_anotar` |
| 法定人数 | `quorum_regla` |
| 议程 | `orden_del_dia_cargar` |
| 大会图形：成员字幕条、议程议题、法定人数、表决结果、休会 | 会自动关联到已核验的数据（help asamblea，`grafico_listar` 类别 Asamblea） |
| 推流目标和录制 | 与会场活动相同（help procedimiento_salon，第 1 步） |
| 活动网页（显示大会、按党团排列席位的会场、法定人数和表决） | `evento_web {accion: generar}` |
| 开头和结尾：测试信号、等待垫片画面、结束垫片画面 | `evento_preparar` |

**2. 检查：** `asamblea_revisar`（议事机构、会场、预置位、场景、中央单元、虚拟席位、议程）→ `evento_revisar` → `evento_verificar {tipo: asamblea}`。修正每个“缺失”项，反复进行直到什么都不缺。

**3. 请对方检查：** `evento_verificar` 中的“a_mano”清单：除了通用项目（摄像机、声音、图形、网页、录制、测试推流），还有**逐个席位测试过的中央单元**、**每个席位的镜头**、一次**测试表决**、**远程成员的链接和 PIN**（至少测试过一个）、**主席控制台**以及**由谁做会议纪要**。只把对方确认过的用 `evento_chequeo` 勾选。

**4. 彩排：**
- **每个席位：** 准予发言（`banca_al_aire`）并结束发言（`recinto_orador_terminar`）：摄像机动了吗？场景载入了吗？字幕条姓名正确吗？麦克风打开了吗？
- **一位远程成员：** 用其链接和 PIN 进入，准许，准予发言。
- **测试表决：** 用中央单元或虚拟席位（`sesion_votacion`）；正式结果（`votacion_resultado_oficial`）显示在画面上。
- **法定人数：** 用出席者执行 `quorum_presentes`。
- 以及所有活动都要做的：音乐、图形、网页、录制和测试推流（help procedimiento_salon，第 4 步）。

**5. 准备就绪：** `evento_verificar {tipo: asamblea}` 没有缺失项且一切都已确认；`evento_preparar {al_aire: true}` 把测试信号放到节目上。通知对方。

**6. 当天，从测试信号到结束：**
1. **提前 30 到 15 分钟：** `evento_iniciar`；测试信号；`stream_start` 和 `record_start`；在平台上确认。
2. **开始之前：** 带音乐的等待垫片画面；远程成员进入并获准；`quorum_presentes`。
3. **开场：** 达到法定人数后，进入第一个段落（国歌、主席宣布开会）。
4. **会议：** 发言队列（`banca_cola`、`banca_al_aire`、`recinto_orador_terminar`）；正在审议的议题（`orden_del_dia_punto`）；表决及其正式结果；需要时用垫片画面休会。记录会自动写入会议纪要（`sesion_registro`）。
5. **结束：** “结束”段落（结束垫片画面）；`stream_stop`、`record_stop`、`evento_terminar` 和 `evento_informe`。
