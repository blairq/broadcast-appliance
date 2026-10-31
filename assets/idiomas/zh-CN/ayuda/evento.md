## 是什么
**活动**就是将要发生的事情的串联单：**段落**（开场、市长致辞、结束），每个段落里是具体的**提示点**。活动拥有自己的**全部**内容（场景、调音台、媒体、大会、网页）：整个活动文件夹可以带到另一台机器上。音乐和视频提示点的文件是**活动的媒体**（“媒体”页面，`media_list`）；讲稿放在串联单的 `Discursos/` 中。

## 提示点的类型以及各自在 `datos` 中需要什么
| 类型 | `datos` | 现在能执行吗？ |
|---|---|---|
| `escena` | `{escena}` | 能 |
| `orador` | `{participante}` + `duracionSeg > 0` | 能 |
| `musica` | `{archivo, accion, repetir}` —— 活动媒体中的一个文件（`media_list`） | 能 |
| `teleprompter` | `{discurso, arrancar}` —— `Discursos/` 中的文本 | 能 |
| `audio` | `{accion: silenciar\|activar\|volumen, fuente, db}` | 能 |
| `video` | `{archivo, alTerminar}` | 还不能 |
| `pantalla` | `{pantalla, contenido}` | 还不能 |
| `pausa` | 等待 `duracionSeg` 指定的时间 | 能 |
| `espera` | 暂停，直到操作员确认 | 能 |
| `nota` | 什么都不做：只是一条提示 | 能 |

还不能执行的类型照样可以写上：它们计入时间，并告诉操作员该做什么。轮到它们时，系统会记录下来，而不是悄悄失败。

## 时间
每个提示点的 `duracionSeg` 汇总成段落和活动的时长；有了 `inicio`（HH:MM）就能算出每项的预计时间，有了 `fecha`（YYYY-MM-DD）加 `huso`（IANA 名称）就能得出 `cuando`，即以绝对时刻表示的开始时间，方便身处其他时区的人。运行中，重要的是**偏差**：`desvioSeg` 为正 = 我们晚了。暂停的时间不算作延误。

## 活动开始前
- `evento_revisar`：不完整的提示点、不存在的场景、缺失或为空的文件。
- `evento_ensayo_rapido`：一次性走完**整个**串联单，不碰任何东西，并说明每个提示点能否执行。写完一份长串联单后最好运行它。
- **彩排**开关（`evento_correr {"ensayo": true}`）以实时方式走一遍串联单，但不播出。

## 运行中
`evento_correr` 配合 `arrancar`、`siguiente`、`atras`、`ir`、`pausar`、`reanudar`、`parar`。**会播出**（彩排除外），所以由操作员决定。

运行串联单**不等于**直播：直播是指真的有东西输出（推流或录制），由 `status` 中的 `en_vivo` 表示。直播时，程序会在任何有风险的操作之前提醒操作员，`settings_set` 也不会应用需要重启会议或引擎的设置（那会把所有嘉宾踢出去，或在最糟糕的时刻中断输出）。为了测试而运行串联单、并未推流时，什么都不会被阻止。

## 提词器
`teleprompter` 提示点从 `Discursos/` 加载一段文本，让它**就绪**但保持停止。其他一切（撰写讲稿、发送讲稿、在播时控制文本）属于它自己的类别：`help {"topic": "prompter"}`。
