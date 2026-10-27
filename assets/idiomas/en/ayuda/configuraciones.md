Each configuration is a folder with EVERYTHING of its own: scenes and settings (Config), media, graphics, plugins, members' photos, thumbnails, events and recordings. **Nothing is shared**: touching a plugin or a scene in one doesn't break another. What belongs to the machine (to all of them) is the models, the credentials, the logs and the cache.

- `configuraciones_listar`: which ones exist, the active one, its description, size and where it came from; and the demos the program ships.
- `configuracion_crear {"nombre", "desde", "descripcion"}` creates one (doesn't activate it). `desde`: `vacia` (factory), `actual` (copy of the one in use), the name of another, `demo:<nombre>` (a program demo) or `evento:<carpeta>/<inicio|cierre|ultima>` (the configuration an event was run with: its settings snapshot and the media of the current one). Recordings aren't copied unless `con_grabaciones`.
- `configuracion_cambiar {"nombre"}` switches to another: **the app restarts** with that folder (it copies nothing). It cuts whatever is running (stream, recording, conference): never do it live unless the operator asks.
- `configuracion_editar {"nombre", "renombrar_a"|"descripcion"|"borrar": true}`: rename, describe or send to the trash (`Configuraciones/.papelera`). The active one can't be renamed or deleted.

**The demo**: `demo:Demo parlamentaria` is a generic parliamentary system ready to try: the City Council of Puerto Ejemplo (15 members and a chair with made-up names), chamber, cameras, simulated conference system, assembly and debate scenes, control surface and agenda.

**Portable**: all paths are saved relative to the configuration (`Media/logo.png`, also inside a pipeline: `location=Media/x.png`). The whole data folder can be copied to another machine as is. When creating a source with a relative path, it is resolved against the configuration in use.

In the interface: Settings › System › Configurations (use, duplicate, rename, delete, create from a demo).