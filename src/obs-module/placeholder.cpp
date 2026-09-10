// ============================================================================
// src/obs-module/placeholder.cpp — PLACEHOLDER de `ba-sources`  [SKELETON]
//
// ⚠️  Este archivo existe SOLO para que el target `ba-sources` (shared lib /
//     DLL de libobs) configure y compile antes de que el agente del módulo
//     aporte la lógica real (spec §7: channel strip nativo, fuentes IPC,
//     automixer; spec §4: flujos de memoria compartida).
//
// `ba-sources` se despliega en obs-plugins/64bit/ (spec §5.1). Un plugin de
// libobs exporta `obs_module_register`/`obs_module_unregister`; aquí se deja
/// un símbolo mínimo exportado para que la shared lib tenga al menos un
/// símbolo y el target compile en ambas plataformas.
//
// Compila en Windows/MSVC y en Linux; todo lo Windows-only va detrás de
// #ifdef _WIN32. No implementa lógica de negocio.
// ============================================================================

/// \brief Símbolo mínimo exportado del módulo (placeholder).
///
/// Garantiza que la shared lib tenga al menos un símbolo exportado. El agente
/// del módulo lo reemplazará por `obs_module_register`/`obs_module_unregister`
/// y los filtros de channel strip (spec §7.2).
///
/// :returns: 0 siempre (placeholder).
extern "C" int ba_sources_placeholder() {
    return 0;
}
