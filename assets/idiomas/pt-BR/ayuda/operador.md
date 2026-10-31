## A barra de baixo
Tudo o que o assistente quer dizer ao operador sem depender de que ele olhe o chat: aparece embaixo da janela do app, com cor conforme a prioridade.
- `operator_message {"text": "...", "priority": "info"|"success"|"warning"|"critical", "title": "...", "ttlSec": 30}`: aviso. critical é vermelho, pisca e fica até alguém fechar.
<!-- «[RESPUESTA DEL OPERADOR]»: no se traduce (es una marca fija que pone o lee el programa). -->
- `operator_ask {"text": "...", "options": ["Sim", "Não"], "allowText": true, "timeoutSec": 60}`: pergunta. Devolve a resposta; se ele não responder a tempo, a pergunta fica e a resposta chega depois ao chat do assistente como `[RESPUESTA DEL OPERADOR]`.
- `operator_messages` (com `history`): o que há na barra e o que já foi respondido.

## Chat dos convidados
- `guest_chat_pending`: conversas em que um convidado escreveu e o operador não respondeu (privadas e a geral).
<!-- «[OPERADOR]»: no se traduce (es una marca fija que pone o lee el programa). -->
- `guest_reply_suggest {"participant": "Ana", "suggestions": ["...", "..."]}`: mostra a mensagem da Ana com respostas sugeridas; o operador toca numa (ou a edita) e ela é enviada (no privado, se ela escreveu no privado). O que ele fizer chega ao chat do assistente (`[OPERADOR] le mando a Ana: ...`).
- `chat_send {"to": "Ana", "text": "..."}`: responder diretamente (se o operador autorizou você).
- Gatilho: `alert_create {"type": "guest_message"}` (ou com `"params": {"privateOnly": true}`).