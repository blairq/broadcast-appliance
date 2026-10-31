Веб-страница мероприятия публикует **PDF**. Каждое мероприятие хранит свои в `docs/` (с обложкой для веб-страницы).

- `documentos_listar` (или `{"punto": 2}`, `{"seccion": "<id>"}`), `documento_agregar {"archivo", "titulo", "descripcion", "puntos", "secciones", "publico"}`, `documento_editar` (также `pagina_tapa`: другая страница как обложка), `documento_quitar`, `documento_texto`, `documento_abrir`.
- **При добавлении Word, таблицы, презентации, HTML или Markdown файл сам переводится в PDF** (заранее конвертировать не нужно).
- `pdf_convertir {"archivo"}` или `{"texto", "formato": "markdown"|"html"|"texto", "titulo"}`: делает PDF средствами, встроенными в программу (WordCraft, DeckCraft, GridCraft и Qt, любой язык). `destino` — чтобы выбрать, куда; `agregar_al_evento: true` добавляет его в документацию.
- **Для PDF не используйте Chrome без экрана** (`--print-to-pdf`, Puppeteer, Playwright): в macOS он запрашивает системную связку ключей и не едет вместе с программой. Используйте `pdf_convertir`.
- Ваши временные файлы идут в `Runtime/tmp/` внутри папки данных (TMPDIR): всё остаётся на USB-накопителе.
