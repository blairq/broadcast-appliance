La web del evento publica **PDF**. Cada evento guarda los suyos en `docs/` (con su carátula para la web).

- `documentos_listar` (o `{"punto": 2}`, `{"seccion": "<id>"}`), `documento_agregar {"archivo", "titulo", "descripcion", "puntos", "secciones", "publico"}`, `documento_editar` (también `pagina_tapa`: otra página como carátula), `documento_quitar`, `documento_texto`, `documento_abrir`.
- **Agregar un Word, una planilla, una presentación, un HTML o un Markdown ya lo pasa a PDF** (no hace falta convertir antes).
- `pdf_convertir {"archivo"}` o `{"texto", "formato": "markdown"|"html"|"texto", "titulo"}`: hace un PDF con lo que trae el programa (WordCraft, DeckCraft, GridCraft y Qt, cualquier idioma). `destino` para elegir dónde; `agregar_al_evento: true` lo suma a la documentación.
- **Para un PDF no uses Chrome sin pantalla** (`--print-to-pdf`, Puppeteer, Playwright): en macOS pide el llavero del sistema y no viaja con el programa. Usá `pdf_convertir`.
- Tus temporales van a `Runtime/tmp/` dentro de la carpeta de datos (TMPDIR): todo queda en el pendrive.