# mk/publicar.mk — Publicar: los paquetes en Docker y la subida a un release de GitHub.
# Lo incluye el Makefile de la raíz (las variables comunes están allá).

.PHONY: dist-linux-docker dist-win-docker subir publicar publicar-macos publicar-linux publicar-win

# --- Archivos de distribucion (los que sube el CI a un release) --------------
# dist/BroadcastAppliance-<version>-<plataforma>.{zip,tar.gz}: la carpeta
# portable completa, lista para descomprimir y usar (o para `--update`).

# ============================================================================
# Releases desde una maquina propia (el CI de GitHub corre solo a mano).
#
#   make publicar            TODO: controles, Linux y Windows (en Docker), el
#                            .dmg si esto es una Mac, y la subida a GitHub.
#
# O por partes (cada dist-* deja el archivo y su .sha256 en dist/):
#   make dist-linux-docker   .tar.gz de Linux, en Docker (Ubuntu 24.04 x86_64)
#   make dist-win-docker     .zip de Windows, en Docker (cross con clang-cl)
#   make dmg-macos           .dmg de macOS (solo en una Mac)
#   make subir               sube lo que haya en dist/ de esta version y verifica
#                            en GitHub tamaño y sha256 (tools/publicar.sh)
#
# El release sale del commit (v<version>-<commit>): lo armado en distintas
# maquinas desde el mismo commit cae junto. Desde master queda "Latest"; si
# no, pre-release. Antes de compilar se chequea: sin cambios sin commitear,
# commit subido a GitHub, release del mismo commit. FORZAR=1 saltea; TAG=...
# elige otro release.
#
# En Linux sin Docker: make publicar-linux / publicar-win (compilan aca).
# ============================================================================

dist-linux-docker:
	tools/ci/compilar-en-docker.sh linux

# Los tests de Windows corren bajo Wine. En una Mac, Wine va encima de Rosetta
# y algunos fallan sin que el programa ande mal (en el runner del CI pasan):
# ahi el fallo se AVISA pero no frena. En un Linux de verdad, frena.
dist-win-docker:
	@rc=0; tools/ci/compilar-en-docker.sh win || rc=$$?; \
	if [ $$rc = 3 ] && [ "$$(uname -s)" = Darwin ]; then \
	  echo ""; echo "AVISO: tests de Windows bajo Wine fallaron (Wine sobre Rosetta): ver arriba. El .zip se armo." >&2; echo ""; \
	elif [ $$rc != 0 ]; then exit $$rc; fi

subir:
	@archivos="$$(ls $(DIST_LINUX) $(DIST_WIN) $(DIST_MACOS) 2>/dev/null)"; \
	  [ -n "$$archivos" ] || { echo "ERROR: no hay nada armado en dist/ para la version $(BA_VERSION)" >&2; exit 1; }; \
	  tools/publicar.sh $$archivos

# Lo que se publica se arma y se prueba entero: sin la caché de tests ni los
# atajos de «no cambió nada» (SIN_CACHE=1 llega a Docker y a cada sub-make).
# Y los tests en serie, como siempre: en paralelo, los del motor (tiempo real)
# pueden fallar por la carga sin que el programa ande mal.
publicar publicar-macos publicar-linux publicar-win: export SIN_CACHE = 1
# Lo que se publica sale sin el detector de acoples (solo desarrollo y pruebas).
publicar publicar-macos publicar-linux publicar-win paquetes paquete-mac paquete-linux paquete-windows: export DIAG_ACOPLES = 0
publicar publicar-macos publicar-linux publicar-win: export TESTS_JOBS = 1
publicar:
	tools/publicar.sh --solo-chequear
	rm -f $(DIST_LINUX)* $(DIST_WIN)* $(DIST_MACOS)*
	$(MAKE) paquetes
	$(MAKE) subir

# Una plataforma sola, compilando en ESTA maquina (sin Docker).
publicar-macos:
	tools/publicar.sh --solo-chequear
	$(MAKE) macos
	$(MAKE) dmg-macos
	tools/publicar.sh $(DIST_MACOS)

publicar-linux:
	tools/publicar.sh --solo-chequear
	$(MAKE) build
	$(MAKE) test
	$(MAKE) dist-linux
	tools/publicar.sh $(DIST_LINUX)

publicar-win:
	tools/publicar.sh --solo-chequear
	$(MAKE) test-win BA_ACCEPT_MSVC_LICENSE=1
	$(MAKE) dist-win BA_ACCEPT_MSVC_LICENSE=1
	tools/publicar.sh $(DIST_WIN)
