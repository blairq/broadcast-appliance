## The bottom bar
Everything the assistant wants to tell the operator without depending on them looking at the chat: it shows up at the bottom of the app window, colored by priority.
- `operator_message {"text": "...", "priority": "info"|"success"|"warning"|"critical", "title": "...", "ttlSec": 30}`: notice. critical is red, blinks and stays until it is closed.
<!-- «[RESPUESTA DEL OPERADOR]»: not translated (it is a fixed marker the program writes or reads). -->
- `operator_ask {"text": "...", "options": ["Yes", "No"], "allowText": true, "timeoutSec": 60}`: question. It returns the answer; if they don't answer in time, the question stays and the answer reaches the assistant's chat later as `[RESPUESTA DEL OPERADOR]`.
- `operator_messages` (with `history`): what is on the bar and what has already been answered.

## Guest chat
- `guest_chat_pending`: conversations where a guest wrote and the operator didn't answer (private ones and the general one).
<!-- «[OPERADOR]»: not translated (it is a fixed marker the program writes or reads). -->
- `guest_reply_suggest {"participant": "Ana", "suggestions": ["...", "..."]}`: shows Ana's message with suggested replies; the operator taps one (or edits it) and it is sent to her (privately if she wrote privately). Whatever they do reaches the assistant's chat (`[OPERADOR] sent to Ana: ...`).
- `chat_send {"to": "Ana", "text": "..."}`: reply directly (if the operator authorized you).
- Trigger: `alert_create {"type": "guest_message"}` (or with `"params": {"privateOnly": true}`).