// ============================================================================
// src/app/escenas/ControlGrillas.hpp — la grilla de personas, andando (modelo)
//
// Junta lo que hace falta para decidir quién va en cada «Grilla de personas»
// (fuentes ba_grilla) y se lo manda al gateway, que pinta los mosaicos:
//   - la gente: los admitidos de la Sala (snapshot del gateway) y los
//     integrantes habilitados de la Asamblea (asamblea::SesionRemota), con la
//     prioridad del integrante (Cuerpo, campo «prioridad»);
//   - quién habla: el evento «hablan» del gateway (mide el audio de cada uno) y,
//     en la asamblea, quien tiene la palabra;
//   - quién ya está al aire en otra fuente: lo que se ve en la escena del
//     programa (el lugar de un invitado, el canal del remoto con la palabra, la
//     banca con la palabra);
//   - las grillas: las fuentes ba_grilla con sus ajustes. A cada una le asigna su
//     canal BA.Grilla.<k> (el primero libre) y lo guarda en sus ajustes.
// La lógica (reparto, lugares estables, disposición) es grilla::Repartidor; acá
// solo se juntan los datos y se manda el resultado cuando cambia.
//
// Lo usan la ventana Propiedades (los ajustes de la fuente) y el MCP
// (grilla_personas_estado / grilla_personas_ajustar), por el mismo camino.
// ============================================================================
#pragma once

#include "escenas/GrillaPersonas.hpp"

#include <QObject>
#include <QTimer>
#include <QVariantList>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace ba::app {
class BroadcastEngine;
class ConferenceClient;
}
namespace ba::app::asamblea {
class Cuerpo;
class SesionRemota;
}

namespace ba::app::escenas {

class ControlGrillas : public QObject {
    Q_OBJECT
    /// [{fuente, canal, grupo, personas: [{clave, nombre}], mas}]: lo que muestra cada grilla.
    Q_PROPERTY(QVariantList grillas READ grillasUi NOTIFY cambio)

   public:
    using json = nlohmann::json;
    /// Lo que usa (todo vive más que este modelo). `sesion` y `cuerpo` pueden faltar.
    struct Partes {
        ConferenceClient& sala;
        BroadcastEngine& motor;
        asamblea::SesionRemota* sesion = nullptr;
        const asamblea::Cuerpo* cuerpo = nullptr;
    };
    explicit ControlGrillas(Partes p, QObject* parent = nullptr);

    /// {grillas: [{fuente, canal, grupo, origen, orden, tope, personas, mas}], gente: [...]}.
    json estado() const { return estado_; }
    QVariantList grillasUi() const;
    /// Cambia ajustes de una grilla (validados): "" o el error.
    std::string ajustar(const std::string& fuente, const json& cambios);
    /// Las fuentes ba_grilla que hay (nombres).
    std::vector<std::string> fuentes() const;

   signals:
    void cambio();

   public slots:
    /// Junta todo, reparte y, si cambió, se lo manda al gateway.
    void recalcular();

   private:
    std::vector<grilla::Persona> gente(std::int64_t ahora);
    std::vector<grilla::Config> configs();

    Partes p_;
    grilla::Repartidor rep_;
    std::set<std::string> hablan_;            ///< lo que dijo el gateway
    std::map<std::string, std::int64_t> voz_; ///< clave → la última vez que habló
    json ultimoPedido_;                       ///< lo último que se le mandó al gateway
    json estado_ = json::object();
    QTimer reloj_;  ///< el programa y las fuentes cambian por muchos caminos: se mira seguido
    QTimer pronto_; ///< junta varios avisos seguidos en una sola vuelta
};

} // namespace ba::app::escenas
