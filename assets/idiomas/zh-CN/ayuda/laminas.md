由一个场景显示的一组图片。**它不是屏幕用的 `carrusel_*`**：那个是在副屏电视上轮播消息和二维码。这个是发言人的演示文稿。

## 它真正的用处
讲到一半，发言人说“回到柱状图那张”。二十张幻灯片加一个“下一张”按钮，就是二十次点击和十秒钟的冷场。`laminas_ir {"carrusel", "numero"}` 一步就跳过去。

## 用法
- `carruseles_listar`：有哪些。从这里开始；其余的都需要名称。
- `carrusel_laminas_ver {"carrusel"}`：按顺序列出幻灯片，带编号、标题和标签。**编号从 1 开始**，就是 `laminas_ir` 要的那个。
- `laminas_armar {"nombre", "imagenes"}`：用零散的图片（标志、背景）手工组一个。如果已存在，追加到末尾。
- `laminas_editar {"carrusel", "accion"}`：删除一张幻灯片、调整位置、重命名、删除，或把它从场景中解除。`borrar` 删除的是轮播，**不是**图片。
- `laminas_aplicar {"carrusel", "escena"}`：**这几乎总是被漏掉的那一步**。轮播没有放**进**场景之前，`laminas_ir` 只会移动指针，画面上什么也看不到。
- `laminas_desarmar {"archivo", "nombre"}`：把 PDF 或 .pptx 转成幻灯片。**比较慢**（每页几秒），而且不会等待：它会告诉你已经开始，完成后会出现在 `carruseles_listar` 中。.pptx 通过 DeckCraft 处理（随程序附带）；旧的 .ppt 和 .odp 需先导出为 .pptx 或 PDF。

## 搜索什么、怎么搜
操作员在一个框里输入“柱状图”，就能找到那张幻灯片。前提是有人事先看过这些图片：`laminas_sin_analizar {"carrusel"}` 给出下一张还没有标签的幻灯片，用 `imagen_preguntar` 看它（只有它会看图：主助手**不**接收图片），把看到的内容用 `laminas_etiquetar {"carrusel", "numero", "etiquetas"}` 保存下来。**每张幻灯片只做一次**；即使结果为空也会被标记，以免永远重复查看。

标签是赶时间的人会用来搜索的词：看到了什么、写了什么、讲的是什么。“柱状图 销售额 季度”，而不是一段描述。
