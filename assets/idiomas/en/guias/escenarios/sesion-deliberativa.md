# Session of a deliberative body

## What it's for
A formal session with members at their seats, the chair, the secretariat, an agenda, regulated
use of the floor, quorum and roll-call votes with an official result. Public and streamed.
Also read help {"topic": "asamblea"} and the assembly session procedure.

## Questions of its own
- Which body, how many seats, who chairs, who is the secretary?
- Is the Body loaded (members, caucuses, seats)? If not: cuerpo_importar or cuerpo_plantilla.
- Is there a conference system (seat microphones, e.g. Bosch DCN)? PTZ cameras with a position
  per seat?
- Remote members (virtual seat)? Do they vote remotely?
- The agenda and the files. Special majorities on any item?
- Speaking times? Points of privilege, rebuttals?
- Streaming, sign language interpreter (DSK layer), captions?
- TVs in the chamber to show the vote?

## What to build
- Body and chamber (recinto_editar) with each seat and its camera preset (ptz_posicion_crear).
- Base assembly scenes (asamblea_escenas_base): chair, seat with the floor, wide
  shot, vote, recess; review with asamblea_revisar.
- Agenda (orden_del_dia_cargar) with documents per item.
- Quorum (quorum_regla) and vote (votacion_abrir / votacion_resultado_oficial).
- If there are remote members: sesion_habilitar, credentials (sesion_credencial) and a test with each one.
- Chamber displays with the vote (pantalla_mostrar content votacion).
- Chair or microphone control surface if it is run from a tablet (templates “Assembly”,
  “Seat microphones”).

## Check
Central unit connected (dcn_conectar), cameras at their positions, quorum with the real roll, a remote
member casting a test vote, the public website with the agenda.

## Hand over
Credentials for remote members, the public website, the chair's control surface; at the end, the
minutes with the votes (evento_acta) and the recording with chapters per item (evento_capitulos).
