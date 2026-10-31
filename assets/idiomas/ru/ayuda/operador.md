## Нижняя панель
Всё, что ассистент хочет сказать оператору, не завися от того, смотрит ли тот в чат: появляется внизу окна приложения, цветом по приоритету.
- `operator_message {"text": "...", "priority": "info"|"success"|"warning"|"critical", "title": "...", "ttlSec": 30}`: уведомление. critical — красное, мигает и остаётся, пока его не закроют.
<!-- «[RESPUESTA DEL OPERADOR]»: no se traduce (es una marca fija que pone o lee el programa). -->
- `operator_ask {"text": "...", "options": ["Да", "Нет"], "allowText": true, "timeoutSec": 60}`: вопрос. Возвращает ответ; если оператор не ответил вовремя, вопрос остаётся, а ответ приходит позже в чат ассистента как `[RESPUESTA DEL OPERADOR]`.
- `operator_messages` (с `history`): что есть на панели и на что уже ответили.

## Чат гостей
- `guest_chat_pending`: переписки, где гость написал, а оператор не ответил (личные и общая).
<!-- «[OPERADOR]»: no se traduce (es una marca fija que pone o lee el programa). -->
- `guest_reply_suggest {"participant": "Ana", "suggestions": ["...", "..."]}`: показывает сообщение Ana с предложенными ответами; оператор нажимает один (или правит его), и он отправляется (лично, если она писала лично). То, что он сделает, приходит в чат ассистента (`[OPERADOR] le mando a Ana: ...`).
- `chat_send {"to": "Ana", "text": "..."}`: ответить напрямую (если оператор вас на это уполномочил).
- Триггер: `alert_create {"type": "guest_message"}` (или с `"params": {"privateOnly": true}`).
