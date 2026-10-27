Folders (inside the portable system): `Media/` (the library: logos, slates, backgrounds, music, videos), `Shares/` (what guests shared), `Recordings/`.

## Recipe: "find image X and use it as a logo at the top of scene 4"
1. `media_find {"query": "X"}` (ignoring case, accents and dashes).
2. `media_view {"path": ...}` to confirm it's the right one (look at it).
3. `scene_list` to find the scene ("scene 4" may be the fourth in the list or one with that name: if in doubt, ask).
4. `medio_poner {"path": ..., "scene": ..., "name": "Logo", "layout": "pip-tr"}` (top right; `pip-tl` top left; or `{x,y,w,h}`).
5. `foto_fuente {"name": escena}` and adjust with `source_update` (position, size) until it looks right.

Videos and audio: `medio_poner` with `loop` for looping backgrounds or music; `alert_create {"type": "media_end"}` to find out when they end.

## Organize the disk
`media_carpetas` tells you HOW they are spread out (the tree, with how many items each folder has; empty ones show up too, because they are where things get moved to). `media_ordenar {"accion": "mover"|"renombrar"|"carpeta"|"borrar"}` rearranges them.

**Deleting doesn't delete**: it sends to `Papelera/`. A file may be in a scene, or be the only one left from the last event; undoing has to be taking it out of a folder. And if the destination already has something with that name, it gets numbered instead of overwritten.

The system folders (Media, Shares, Recordings) are not moved or deleted. The operator sees all this in the Media tab, and whatever you change here shows up for them without having to refresh.

## Presentations
A `.pdf` or a `.pptx` isn't "used" in a scene: it is BROKEN DOWN into slides and becomes a carousel, which is what lets you jump to any of them live. See `help {"topic": "laminas"}`.