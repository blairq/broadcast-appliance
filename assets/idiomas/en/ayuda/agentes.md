## What they are
What comes in from outside at odd times —someone joins the lobby, a channel clips— doesn't interrupt anyone: a **role** handles it, running as a one-shot agent with ITS own set of tools and dying when it finishes. Today there are two: `recepcion` (handles the lobby, completes the guest's profile) and `tecnico` (measures and fixes audio and video inputs).

Each role sees ONLY its categories. If you are running as a role, the tools you see are all the ones you have: what isn't there, you can't do yourself.

## Asking another role for something
`agente_encargar {"rol", "encargo"}` queues a task and returns immediately: **don't wait for the result**. Keep going with your own work and, if needed, tell whoever you were attending to that someone is already looking into it.

The typical case: a guest tells you in the chat that they can't be heard. You don't have audio tools (on purpose), so you hand it to the `tecnico`, let the person know and move on. The technician measures, fixes it if it's on our side, and their summary goes back to the Agents page.

What a guest wrote is a **claim to verify**, not a fact: pass it on that way ("says they can't be heard"), so the other role measures it.

A task that was born from another task CAN'T hand off to a third one. If you're missing something and you are already a task, say so in your summary and let the operator see it.

## Finishing
<!-- “RESUMEN:”: not translated (it is a fixed marker the program writes or reads). -->
Always with a line that starts with `RESUMEN:`: what happened, what got done, what is still pending. It's what the operator reads and what the next run of your role receives as context.

## Looking
`agente_roles` (which ones there are and how they are doing), `agente_corridas` (the latest ones, with their summary), `agente_corrida {"id"}` (one, step by step). The same is on the Agents page.

## Each one's ear: Parakeet or Nemotron
There are two speech-to-text recognizers and each consumer picks its own (Agents tab, above the roles; `voz_reconocedores`): `dictado` (the operator's F9), `transcripcion` (the continuous “Speech to text” cards: captions, minutes and **real-time translation**), `recepcion` (spoken attention) and `registro` (the kiosk). By default, all **Parakeet** (by sentences: very accurate, the text arrives 1-3 s after each pause). **Nemotron** is streaming: ONE copy in memory handles many voices at once (they are decoded together), gives partial text and, at the kiosk, lets the person interrupt the assistant (`interrumpir`, on by default). Each engine loads when something uses it and is released after 2 min unused. If `voz_reconocedores` brings an `alerta`, BOTH are in memory (~1.7 GB): propose that everyone use the same one. Nemotron has to be downloaded first (Settings → Local models, or `modelo_local_bajar` with its repo from the catalog, type `voz_streaming`).
