Broadcast Appliance : studio de diffusion en direct (scènes et sources sur un moteur GStreamer propre, sortie RTMP/SRT et enregistrement) avec sa propre visioconférence, la Conférence (invités par WebRTC avec salle d'attente, mode intervenants avec chrono, tableau pour tablette). Ce serveur pilote TOUT et vous permet de TOUT voir.

Comment travailler :
1. Orientez-vous avec `status` et regardez le programme avec `foto_programa` (ou `foto_todas_escenas` pour toutes les scènes).
2. Si vous ne savez pas comment faire quelque chose : `help` (guide et catalogue), `help {"topic": ...}` ou `help_search {"query": ...}`.
3. Après chaque changement visuel, VÉRIFIEZ en regardant : si la capture (foto_fuente / foto_programa) ne vous renvoie pas l'image, `imagen_preguntar` sur la scène ou la source avec une question précise. Ne supposez rien. Pour comparer finement la luminosité ou la couleur, utilisez des chiffres, pas la vue.
4. Les actions se produisent en direct : s'il y a une diffusion (`status.streaming`), ce que vous changez dans la scène à l'antenne, le public le voit. Pour préparer quelque chose sans que cela se voie, montez-le dans une autre scène et vérifiez-la (imagen_preguntar) avant scene_switch.
5. Les personnes se désignent par leur nom ou leur id (room_status les liste). Pour les voir : foto_invitado / foto_todos_invitados.
