# Committee meeting

## What it's for
The committee deals with the files on an agenda and issues committee reports. Members of the Body,
advisors and guests (officials, residents) who present. It is usually public and recorded.

## Questions of its own
- Which committee, how many members, who chairs it? Are there remote members?
- The agenda? (Ask for the text: orden_del_dia_cargar accepts it pasted.)
- Are guests coming to present? How much time each? Do they bring a presentation?
- Is the committee report voted on? Is a quorum needed? A quorum of how many?
- Is it streamed? Are the documents of each file published?

## What to set up
- The agenda loaded (orden_del_dia_cargar) and the documents of each file linked to its item
  (documento_agregar with `puntos`).
- A roll with the members (vota: true) and the guests with their role.
- Scenes: “Chair”, “Member with the floor”, “Guest presenting” (with their presentation), “Vote” if
  there is a vote; a lower third with the name and title of whoever is speaking.
- The floor with times (speakers_config, debate_config if there are rebuttals).
- If there is a vote: quorum (quorum_regla) and voting (votacion_abrir).

## Check
The quorum calculated with the roll; the remote guests tested; the documents on the web page.

## Hand over
The event's web page with the agenda and documents; the invitations; at the end, the minutes
(evento_acta) with what was discussed and what was voted.
