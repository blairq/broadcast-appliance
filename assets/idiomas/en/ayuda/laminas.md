A strip of images shown by a scene. **It is not the displays' `carrusel_*`**: that rotates messages and QR codes on a secondary TV. This is the speaker's presentation.

## What it's really for
In the middle of the talk the speaker says "go back to the one with the bar chart". With twenty slides and a next button that's twenty clicks and ten seconds of wasted airtime. `laminas_ir {"carrusel", "numero"}` is one jump.

## How it's used
- `carruseles_listar`: what's there. This is where you start; the rest asks for the name.
- `carrusel_laminas_ver {"carrusel"}`: the slides in order, with their number, title and tags. The **number starts at 1** and it's what `laminas_ir` asks for.
- `laminas_armar {"nombre", "imagenes"}`: build one by hand with loose slates (logos, backgrounds). If it already exists, it appends at the end.
- `laminas_editar {"carrusel", "accion"}`: remove a slide, move it, rename, delete or detach it from the scene. `borrar` takes the carousel, NOT the images.
- `laminas_aplicar {"carrusel", "escena"}`: **this is the step that's almost always missing**. Until the carousel is IN a scene, `laminas_ir` moves the pointer and nothing is seen.
- `laminas_desarmar {"archivo", "nombre"}`: turns a PDF or a .pptx into slides. **It takes a while** (seconds per page) and doesn't wait: it says it started, and when it finishes it shows up in `carruseles_listar`. A .pptx goes through DeckCraft (it travels with the program); old .ppt and .odp files, exported to .pptx or PDF.

## What gets searched and how
The operator types "bars" in a box and gets that slide. That works if someone looked at the images beforehand: `laminas_sin_analizar {"carrusel"}` gives the next one without tags, you look at it with `imagen_preguntar` (it's the one that can look: the main assistant does NOT receive images), and what was seen is saved with `laminas_etiquetar {"carrusel", "numero", "etiquetas"}`. It's done **once per slide**; if it came out empty it's marked anyway, so it's never looked at again.

Tags are words someone in a hurry would search with: what's shown, what it says, what it's about. "bar chart sales quarter", not a description.