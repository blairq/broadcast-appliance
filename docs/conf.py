# ============================================================================
# docs/conf.py — Configuración de Sphinx para "Broadcast Appliance Portable v2"
#
# Proyecto C++20 documentado en español. El build es ROBUSTO por defecto:
# solo necesita Sphinx + sphinx-rtd-theme (docs/requirements.txt) y las páginas
# RST manuales de docs/modules/.
#
# Integración con Doxygen (opcional):
#   - Si existe el XML generado por Doxygen en `docs/doxygen/xml` Y breathe está
#     instalado, se activa `breathe` para poder incluir páginas de símbolos C++
#     (.. doxygenimport:: / .. autodoxygenfunction:: / etc.).
#   - Si falta cualquiera de los dos, el build sigue funcionando con las páginas
#     RST manuales. El build de ReadTheDocs NUNCA depende de que Doxygen haya
#     corrido con éxito.
# ============================================================================

import os

# --- Proyecto ---------------------------------------------------------------
# Nombre, autor y versión. La versión coincide con CMakeLists.txt
# (project(... VERSION 2.0.0)) y con versions.cmake.
project = "Broadcast Appliance Portable v2"
author = "Equipo Broadcast Appliance"
copyright = "2026, Broadcast Appliance"

# `version` = versión corta; `release` = versión completa (2.0.0).
version = "2.0.0"
release = "2.0.0"

# Idioma de la documentación (español). Sphinx traduce sus mensajes con esto.
language = "es"

# Documento raíz del toctree.
master_doc = "index"

# Archivos/directorios a excluir del build.
exclude_patterns = [
    "_build",
    "Thumbs.db",
    ".DS_Store",
    # El XML de Doxygen no debe indexarse como documento RST.
    "doxygen",
]

# --- Extensiones (SAFE sin breathe) -----------------------------------------
# Lista base: NO depende de breathe. Sphinx core es suficiente para construir
# las páginas RST manuales de docs/modules/ y las páginas de compilación/tests.
extensions = []

# Doxygen / breathe (OPCIONAL). Solo se activa si:
#   (a) existe el XML generado por Doxygen en docs/doxygen/xml, Y
#   (b) el paquete `breathe` está instalado en el entorno.
# Si falta cualquiera, se omite y el build sigue funcionando.
_doxygen_xml_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "doxygen", "xml")
if os.path.isdir(_doxygen_xml_dir):
    try:
        import breathe  # noqa: F401
        extensions.append("breathe")
        # Proceso del proyecto que breathe usará para resolver los símbolos.
        breathe_default_project = "BroadcastAppliance"
        # Rol por defecto para los símbolos C++ (dominio cpp de Sphinx core).
        breathe_default_role = "cpp"
    except ImportError:
        # breathe no instalado: seguimos solo con RST manual.
        pass

# --- Tema HTML (con fallback) -----------------------------------------------
# sphinx-rtd-theme si está disponible; si no, cae a un tema por defecto de
# Sphinx ("classic") para que el build nunca falle por falta de tema.
try:
    import sphinx_rtd_theme  # noqa: F401
    html_theme = "sphinx_rtd_theme"
    html_theme_options = {
        "collapse_navigation": True,
        "sticky_navigation": True,
        "navigation_depth": 3,
        "titles_only": True,
        "prev_next_buttons_location": "bottom",
    }
except ImportError:
    html_theme = "classic"

# Estilo de resaltado de código (para los fragmentos C++/CMake).
pygments_style = "sphinx"

# --- Comportamiento ---------------------------------------------------------
# No ser "nitpicky": los roles cpp:* sin definición no rompen el build.
nitpicky = False

# Mostrar el número de versión en el header (si el tema lo soporta).
html_show_sourcelink = False

# El logo del programa (tools/icono.py): arriba del menu y en la pestaña.
html_logo = "../assets/icons/logo-marca.png"
html_favicon = "../assets/icons/broadcast-appliance-32.png"
