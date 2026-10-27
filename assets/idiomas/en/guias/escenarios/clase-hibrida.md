# Hybrid class

## What it's for
A teacher gives a class with some students present and others connected. No quorum or voting, but
with attendance (taken by hand), materials, questions and a recording for whoever missed it.

## Questions of its own
- What subject, topic and level? How long does it last and what parts is it divided into?
- How many students in the classroom and how many remote? Do they need to register or is the
  link enough?
- Is attendance taken? (By hand: padron_anotar / padron_acreditar per student.)
- Materials: worksheets, readings, presentations? Are they shown on screen? (PDF scene.)
- A teleprompter for the teacher with the class script?
- Does any student need translation or captions (another language, hearing loss)?
- The school's logo or the province's coat of arms? Slates for each topic with images?
- Do remote students ask in the chat or speak? Is it recorded to upload later?

## What to set up
- Blocks per topic with a title slate per topic (real text in `valores`, salidaEn 4000-5000) and a
  lightweight image of the topic in its scene.
- Scenes “Teacher”, “Whiteboard/Presentation”, “PDF” (pdf_escena_crear) with the worksheets loaded
  in the event's documents, linked to each topic.
- A teleprompter with the script of each part.
- A roll of students for attendance (without `vota`), with their contact if the invitation is sent.
- Captions or translation if requested (traduccion_agregar).
- The school's logo as a DSK layer.

## Check
The remote students' link tested from a phone; the PDF scene with the worksheets; the teacher's
audio (lavalier mic if there is one).

## Hand over
The link for the students (or the invitation per student), the class's web page with the
materials, and at the end the recording with chapters per topic.
