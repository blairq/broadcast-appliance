// ============================================================================
// src/app/evento/TiposDeCue.hpp — los tipos de cue y lo que usa cada uno: UNA lista
//
// El formulario de un cue (la página Guion), la validación de `guion_cue` (el MCP) y su
// esquema salen de acá. Antes había dos tablas y ya decían cosas distintas.
//
//   campos     lo que se ve en el formulario, en su orden
//   avanzados  lo que el cue también usa al ejecutarse, pero se pone de otra forma (los
//              `valores` de un gráfico van por sus parámetros) o lo escribe el asistente
//
// Sin Qt: los textos van marcados para traducir (contexto «evento») y se traducen al
// mostrarlos.
// ============================================================================
#pragma once

#include <map>
#include <string>
#include <vector>

namespace ba::evento {

struct TipoDeCue {
    const char* tipo;
    const char* titulo; ///< sin traducir (contexto «evento»)
    const char* ayuda;  ///< ídem
    std::vector<std::string> campos;
    std::vector<std::string> avanzados;
};

/// Todos, en el orden del formulario (el mismo de evento::Tipo).
const std::vector<TipoDeCue>& tiposDeCue();
/// Los nombres, en ese orden.
std::vector<std::string> nombresDeTipos();
/// Lo que usa cada tipo en `datos` (campos y avanzados).
std::map<std::string, std::vector<std::string>> camposPorTipo();

} // namespace ba::evento
