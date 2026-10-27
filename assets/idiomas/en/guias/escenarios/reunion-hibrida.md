# Hybrid work meeting

## What it's for
A formal meeting with people in the room and people connected: there are presentations, documents,
sometimes decisions. Remote people have to take part just like those present.

## Questions of its own
- How many in the room and how many remote? Who chairs or moderates?
- Are there presentations (.pptx, PDF)? Are they shown on screen from here or does each person share them?
- Do decisions have to be made or something voted? (If they vote with a quorum: the “Remote assembly” scenario.)
- Is it recorded? Are minutes needed?
- Are there TVs in the room so those present can see the remote people?

## What to build
- Blocks per topic with the person in charge and the time.
- Scenes: “Room” (general camera), «Remoto con la palabra» (the remote member who has the floor:
  the program creates it and looks for it by that name), “Presentation” (slides or PDF:
  laminas_desarmar or pdf_escena_crear), “Grid”.
- Event documents with the PDFs per topic (documento_agregar, linked to their block).
- If there are TVs: pantalla_agregar and what to show on each one (the program or the grid).
- Speaker queue to order who has the floor (speakers_config).

## Check
Tunnel with a stable address; a test remote participant (echo and network); that the presentations
look right in the scene; the TVs showing.

## Hand over
Invitations with a link for each remote person; the event website with the documents; the
control surface link if someone runs it from a tablet.
