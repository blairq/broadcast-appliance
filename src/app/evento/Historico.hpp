// ============================================================================
// src/app/evento/Historico.hpp — leer el histórico (H6): lo que comparten la página y el MCP
//
// Todo lo de un evento ya está en su carpeta (bitácora, fotos de Config/,
// grabaciones.json, transmisiones.json, asamblea/, registro/, sellos.json).
// Esto lo lee y lo junta, sin Qt, para que la página Histórico y las
// herramientas del MCP digan exactamente lo mismo.
// ============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::evento {

struct Historico {
    using json = nlohmann::json;

    /// Cuántas cosas tiene un evento, para la lista: {grabaciones, transmisiones,
    /// transcripciones, lineas_bitacora (aprox. por tamaño), sellado, acta, bytes}.
    static json resumen(const std::filesystem::path& evento);

    /// Las grabaciones (con capítulos: la escena al aire en cada momento, en
    /// segundos desde el inicio del archivo), los vivos y las transcripciones.
    static json grabaciones(const std::filesystem::path& evento);

    /// Los archivos de la carpeta (ruta relativa, bytes), sin Respaldos/.
    static json archivos(const std::filesystem::path& evento, std::size_t tope = 2000);

    /// Busca `texto` (sin distinguir mayúsculas) en la bitácora, las
    /// transcripciones, la asamblea, las actas y los discursos de cada evento
    /// de `raiz` (Eventos/). [{evento, archivo, linea, texto, t?}], hasta `limite`.
    static json buscar(const std::filesystem::path& raiz, const std::string& texto, std::size_t limite = 200,
                       const std::atomic<bool>* cancelar = nullptr);

    /// Copia el evento a `destino`/<nombre>/ (con o sin Grabaciones/) y los
    /// contenidos de sus fotos de configuración (en config/objetos/), para que
    /// la copia se entienda sola en otra máquina. "" o el error.
    static std::string exportar(const std::filesystem::path& evento, const std::filesystem::path& objetos,
                                const std::filesystem::path& destino, bool conGrabaciones,
                                std::filesystem::path* copia = nullptr);
};

} // namespace ba::evento
