The event website publishes **PDFs**. Each event keeps its own in `docs/` (with its cover for the website).

- `documentos_listar` (or `{"punto": 2}`, `{"seccion": "<id>"}`), `documento_agregar {"archivo", "titulo", "descripcion", "puntos", "secciones", "publico"}`, `documento_editar` (also `pagina_tapa`: another page as the cover), `documento_quitar`, `documento_texto`, `documento_abrir`.
- **Adding a Word file, a spreadsheet, a presentation, an HTML or a Markdown file already converts it to PDF** (no need to convert first).
- `pdf_convertir {"archivo"}` or `{"texto", "formato": "markdown"|"html"|"texto", "titulo"}`: makes a PDF with what the program ships (WordCraft, DeckCraft, GridCraft and Qt, any language). `destino` to choose where; `agregar_al_evento: true` adds it to the documentation.
- **For a PDF don't use headless Chrome** (`--print-to-pdf`, Puppeteer, Playwright): on macOS it asks for the system keychain and it doesn't travel with the program. Use `pdf_convertir`.
- Your temporary files go to `Runtime/tmp/` inside the data folder (TMPDIR): everything stays on the USB drive.