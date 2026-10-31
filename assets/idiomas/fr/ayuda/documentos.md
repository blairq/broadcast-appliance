La page web de l'événement publie des **PDF**. Chaque événement garde les siens dans `docs/` (avec leur couverture pour le web).

- `documentos_listar` (ou `{"punto": 2}`, `{"seccion": "<id>"}`), `documento_agregar {"archivo", "titulo", "descripcion", "puntos", "secciones", "publico"}`, `documento_editar` (aussi `pagina_tapa` : une autre page comme couverture), `documento_quitar`, `documento_texto`, `documento_abrir`.
- **Ajouter un Word, un tableur, une présentation, un HTML ou un Markdown le convertit déjà en PDF** (inutile de le convertir avant).
- `pdf_convertir {"archivo"}` ou `{"texto", "formato": "markdown"|"html"|"texto", "titulo"}` : crée un PDF avec ce qu'embarque le programme (WordCraft, DeckCraft, GridCraft et Qt, toutes langues). `destino` pour choisir où ; `agregar_al_evento: true` l'ajoute à la documentation.
- **Pour un PDF, n'utilisez pas Chrome sans écran** (`--print-to-pdf`, Puppeteer, Playwright) : sous macOS il demande le trousseau du système et il ne voyage pas avec le programme. Utilisez `pdf_convertir`.
- Vos fichiers temporaires vont dans `Runtime/tmp/` à l'intérieur du dossier de données (TMPDIR) : tout reste sur la clé USB.
