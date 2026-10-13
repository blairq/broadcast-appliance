# Plan: documentación de cada evento (PDF con carátula, por punto y por sección)

Pedido (2026-10-13): agregar documentación de las asambleas —PDF, básicamente— con una
descripción y la carátula como miniatura; relacionar cada documento con puntos del orden
del día y con el evento en general; una API; y que la página web del evento la muestre y
la deje bajar, en general y por sección. La miniatura se genera UNA vez y queda guardada
(la web la sirve directo). Ordenada en cada evento en su carpeta `docs/`.

## Dónde vive

En la carpeta del evento (cada evento tiene todo lo suyo; la copia total la lleva):

```
<evento>/docs/
  documentos.json        el índice: [{id, archivo, titulo, descripcion, paginas, bytes,
                         agregado_ms, publico, puntos: [n…], secciones: [id de bloque…]}]
  archivos/<archivo>.pdf los documentos (el nombre original, sin pisar otro)
  miniaturas/<id>.jpg    la carátula (primera página, 360 px de ancho), hecha al agregarlo
```

## Partes

- **Modelo** `evento::Documentos` (el índice, sin Qt) y `DocumentosViewModel` (Qt PDF:
  páginas, título del PDF, el texto de las primeras páginas para sugerir la descripción,
  la carátula a JPG). Agregar (copia el PDF), editar (título, descripción, público,
  puntos, secciones), quitar (a la papelera), el texto de un documento.
- **Interfaz**: pestaña «Documentos» de la página del evento (las miniaturas, arrastrar
  PDF, editar, relacionar con puntos y secciones); en el orden del día, los documentos
  de cada punto.
- **MCP**: documentos_listar, documento_agregar, documento_editar, documento_quitar,
  documento_texto.
- **API pública** (la sala): `GET /api/v1/evento/documentos` (los públicos, con sus
  puntos y secciones), `GET /evento/docs/<id>.pdf` y `/evento/docs/<id>.jpg`. Solo los
  públicos del evento abierto.
- **Página web**: vivo.js trae los documentos (`BAEvento.documentos`, `data-vivo=
  "documentos"`): la portada muestra todos; cada sección, los suyos.
