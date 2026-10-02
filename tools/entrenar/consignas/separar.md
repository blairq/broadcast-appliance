{ROL}

TAREA: el modelo CONFUNDE los pares de herramientas listados en ./lote.txt (con su ficha). Para cada herramienta escribí {N} frases nuevas. Para cada par, pensá qué distingue a cada una de su pareja (qué objeto toca, qué acción hace, ver contra cambiar, probar contra sacar al aire, crear contra copiar...) y ponelo explícito en cada frase, con su sustantivo técnico propio. Nunca uses las palabras características de la pareja. Que un experto no dude ni un segundo.

SALIDA, una línea por frase: {"herramienta": "nombre_exacto", "frase": "..."}

STE-ES (docs/ste-es.md: el alma de ASD-STE100 en castellano de cabina). El NÚCLEO de la frase (acción, objeto, cantidad) con palabras de un solo significado; la superficie (voseo, muletillas, sin tildes, algún typo) libre:
1. Una palabra, un significado: cada verbo de acción con su sentido, no otro. 2. Las palabras de muchos sentidos («poné», «sacá», «pasá», «mandá», «tirá», «dale», «cambiá») solo con el objeto que fija cuál («poné el zócalo», no «ponelo»). 3. Un pedido, una acción. 4. Objeto explícito (si falta, es falta_dato). 5. Cantidad explícita o con las convenciones. 6. Un término por cosa. 7. Orden en imperativo (voseo) o consulta con su palabra de pregunta; un síntoma («se acopla el mic de Laura») vale si nombra el objeto y el problema. 8. De 3 a 16 palabras.
Lo que escribas pasa por un verificador de STE-ES además de la matemática: si rompe una regla, vuelve con el motivo.
