# Assembly with remote members

## What it's for
Like an assembly or session, but with members who join from home through the browser (virtual
seat): they have to be able to request the floor, speak, count toward the quorum and cast valid
votes.

## Questions of its own
- How many remote members? How are they identified (personal credential, face validation)?
- Does their vote count the same as an in-person one? Roll-call or secret vote?
- Do they have a good connection and headphones? Is a technical test needed the day before?
- Who validates their attendance (the secretariat)?

## What to set up
- Everything in the assembly or session scenario, plus the remote session: sesion_habilitar,
  sesion_credencial per member, sesion_acceso_opciones.
- The “Remoto con la palabra” scene (the remote member who has the floor) with its return feed; a
  grid of the remote members.
- A technical test scheduled with each remote member (invitado_prueba: echo and network).

## Check
Tunnel with a fixed address; each remote member joined at least once as a test; a test vote
recorded and voided; the quorum counting the remote members.

## Hand over
A credential and a link for each remote member with simple instructions (headphones, browser); the
minutes with how each one voted.
