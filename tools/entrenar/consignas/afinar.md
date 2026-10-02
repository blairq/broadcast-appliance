{ROL}

TAREA: una medición matemática del modelo marcó, en ./lote.txt, qué frases de cada herramienta empeoran su grupo y HACIA QUÉ VECINA lo corren ("tira a"), y qué le falta a cada grupo (PEDIDO). Atacá ese detalle con inteligencia, no reemplaces palabras al azar:
1. Frases marcadas ARREGLAR: pensá por qué tiran hacia esa vecina (qué palabra, qué objeto o qué forma de pedir comparte con ella, o si pide algo que la herramienta no hace) y reescribila conservando la intención, sacando ese detalle y poniendo lo que distingue a ESTA herramienta según su ficha.
2. Frases nuevas: el DOBLE de las que pide, fieles a la ficha y variadas; si el grupo dice APRETADO, variá mucho la forma.
3. Si dice que sobran: cuáles de las no marcadas son casi repetidas.
Si hay frases RECHAZADAS en una vuelta anterior, no repitas su error ("cae mas cerca de X" = se parece a X; "corre el grupo" = la aleja de su centro; "se parece al examen o al banco" = usá otra forma).

SALIDA, una línea por cambio:
  {"herramienta": "nombre", "tipo": "reescribe", "original": "texto exacto de la marcada", "frase": "nueva"}
  {"herramienta": "nombre", "tipo": "nueva", "frase": "..."}
  {"herramienta": "nombre", "tipo": "sobra", "original": "texto exacto", "frase": ""}

STE-ES (docs/ste-es.md: el alma de ASD-STE100 en castellano de cabina). El NÚCLEO de la frase (acción, objeto, cantidad) con palabras de un solo significado; la superficie (voseo, muletillas, sin tildes, algún typo) libre:
1. Una palabra, un significado: cada verbo de acción con su sentido, no otro. 2. Las palabras de muchos sentidos («poné», «sacá», «pasá», «mandá», «tirá», «dale», «cambiá») solo con el objeto que fija cuál («poné el zócalo», no «ponelo»). 3. Un pedido, una acción. 4. Objeto explícito (si falta, es falta_dato). 5. Cantidad explícita o con las convenciones. 6. Un término por cosa. 7. Orden en imperativo (voseo) o consulta con su palabra de pregunta; un síntoma («se acopla el mic de Laura») vale si nombra el objeto y el problema. 8. De 3 a 16 palabras.
Lo que escribas pasa por un verificador de STE-ES además de la matemática: si rompe una regla, vuelve con el motivo.
