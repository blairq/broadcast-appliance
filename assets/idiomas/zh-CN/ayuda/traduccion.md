翻译功能读取一个**文本输出**（某个“语音转文字”卡片的输出：«Orador»），并发布另一个内容相同、换成另一种语言的输出（«Orador · en»）。它在本机运行，不需要网络，使用 OPUS-MT 模型（每句 0.1 到 0.6 秒）。从活动的语言翻译（`traducciones` 会说明源语言和可选的目标语言）。以目前的语言包，西班牙语 <-> 英语和西班牙语 <-> 葡萄牙语是直接翻译；其他语言都经由英语中转。

## 做法：英文字幕，以及在音轨 2 上的英文语音
1. 发言人的语音转文字（help {"topic": "audio_rack"}）：一个 «Orador» 输出，连续模式。
2. `traduccion_agregar {"desde": "Orador", "a": "en"}`。如果返回 `faltan`，就 `traduccion_bajar {"a": "en"}` 并等待（modelo_local_estado 显示进度）。转写的片段会累积到一句话结束（或 0.6 秒没有新文字：traduccion_ajustar 中的 `espera_ms`）才翻译；如果发言人一直不停顿地说下去，到 `maximo_ms`（8 秒）时翻译已有的内容。它在来源的音频机架中显示为一个“翻译”卡片（在其语音转文字旁边；不需要连线）；操作员在那里查看和调整。
2b. 字幕：在场景中放一个 **«Subtítulos»** 框（`crear_fuente` type `ba_subtitulos`，settings `{"flujo": "Orador · en"}`；放在底部、横跨画面，例如 layout {x: 0.08, y: 0.78, w: 0.84, h: 0.16}）。它会断行（`caracteres`，42），同时显示 `lineas`（2）行，每块显示足够阅读的时间（`cps`，每秒 17 个字符；`minimo` 1.5 秒），每行后面带背景，并自动消失（`borrar`）。要同时显示原文和译文，放两个框。不说话也能测试：`texto_publicar {"flujo": "Orador", "texto": "..."}`。
3. `texto_a_voz {"fuente": "Traduccion", "voz": "csukuangfj/vits-piper-en_US-amy-medium", "lee": "Orador · en"}`，然后在矩阵中把该来源送到音轨 2。
4. `traducciones` 显示最近翻译的一句及其耗时。
5. 多位发言人同时说话，或文字来得慢：转写可以用 Nemotron（流式）代替 Parakeet：`voz_reconocedores {"consumidor": "transcripcion", "motor": "nemotron"}`（help {"topic": "agentes"}）。

## 术语表（绝不能译错的内容）
翻译之前，每个已知术语都会被替换为现成的译文。没有它，«cuarto intermedio»（休会）会被译成 “middle room”，«no es moco de pavo»（不是小事）会被译成 “turkey mucus”。自带三组：«general»（导播间）、«asamblea»（会议、动议、当场审议、党团、委员会）和 «localidades»（地名）。**活动开始前，把活动的术语加进去**：委员会名称、项目、有名称的法律、职务、机构。
- `glosario_ver {"buscar"?}`：现有内容，以及 `archivo`：操作员的 JSON（Datos/Config/glosario-traduccion.json）。可以直接编辑：每个术语为 `{"es": ["forma", "otra forma"], "en": "...", "pt": "...", "fr": "...", "de": "...", "ru": "...", "zh": "...", "grupo": "..."}`；按第一种形式覆盖默认的术语。
- `glosario_agregar {"es": [...], "en": "...", ...}`、`glosario_quitar {"es"}` 或 `{"grupo", "activo": false}`。
- 开始播出前用 `traducir {"texto", "a"}` 测试。
- 规则：`es` 中写多种形式（单数、复数、阴性）；译文要能放进句子里（如果术语是 «solicito pasar a cuarto intermedio»，译文就应是整句话）；俄语和中文的术语是在经由英语中转时应用的，所以要注意英文形式（用常见词：“parliamentary group”，而不是 “caucus”）。
- 模型会把不认识的内容逐词直译：新的习语、可能被当作普通词读的专有名词（Esquel、Gaiman……）要加入术语表，并写上每种语言的形式。
