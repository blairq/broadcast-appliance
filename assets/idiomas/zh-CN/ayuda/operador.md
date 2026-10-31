## 底部栏
助手想告诉操作员、又不依赖操作员去看聊天的所有内容：显示在程序窗口底部，按优先级着色。
- `operator_message {"text": "...", "priority": "info"|"success"|"warning"|"critical", "title": "...", "ttlSec": 30}`：通知。critical 为红色、闪烁，并一直保留直到被关闭。
<!-- «[RESPUESTA DEL OPERADOR]»: no se traduce (es una marca fija que pone o lee el programa). -->
- `operator_ask {"text": "...", "options": ["Si", "No"], "allowText": true, "timeoutSec": 60}`：提问。返回回答；如果没有及时回答，问题会保留，回答稍后会以 `[RESPUESTA DEL OPERADOR]` 的形式到达助手的聊天。
- `operator_messages`（加 `history`）：底部栏上有什么，以及已经回答了什么。

## 嘉宾聊天
- `guest_chat_pending`：嘉宾写了消息而操作员还没回复的对话（私聊和公共聊天）。
<!-- «[OPERADOR]»: no se traduce (es una marca fija que pone o lee el programa). -->
- `guest_reply_suggest {"participant": "Ana", "suggestions": ["...", "..."]}`：显示 Ana 的消息和建议回复；操作员点选其中一条（或编辑）后发给她（如果她是私聊，就私下发送）。操作员做的任何事都会到达助手的聊天（`[OPERADOR] le mando a Ana: ...`）。
- `chat_send {"to": "Ana", "text": "..."}`：直接回复（如果操作员授权了你）。
- 触发器：`alert_create {"type": "guest_message"}`（或加 `"params": {"privateOnly": true}`）。
