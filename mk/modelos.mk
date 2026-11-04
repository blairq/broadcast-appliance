# mk/modelos.mk — El modelo local: corpus, entrenamiento, fábrica e inventario del MCP.
# Lo incluye el Makefile de la raíz (las variables comunes están allá).

.PHONY: mcp-inventario deps-llamacpp bateria-local corpus-entrenamiento venv-entrenar vector-control \
        corpus-descubrir modelo modelo-fabricar modelo-corpus modelo-inventario modelo-etiquetas \
        venv-mlx deps-llamacpp-convertir

# El inventario del MCP a docs/mcp.json: que herramientas hay, con su ayuda,
# sus etiquetas de busqueda, en que archivo viven y que roles las pueden usar.
# NECESITA EL APARATO ANDANDO: la mitad del dato (descripciones y esquemas) sale
# de preguntarle a el, que es lo que el modelo ve de verdad. Sacarlo del codigo
# obligaria a interpretar literales de C++ partidos en pedazos.
mcp-inventario:
	tools/docs/mcp-inventario.py

# llama.cpp (CPU) para medir un modelo chico corriendo ACA. Ver el comentario
# del script: la GPU no esta libre, se la come el motor de video.
deps-llamacpp:
	tools/deps/llamacpp.sh

# Cuanto tarda y cuanto acierta ese modelo chico, con el prefijo cacheado.
# Hace falta un .gguf en extern/modelos (no viaja en el repo: son gigas).
bateria-local:
	tools/entrenar/bateria-local.py --modelo extern/modelos/Qwen3-1.7B-Q4_K_M.gguf --solo-nombre

# El conjunto de entrenamiento del modelo chico, desde las semillas escritas a
# mano. El examen (el corpus de siempre) queda intacto: es el unico numero
# honesto que hay.
corpus-entrenamiento:
	tools/entrenar/corpus-entrenamiento.py

## El entorno de entrenamiento, por script: nada se instala a mano.
venv-entrenar:
	tools/entrenar/venv.sh

## El vector de control y el barrido de su escala. MODELO= el ya entrenado.
## Sale un .gguf que llama.cpp aplica con --control-vector-scaled.
vector-control:
	@test -n "$(MODELO)" || { echo "uso: make vector-control MODELO=<ruta>"; exit 1; }
	.venv-entrenar/bin/python tools/entrenar/vector-control.py \
		--modelo "$(MODELO)" --salida "$(MODELO)-derivar.gguf"
	PYTHON=.venv-entrenar/bin/python tools/entrenar/barrer-escala.sh \
		"$(MODELO)" "$(MODELO)-derivar.gguf"

# Que herramientas nuevas del MCP no tienen frases, y si el prompt cambio
# —que es cuando el modelo entrenado quedo viejo aunque las frases esten—.
# Con --generar propone frases desde las etiquetas del buscador, PARA REVISAR.
corpus-descubrir:
	tools/entrenar/corpus-descubrir.py

## EL MODELO REACTIVO, AUTOMATICO. Cada vez que el MCP crece: inventario, json de
## equivalencias, bucle del corpus (cobertura, sentido, CDNV, Frobenius, margen)
## y fabrica (entrenar, medir, MLX q4, GGUF Q4_K_M con la cabeza MTP entrenada,
## carpetas con forma de repo de Hugging Face; no sube nada).
##   PERFIL: datos/perfiles/<perfil>.json (que herramientas, por categoria)
##   BASE:   Qwen/Qwen3.5-2B | -4B | -9B | unsloth/Qwen3.5-4B-MTP-GGUF ... (se detecta sola)
##   HF_USUARIO=x SUBIR=1: sube los repos (MLX q4 y GGUF) como privados al terminar
PERFIL ?= reactivo
BASE ?= Qwen/Qwen3.5-4B
VUELTAS ?= 2
modelo: venv-entrenar venv-mlx deps-llamacpp deps-llamacpp-convertir
	.venv-entrenar/bin/python tools/entrenar/actualizar-modelo.py --perfil $(PERFIL) --base $(BASE) --vueltas $(VUELTAS) \
		$(if $(HF_USUARIO),--hf-usuario $(HF_USUARIO)) $(if $(SUBIR),--subir)

## Solo la fabrica, con el corpus que hay (NOMBRE= la carpeta en ~/entrenamientos/fabrica).
modelo-fabricar: venv-entrenar venv-mlx deps-llamacpp deps-llamacpp-convertir
	@test -n "$(NOMBRE)" || { echo "uso: make modelo-fabricar NOMBRE=<nombre> [PERFIL=] [BASE=]"; exit 1; }
	.venv-entrenar/bin/python tools/entrenar/fabricar-modelo.py --perfil $(PERFIL) --base $(BASE) --nombre $(NOMBRE)

## Solo el bucle del corpus (NOMBRE= la corrida; MEDIDOR= un adaptador para medir, si hay).
modelo-corpus: venv-entrenar
	@test -n "$(NOMBRE)" || { echo "uso: make modelo-corpus NOMBRE=<nombre> [PERFIL=] [BASE=] [MEDIDOR=]"; exit 1; }
	.venv-entrenar/bin/python tools/entrenar/bucle-corpus-completo.py --perfil $(PERFIL) --base $(BASE) \
		--nombre $(NOMBRE) --vueltas $(VUELTAS) $(if $(MEDIDOR),--medidor $(MEDIDOR))

## Que herramientas hay y cuales entran en el perfil.
modelo-inventario:
	.venv-entrenar/bin/python tools/entrenar/inventario-mcp.py --perfil $(PERFIL)

## El json de equivalencias al dia (si el perfil contesta con etiquetas).
modelo-etiquetas: venv-entrenar
	.venv-entrenar/bin/python tools/entrenar/completar-etiquetas.py --perfil $(PERFIL)

## MLX (para la Mac) y el conversor HF -> GGUF de llama.cpp, por script.
venv-mlx:
	tools/entrenar/venv-mlx.sh
deps-llamacpp-convertir:
	tools/deps/llamacpp-convertir.sh
