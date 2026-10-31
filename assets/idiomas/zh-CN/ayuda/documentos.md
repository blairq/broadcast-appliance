活动网站发布 **PDF**。每个活动把自己的文档保存在 `docs/`（附带网站用的封面）。

- `documentos_listar`（或 `{"punto": 2}`、`{"seccion": "<id>"}`）、`documento_agregar {"archivo", "titulo", "descripcion", "puntos", "secciones", "publico"}`、`documento_editar`（还有 `pagina_tapa`：用另一页作封面）、`documento_quitar`、`documento_texto`、`documento_abrir`。
- **添加 Word 文件、电子表格、演示文稿、HTML 或 Markdown 时会自动转成 PDF**（无需事先转换）。
- `pdf_convertir {"archivo"}` 或 `{"texto", "formato": "markdown"|"html"|"texto", "titulo"}`：用程序自带的工具（WordCraft、DeckCraft、GridCraft 和 Qt，支持任何语言）生成 PDF。`destino` 用来选择保存位置；`agregar_al_evento: true` 会把它加入活动文档。
- **生成 PDF 不要用无界面的 Chrome**（`--print-to-pdf`、Puppeteer、Playwright）：在 macOS 上它会要求访问系统钥匙串，而且不随程序携带。请用 `pdf_convertir`。
- 你的临时文件放在数据文件夹里的 `Runtime/tmp/`（TMPDIR）：一切都留在 U 盘上。
