文件夹（在便携系统内）：`Media/`（媒体库：标志、垫片画面、背景、音乐、视频）、`Shares/`（嘉宾分享的内容）、`Recordings/`。

## 做法：“找到图片 X，把它作为标志放在场景 4 的上方”
1. `media_find {"query": "X"}`（忽略大小写、重音和连字符）。
2. `media_view {"path": ...}` 确认是那张（看一看）。
3. `scene_list` 找到场景（“场景 4”可能是列表中的第四个，也可能是叫这个名字的场景：有疑问就问）。
4. `medio_poner {"path": ..., "scene": ..., "name": "Logo", "layout": "pip-tr"}`（右上；`pip-tl` 左上；或 `{x,y,w,h}`）。
5. `foto_fuente {"name": escena}`，再用 `source_update`（位置、大小）调整，直到满意为止。

视频和音频：`medio_poner` 加 `loop` 用于循环背景或音乐；`alert_create {"type": "media_end"}` 用于得知何时播完。

## 整理磁盘
`media_carpetas` 告诉你文件是**如何**分布的（目录树，以及每个文件夹里有多少项；空文件夹也会列出，因为东西可以移到那里）。`media_ordenar {"accion": "mover"|"renombrar"|"carpeta"|"borrar"}` 用来重新整理。

**删除并不真删**：会送到 `Papelera/`。一个文件可能还在某个场景中，或者是上一次活动仅剩的那份；撤销只需要把它从文件夹里拿出来。如果目标位置已有同名文件，会加编号而不是覆盖。

系统文件夹（Media、Shares、Recordings）不会被移动或删除。操作员在“媒体”选项卡中能看到这一切，你在这里改的东西会直接显示给他，无需刷新。

## 演示文稿
`.pdf` 或 `.pptx` 不是直接“用”在场景里的：它会被**拆分**成一张张幻灯片，变成一个轮播，这样直播时就能跳到任意一张。见 `help {"topic": "laminas"}`。
