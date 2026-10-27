## La barra de abajo
Todo lo que el asistente le quiere decir al operador sin depender de que mire el chat: aparece abajo de la ventana de la app, con color segun prioridad.
- `operator_message {"text": "...", "priority": "info"|"success"|"warning"|"critical", "title": "...", "ttlSec": 30}`: aviso. critical es rojo, titila y queda hasta que lo cierren.
<!-- «[RESPUESTA DEL OPERADOR]»: no se traduce (es una marca fija que pone o lee el programa). -->
- `operator_ask {"text": "...", "options": ["Si", "No"], "allowText": true, "timeoutSec": 60}`: pregunta. Devuelve la respuesta; si no contesta a tiempo, la pregunta queda y la respuesta llega despues al chat del asistente como `[RESPUESTA DEL OPERADOR]`.
- `operator_messages` (con `history`): lo que hay en la barra y lo ya contestado.

## Chat de los invitados
- `guest_chat_pending`: conversaciones donde un invitado escribio y el operador no contesto (privadas y la general).
<!-- «[OPERADOR]»: no se traduce (es una marca fija que pone o lee el programa). -->
- `guest_reply_suggest {"participant": "Ana", "suggestions": ["...", "..."]}`: muestra el mensaje de Ana con respuestas sugeridas; el operador toca una (o la edita) y se le manda (en privado si ella escribio en privado). Lo que haga llega al chat del asistente (`[OPERADOR] le mando a Ana: ...`).
- `chat_send {"to": "Ana", "text": "..."}`: responder directamente (si el operador te autorizo).
- Disparador: `alert_create {"type": "guest_message"}` (o con `"params": {"privateOnly": true}`).