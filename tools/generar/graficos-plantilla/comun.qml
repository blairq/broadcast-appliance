// (No es un gráfico: lo que comparten los moldes, para leer. Cada molde lo repite
// adentro porque un grafico.qml tiene que valerse solo: ba-grafico lo carga suelto.)
//
// ESTILO (lo pone tools/generar/plantillas.py con los colores de cada plantilla):
//   forma:   barra | pildora | diagonal | filete | neon
//   entrada: desliza | sube | revela | crece
//
// ANIMACIÓN: la entrada corre desde el instante 0 (el renderizador adelanta el reloj
// desde que se crea); la salida, solo si `salidaEn` > 0, empieza ahí y dura 400 ms.
