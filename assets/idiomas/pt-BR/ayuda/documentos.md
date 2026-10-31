A página web do evento publica **PDF**. Cada evento guarda os seus em `docs/` (com sua capa para a página web).

- `documentos_listar` (ou `{"punto": 2}`, `{"seccion": "<id>"}`), `documento_agregar {"archivo", "titulo", "descripcion", "puntos", "secciones", "publico"}`, `documento_editar` (também `pagina_tapa`: outra página como capa), `documento_quitar`, `documento_texto`, `documento_abrir`.
- **Adicionar um Word, uma planilha, uma apresentação, um HTML ou um Markdown já o converte em PDF** (não é preciso converter antes).
- `pdf_convertir {"archivo"}` ou `{"texto", "formato": "markdown"|"html"|"texto", "titulo"}`: faz um PDF com o que o programa traz (WordCraft, DeckCraft, GridCraft e Qt, qualquer idioma). `destino` para escolher onde; `agregar_al_evento: true` o adiciona à documentação.
- **Para um PDF, não use o Chrome sem tela** (`--print-to-pdf`, Puppeteer, Playwright): no macOS ele pede o chaveiro do sistema e não viaja com o programa. Use `pdf_convertir`.
- Seus temporários vão para `Runtime/tmp/` dentro da pasta de dados (TMPDIR): tudo fica no pen drive.