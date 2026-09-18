// ============================================================================
// src/obs-module/ba_placeholder.cpp — esqueleto del modulo sin libobs
//
// Proposito:
//   `ba-sources` es una biblioteca compartida y necesita al menos un simbolo
//   para enlazar. Cuando libobs NO esta disponible (un entorno sin
//   `extern/obs-linux`, o un cross-build de validacion), el contenido real del
//   modulo no se compila y este archivo es lo unico que queda: mantiene el
//   target vivo para que el arbol siga compilando y el resto del proyecto se
//   pueda validar.
//
//   Con libobs presente este archivo se excluye y el modulo se compila de
//   verdad (ba_module.cpp + ba_ipc_sources.cpp + ba_dsp_sources.cpp).
// ============================================================================

/// Simbolo minimo del esqueleto. No hace nada: existir es su unica funcion.
///
/// :returns: 0 siempre.
extern "C" int ba_sources_placeholder() { return 0; }
