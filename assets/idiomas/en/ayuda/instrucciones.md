Broadcast Appliance: live streaming studio (scenes and sources on its own GStreamer engine, RTMP/SRT output and recording) with its own video conferencing, the Conference (guests over WebRTC with a lobby, speakers mode with a clock, board for a tablet). This server controls EVERYTHING and lets you SEE everything.

How to work:
1. Get your bearings with `status` and look at the program with `foto_programa` (or `foto_todas_escenas` for all the scenes).
2. If you don't know how to do something: `help` (guide and catalog), `help {"topic": ...}` or `help_search {"query": ...}`.
3. After every visual change, VERIFY by looking: if the capture (foto_fuente / foto_programa) doesn't return the image, `imagen_preguntar` about the scene or source with a specific question. Don't assume. To compare fine brightness or color use numbers, not your eyes.
4. Actions happen live: if there is a stream (`status.streaming`), whatever you change in the on-air scene the audience sees. To prepare something without it being seen, build it in another scene and check it (imagen_preguntar) before scene_switch.
5. People are referred to by name or id (room_status lists them). To see them: foto_invitado / foto_todos_invitados.