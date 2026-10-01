# ============================================================================
# tools/ci/compilar.Dockerfile — la maquina de compilar de Linux y Windows
#
# Ubuntu 24.04 x86_64, lo mismo que el runner del CI, con las herramientas de
# build que instalan los jobs `linux` y `windows` de .github/workflows/ci.yml.
# Nada de esto viaja en el bundle: son las herramientas de quien compila (las
# dependencias del producto las baja cada `make deps-*` a extern/, con hash).
#
# Las bibliotecas de escritorio de la ultima linea (cups, nss, avahi...) las
# pide el LibreOffice oficial al sistema (tools/deps/linux-baseline-libreoffice.txt):
# el runner del CI las trae y sin ellas libreoffice.sh no puede VERIFICAR que
# convierte.
#
# En una Mac con Apple Silicon corre con Rosetta (ver compilar-en-docker.sh).
# ============================================================================
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      cmake build-essential pkg-config git curl ca-certificates rsync unzip xz-utils zstd file \
      ccache ninja-build clang patchelf p7zip-full binutils \
      python3 python3-venv \
      libgl1-mesa-dev libegl1-mesa-dev libx11-dev libx11-xcb-dev libxcb1-dev \
      libxcb-randr0-dev libxcb-shm0-dev libxcb-xfixes0-dev libxcb-xinerama0-dev \
      libxcb-composite0-dev libxcb-damage0-dev libxcb-present-dev libxcb-xkb-dev \
      libxcomposite-dev libxinerama-dev libxkbcommon-dev libxss-dev \
      libxext-dev libxrender-dev libxrandr-dev \
      libpulse-dev libasound2-dev libudev-dev \
      libwayland-dev libdrm-dev libfreetype-dev libfontconfig1-dev \
      libsndfile1-dev libcairo2-dev libgl-dev \
      xvfb libgl1-mesa-dri \
      libcups2t64 libnss3 libavahi-client3 libxinerama1 libglib2.0-0t64 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /trabajo
