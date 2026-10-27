An assembly session (council, board, directors' meeting, condominium meeting) is not a rundown of cues: the floor changes hands with no prior order and every seat has to be seen and heard when its turn comes. Prepare it in this order; `asamblea_revisar` tells you at every moment what's missing and how to fix it. **Run it at the start and at the end.**

1. **The Body:** each member with their seat (`cuerpo_importar` from a spreadsheet, or `integrante_editar`). Without a seat they don't appear in the chamber.
2. **The chamber:** the grid of seats as seen from the control booth (`recinto_editar {filas, columnas, auto: true}`).
3. **Each seat's shot:** for each seat, a PTZ position that frames it (`ptz_posicion_crear`) linked to the SEAT (`banca_toma {banca, camara, posicion}`; it stays with the seat even if who sits there changes, and an empty seat has one too). The speaker and debate scenes use the “Banca con la palabra” source (the seat that has the floor) (`asamblea_escenas_base {crear: true}`): it shows the shot of whoever is speaking (slot 1; in a debate, slot 2 too). With a digital camera it crops it from the full shot without moving anything (the wide shot stays the same and two seats can be seen at once); a motorized one goes to the position on its own. “Focus on seat 10 / on John Smith / the chair”: `enfocar {que}`.
4. **THE SCENES** (the most often forgotten): when a seat is given the floor, the scene that shows its camera is BUILT; if no scene shows it, nobody goes out. You need:
   - one scene per seat camera (with the `{orador}` lower third if you want the name on screen);
   - a wide shot;
   - one with “Remoto con la palabra” (the remote member who has the floor, speaking from their phone);
   - the debate ones: two remotes side by side (“Remoto con la palabra” and “Remoto con la palabra 2”), one from the room and one remote, two from the room;
   - if there's an on-screen vote, the result one.
   `asamblea_escenas_base` proposes them built with the cameras and inputs available (show the proposal to the operator) and with `crear: true` it creates them and configures the debate. They can be touched up afterwards (lower third, background, logo).
5. **The conference system** (if there is one): `help {"topic": "conferencia"}`; each seat with its unit.
6. **The remotes:** `help {"topic": "sesion"}`; link and PIN for those attending remotely; what they see on their phone (`sesion_vistas`).
7. **The agenda** (`orden_del_dia_cargar`): it's shown on the phones and on the chair's console.
8. **The debate** (if there will be one): `debate_config` with the scene for each type and the time for each turn.
9. **The rundown** (`evento_armar`) for what does have an order: opening, anthem, closing. The body of the session is driven by the speakers' queue (`banca_*`), not by cues.

During the session: `banca_al_aire` / `recinto_orador_terminar` (the floor), `presidencia_callar_todos` and `presidencia_hablar`, `debate_iniciar` / `debate_turno`, voting with `dcn_votacion` or `sesion_votacion`.

To try or show all this without touching the real thing there's a complete demo (a council with 15 members, chamber, simulated conference system, scenes and debate): `configuracion_crear {"nombre": "Demo", "desde": "demo:Demo parlamentaria"}` and `configuracion_cambiar` (see `help {"topic": "configuraciones"}`).
