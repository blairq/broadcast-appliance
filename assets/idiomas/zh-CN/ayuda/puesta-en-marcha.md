适用于不熟悉系统的人，或者 AI 或语音方面有东西没有响应时。**始终从 `ia_estado` 开始**：它会说明缺什么、为什么以及去哪里设置（`pagina` 和 `seccion`，用于 `ui_show_page`）。它与页眉上的“IA”标记是同一个东西。按以下顺序一步一步来，并反复查看 `ia_estado`，直到它显示 `todo_bien`。

1. **助手**（“助手”选项卡）。`assistant_status`：服务商、是否已安装、是否在运行。用 `assistant_config {"provider", "model"}` 选择；缺少时用 `assistant_install`；用 `assistant_run` 启动。使用账号（Claude 或 Gemini，无 API 密钥）时，第一次需要在它的终端中输入 `/login`：这由操作员来做。使用 API 密钥时，把它粘贴到“助手”选项卡（绝不要通过聊天）。如果它在运行却看不到系统，问题在 MCP：设置 › 助手（MCP）。
2. **本地模型**（只有当助手使用“local”服务商，或操作员语音使用本地模型时）。`modelo_local_estado` → `modelo_local_bajar` → `modelo_local_activar`。下载需要时间：先告诉对方它有多大。
3. **操作员的麦克风**（按键听写和对助手“说话”）。`voz_microfono` 列出并选择。如果没有识别器，`voz_ajustes` 会说明要下载什么。测试一下：让操作员按键听写一句话。
4. **操作员的耳机**：从哪里听到回答的语音和监听内容（PFL）。`voz_automaticas {"auriculares": id}`（输出由 `audio_outputs` 列出）。如果有麦克风开着，绝不要用会场的扬声器：会啸叫。
5. **回答的语音**（让助手说话）：`voz_ajustes`（如果有，选择并下载一个简体中文语音），然后用 `voz_decir {"texto": "Hola, ya estoy andando"}` 通过耳机测试。
6. **提词器的语音**（如果要用）：`voz_teleprompter`。
7. **翻译和字幕**（如果需要）：`traducciones`、`traduccion_agregar`；缺少的语言包从 设置 › 翻译 下载。

完成后，用两行话告诉操作员哪些已经可以用了，哪些是有意没有配置的。
