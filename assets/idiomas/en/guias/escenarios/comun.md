# Preparing an event: the protocol (applies to every type)

Preparing an event is NOT doing what the operator names: it is walking them, step by step, to an
event that works, without them having to remember to ask for each thing. Whatever you don't ask
about will be missing on the day of the event.

## 1. Interview BEFORE you set up anything

Ask the questions in the scenario guide a few at a time (three or four per message, with options
when possible: “in person, remote, or both?”). Suggest reasonable values for what the operator
doesn't know (“if you're not sure, I'll set it to 90 minutes”). Before touching anything,
summarize what you are going to set up in a short list and ask for the OK.

Always, for any event:
- Name, date, start time, duration and TIME ZONE (without a time zone, remote people don't know
  what time it is).
- Who is coming and how: how many people? In person, remote or both? Are there guests who speak
  (they join the room from the browser)? Is there an audience that watches (the event's web page)
  and asks questions?
- Do people need to be registered? How are they contacted: email, WhatsApp, Telegram? Do they get
  an invitation with a link and a QR code?
- Is it streamed (YouTube, Twitch, Facebook, another platform)? Is it recorded?
- Is there an agenda, a list of topics or blocks? Who speaks in each part and for how long?
- Are there documents to share (PDFs, presentations, readings)? Are they shown on screen?
- Visual identity: the institution's logo, the coat of arms of the province or the country,
  colors, a background image? (Ask for the files or look for them; see “Images” below.)
- Background music or a bumper for the standby and the breaks?
- A teleprompter for whoever hosts or presents?
- Translation into another language (captions or voice)? For whom?
- Screens in the room (TVs), or QR codes printed or on screen?
- Who operates on the day of the event and from where (the machine, a tablet: control surface)?

## 2. Set it up in order

1. The event (evento_nuevo / evento_armar) with date, time, time zone, duration and blocks with
   their times. After evento_nuevo with abrir, the app restarts and the conversation continues on
   its own.
2. The people: padron_anotar with their role (speaker, guest, member, student…), how they attend
   and their contact (email, WhatsApp). For voting: `vota: true`.
3. The scenes: one per moment (opening, host, guest, presentation, PDF, closing) and, if the topic
   changes, an image of its own for each topic. A scene for documents: pdf_escena_crear.
4. The graphics: lower thirds with real NAMES and TITLES, title slates for each block. The texts go
   in the cue's `valores` (not `parametros`); how long they stay on screen, `salidaEn` (ms) inside
   the values (0 = it stays). Test them with grafico_probar before leaving them in the rundown.
5. The teleprompter: one speech per part (prompter_escribir) and its cue in the rundown.
6. The documents: documento_agregar (or pdf_convertir) with a description, linked to its item or
   section. Look for whatever the operator names; if there is nothing, suggest what to add.
7. The event's web page (evento_web): title, description, a lightweight image, documents.
8. Music (media_list, playlist_crear), logos and coats of arms (DSK layers: dsk_definir),
   translation (traduccion_agregar) if needed.
9. The rundown with its cues (guion_cue) and the checklist (evento_verificar).

## 3. Check BEFORE inviting anyone

A link or a QR code that gets sent and doesn't work is worse than not sending it.
- The connection from outside: acceso_remoto_estado (the tunnel works and the address is the final
  one).
- If a QR code is going to be PRINTED or invitations sent days in advance: a FIXED address (your
  own domain or a reserved subdomain: acceso_remoto_probar_direccion, acceso_remoto_dns_verificar).
  If the address changes later, everything has to be reprinted and resent. Tell the operator.
- The camera and the audio: foto_programa / foto_fuente; that it can be seen and heard (without
  playing through the speakers at night: measure with audio_measure).
- A test guest: the room link (room_link) opened from another device or the browser, and
  invitado_prueba (echo and network) with someone inside.
- The event's web page opens (evento_web) and shows what it has to show.
Only then: the invitations (invitacion per person: text, link and QR code), and explain how to
send them (email, WhatsApp) or send them with a notifier if one is configured.

## 4. Rehearsal

ALWAYS suggest it: evento_ensayo_rapido (goes through the rundown without waiting) and, better, a
real rehearsal with evento_correr {accion: arrancar, ensayo: true}. Tell them what failed and fix
it.

## 5. Hand it over

When you are done, in one message:
- The LINK to the event's web page (evento_web) so they can open it.
- The links each person will use: guest room, teleprompter, control surface, audience.
- What is still pending and what only a person can confirm (evento_chequeo_ver).
- How to start on the day of the event (Direction card: “Start the rundown”).

## Images

Backgrounds and slates: JPG of 1920×1080 at most and under 500 KB (an 8 MB image takes a long time
to load on the web and on the audience's phones). If the one you find is heavy, shrink it before
using it (ffmpeg is included: `ffmpeg -i in.png -vf scale=1920:-1 -q:v 4 out.jpg`). Logos with a
transparent background (PNG). Say where each image came from and whether it can be used.

## What is NOT done without asking

Going on air, streaming, sending invitations, deleting or overwriting scenes that already exist,
spending money or creating accounts. A new event starts FROM SCRATCH: don't reuse or modify the
scenes of another event unless you are asked to.
