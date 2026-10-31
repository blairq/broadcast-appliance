## La barre du bas
Tout ce que l'assistant veut dire à l'opérateur sans dépendre du fait qu'il regarde le chat : cela apparaît en bas de la fenêtre de l'application, avec une couleur selon la priorité.
- `operator_message {"text": "...", "priority": "info"|"success"|"warning"|"critical", "title": "...", "ttlSec": 30}` : message. critical est rouge, clignote et reste jusqu'à ce qu'on le ferme.
<!-- «[RESPUESTA DEL OPERADOR]»: no se traduce (es una marca fija que pone o lee el programa). -->
- `operator_ask {"text": "...", "options": ["Si", "No"], "allowText": true, "timeoutSec": 60}` : question. Renvoie la réponse ; s'il ne répond pas à temps, la question reste et la réponse arrive plus tard dans le chat de l'assistant sous la forme `[RESPUESTA DEL OPERADOR]`.
- `operator_messages` (avec `history`) : ce qu'il y a dans la barre et ce qui a déjà reçu une réponse.

## Chat des invités
- `guest_chat_pending` : conversations où un invité a écrit et l'opérateur n'a pas répondu (privées et la générale).
<!-- «[OPERADOR]»: no se traduce (es una marca fija que pone o lee el programa). -->
- `guest_reply_suggest {"participant": "Ana", "suggestions": ["...", "..."]}` : affiche le message d'Ana avec des réponses suggérées ; l'opérateur en touche une (ou la modifie) et elle lui est envoyée (en privé si elle a écrit en privé). Ce qu'il fait arrive dans le chat de l'assistant (`[OPERADOR] le mando a Ana: ...`).
- `chat_send {"to": "Ana", "text": "..."}` : répondre directement (si l'opérateur vous y a autorisé).
- Déclencheur : `alert_create {"type": "guest_message"}` (ou avec `"params": {"privateOnly": true}`).
