// ============================================================================
// src/app/arranque/ModoVivoConfig.hpp — qué protege el modo en vivo, guardado (modelo)
//
// Config/modo-vivo.json: {activo, incluir_grabando, paginas}. Con el modo
// activo, al aire (y grabando si incluir_grabando) las páginas de la lista
// piden confirmar con un motivo. De fábrica: activo, sin contar la grabación,
// y protegidas Configuración, Plugins y Agentes.
// ============================================================================
#pragma once

#include <filesystem>
#include <string>

#include <QStringList>

#include <nlohmann/json.hpp>

namespace ba::app::config {

class ModoVivoConfig {
   public:
    /// Lee lo guardado (sin archivo, lo de fábrica).
    explicit ModoVivoConfig(std::filesystem::path dataRoot);

    bool activo() const { return activo_; }
    bool incluirGrabando() const { return incluirGrabando_; }
    const QStringList& paginas() const { return paginas_; }

    /// Aplica {activo?, incluir_grabando?, paginas?} (lo que no viene, queda) y lo guarda.
    /// "" o el error.
    std::string cambiar(const nlohmann::json& a);

   private:
    void aplicar(const nlohmann::json& a);
    std::filesystem::path archivo_;
    bool activo_ = true;
    bool incluirGrabando_ = false;
    QStringList paginas_{"configuracion", "plugins", "agentes"};
};

} // namespace ba::app::config
