The standard procedure for an **assembly session** (council, board, directors' meeting, owners' association): members in seats, quorum, agenda, votes and, if any, remote members. A session **is not a rundown of cues**: the floor changes hands with no set order and the speakers queue handles it. Follow it in order, **propose before you act** and don't make up data. For the details of each piece: help asamblea, conferencia and sesion.

**0. What to ask:** name of the body and of the session; date, time and **time zone**; the **list of members** with seat, name, position and caucus (a spreadsheet is best); who **votes**; the **quorum rule** and the majority; the **agenda** (items and files); whether there is a **conference system** (seat microphones) or you work without one; whether there are **remote members**; where it is streamed and whether it is recorded; whether there is an anthem, a minute of silence or other fixed moments.

**1. Set up:**
| What | With what |
|---|---|
| The event as an assembly and its rundown (only what has an order: Pre-show, opening, closing) | `evento_armar {nombre, fecha, inicio, huso, asamblea: true, gente, bloques}` |
| The Body: each member with their seat, position and caucus (validated) | `cuerpo_importar` (spreadsheet), `integrante_editar`, `integrante_validar` |
| The chamber: the grid of seats | `recinto_editar {filas, columnas, auto: true}` |
| The shot of each seat | `ptz_posicion_crear` and `banca_toma {banca, camara, posicion}`; test with `enfocar {que: "banca N"}` |
| THE SCENES for seats, wide shot, remote member with the floor and debate | `asamblea_escenas_base` (show the proposal; `crear: true`) |
| The conference system (if any) | `dcn_conectar`; each seat with its unit (help conferencia) |
| The remote members (if any) | `sesion_credencial {banca}` (link and PIN), `sesion_acceso_opciones`; the chair's console: `sesion_consola {accion: crear}` |
| The roll with who votes | `evento_armar {gente: [{…, vota: true}]}` or `padron_anotar` |
| The quorum | `quorum_regla` |
| The agenda | `orden_del_dia_cargar` |
| The assembly graphics: member lower third, agenda item, quorum, vote result, recess | they link themselves to what is validated (help asamblea, `grafico_listar` category Asamblea) |
| Where it is streamed and the recording | as in an in-room event (help procedimiento_salon, step 1) |
| The event's web page (shows the assembly, the chamber with the seats by caucus, the quorum and the votes) | `evento_web {accion: generar}` |
| The beginning and the end: test pattern, standby slate, end slate | `evento_preparar` |

**2. Check:** `asamblea_revisar` (body, chamber, presets, scenes, conference system, virtual seat, agenda) → `evento_revisar` → `evento_verificar {tipo: asamblea}`. Fix each “missing” item and repeat until nothing is missing.

**3. Ask the person to check:** the “a_mano” list from `evento_verificar`: besides the general items (cameras, sound, graphics, page, recording, test stream), the **conference system tested seat by seat**, the **shot of each seat**, a **test vote**, the **remote members with their link and PIN** (one of them tested), the **chair's console** and **who takes the minutes**. Mark with `evento_chequeo` only what they confirm.

**4. Rehearse:**
- **Each seat:** give it the floor (`banca_al_aire`) and end it (`recinto_orador_terminar`): does the camera move, does the scene come together, does the lower third show the right name, does the microphone open?
- **A remote member:** have them join with their link and PIN, enable them, give them the floor.
- **Test vote:** with the conference system or with the virtual seat (`sesion_votacion`); the official result (`votacion_resultado_oficial`) on screen.
- **Quorum:** `quorum_presentes` with those present.
- And what every event needs: music, graphics, web page, recording and test stream (help procedimiento_salon, step 4).

**5. Leave it ready:** `evento_verificar {tipo: asamblea}` with nothing missing and everything confirmed; `evento_preparar {al_aire: true}` leaves the test pattern on program. Let the person know.

**6. On the day, from the test pattern to the end:**
1. **30 to 15 minutes before:** `evento_iniciar`; test pattern; `stream_start` and `record_start`; confirm on the platform.
2. **Before starting:** standby slate with music; the remote members join and are enabled; `quorum_presentes`.
3. **Opening:** with quorum, the first block (anthem, the chair's opening).
4. **The session:** the speakers queue (`banca_cola`, `banca_al_aire`, `recinto_orador_terminar`); the item under discussion (`orden_del_dia_punto`); votes and their official result; a recess with its slate if needed. The log goes straight to the minutes (`sesion_registro`).
5. **Closing:** the Closing block (end slate); `stream_stop`, `record_stop`, `evento_terminar` and `evento_informe`.
