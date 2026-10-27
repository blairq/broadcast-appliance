Una tira de imagenes que muestra una escena. **No es el `carrusel_*` de las pantallas**: eso rota mensajes y QR en un televisor secundario. Esto es la presentacion del orador.

## Para que sirve de verdad
En medio de la charla el orador dice "volve a la del grafico de barras". Con veinte diapositivas y un boton de siguiente son veinte clics y diez segundos de aire perdido. `laminas_ir {"carrusel", "numero"}` es un salto.

## Como se usa
- `carruseles_listar`: que hay. Por aca se empieza; el resto pide el nombre.
- `carrusel_laminas_ver {"carrusel"}`: las laminas en orden, con su numero, su titulo y sus etiquetas. El **numero empieza en 1** y es lo que pide `laminas_ir`.
- `laminas_armar {"nombre", "imagenes"}`: armar uno a mano con placas sueltas (logos, fondos). Si ya existe, agrega al final.
- `laminas_editar {"carrusel", "accion"}`: quitar una lamina, moverla de lugar, renombrar, borrar o soltarlo de la escena. `borrar` se lleva el carrusel, NO las imagenes.
- `laminas_aplicar {"carrusel", "escena"}`: **es el paso que falta casi siempre**. Hasta que el carrusel no esta EN una escena, `laminas_ir` mueve el puntero y no se ve nada.
- `laminas_desarmar {"archivo", "nombre"}`: convierte un PDF o un .pptx en laminas. **Tarda** (segundos por pagina) y no espera: avisa que empezo, y cuando termina aparece en `carruseles_listar`. Un .pptx pasa por DeckCraft (viaja en el programa); el .ppt viejo y el .odp, exportados a .pptx o a PDF.

## Que se busca y como
El operador escribe "barras" en un cuadro y le queda esa lamina. Eso funciona si alguien miro las imagenes antes: `laminas_sin_analizar {"carrusel"}` da la proxima sin etiquetas, se la mira con `imagen_preguntar` (que es quien sabe mirar: el asistente principal NO recibe imagenes), y lo que se vio se guarda con `laminas_etiquetar {"carrusel", "numero", "etiquetas"}`. Se hace **una vez por lamina**; si salio vacio queda marcada igual, para no volver a mirarla para siempre.

Las etiquetas son palabras con las que alguien apurado buscaria: que se ve, que dice, de que trata. "grafico barras ventas trimestre", no una descripcion.