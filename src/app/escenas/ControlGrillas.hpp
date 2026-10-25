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
//   - quién ya está al aire en otra fuente: lo que se ve en el programa —la
//     escena al aire y las capas DSK que están arriba, entrando en las escenas
//     anidadas (grilla::fuentesQueSeVen)—: el lugar de un invitado, el canal del
//     remoto con la palabra, la banca con la palabra y las grillas que se ven
//     (quien está en una de esas no se repite en las demás, ver el Repartidor);
//   - las grillas: las fuentes ba_grilla con sus ajustes. A cada una le asigna su
//     canal BA.Grilla.<k> (el primero libre) y lo guarda en sus ajustes;
//   - lo que el operador decidió a mano (grilla::Manual): fijar a alguien en una
//     grilla, sacarlo, moverlo de una a otra, devolverlo a lo automático. Vive en
//     memoria: las claves son de la conexión de cada uno.
// La lógica (reparto, lugares estables, disposición) es grilla::Repartidor; acá
// solo se juntan los datos y se manda el resultado cuando cambia.
//
// Lo usan la ventana Propiedades (los ajustes de la fuente y quién está en ella:
// VistaGrillas), la Sala y la Asamblea (el resumen) y el MCP
// (grilla_personas_estado / _ajustar / _fijar / _sacar / _mover / _devolver), por el
// mismo camino.
// ============================================================================
#pragma once

#include "escenas/GrillaPersonas.hpp"

#include <QObject>
#include <QString>
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
    /// [{fuente, canal, grupo, al_aire, personas: [{clave, nombre, fijo, hablando}], fuera: [{clave, nombre}],
    /// mas, aviso?}]: lo que muestra cada grilla (y a quién se sacó a mano de cada una).
    Q_PROPERTY(QVariantList grillas READ grillasUi NOTIFY cambio)
    /// [{clave, nombre, origen, con_camara, al_aire, hablando, prioridad, en: [fuentes], fijo_en}]: la gente que puede
    /// aparecer en una grilla.
    Q_PROPERTY(QVariantList gente READ genteUi NOTIFY cambio)

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

    /// {grillas: [{fuente, canal, grupo, origen, orden, tope, al_aire, personas, fuera, mas}], gente: [...]}.
    json estado() const { return estado_; }
    QVariantList grillasUi() const;
    QVariantList genteUi() const;
    /// Cambia ajustes de una grilla (validados): "" o el error.
    std::string ajustar(const std::string& fuente, const json& cambios);
    /// Las fuentes ba_grilla que hay (nombres).
    std::vector<std::string> fuentes() const;

    // --- A mano (la vista y el MCP): "" o el error ------------------------------------
    /// Fija a `clave` en `fuente`: va siempre en esa (primero), aunque los filtros digan otra cosa.
    std::string fijar(const std::string& fuente, const std::string& clave);
    /// Saca a `clave` de `fuente` (y de las grillas de su grupo).
    std::string sacar(const std::string& fuente, const std::string& clave);
    /// La pasa de `desde` a `hacia`: la fija en `hacia` y, si son de grupos distintos, la saca de `desde`.
    std::string mover(const std::string& clave, const std::string& desde, const std::string& hacia);
    /// Vuelve a lo automático en `fuente` (ni fijada ni sacada).
    std::string devolver(const std::string& fuente, const std::string& clave);
    Q_INVOKABLE QString fijarQml(const QString& fuente, const QString& clave) { return QString::fromStdString(fijar(fuente.toStdString(), clave.toStdString())); }
    Q_INVOKABLE QString sacarQml(const QString& fuente, const QString& clave) { return QString::fromStdString(sacar(fuente.toStdString(), clave.toStdString())); }
    Q_INVOKABLE QString moverQml(const QString& clave, const QString& desde, const QString& hacia) {
        return QString::fromStdString(mover(clave.toStdString(), desde.toStdString(), hacia.toStdString()));
    }
    Q_INVOKABLE QString devolverQml(const QString& fuente, const QString& clave) { return QString::fromStdString(devolver(fuente.toStdString(), clave.toStdString())); }

   signals:
    void cambio();

   public slots:
    /// Junta todo, reparte y, si cambió, se lo manda al gateway.
    void recalcular();

   private:
    /// Lo que se ve en el programa: la escena al aire y las capas DSK de arriba, con las anidadas.
    std::vector<std::string> queSeVe() const;
    std::vector<grilla::Persona> gente(std::int64_t ahora, const std::vector<std::string>& seVe);
    std::vector<grilla::Config> configs(const std::vector<std::string>& seVe);
    /// Valida que haya una grilla `fuente` (y, si `clave` no está vacía, que esa persona esté conectada).
    std::string validarMano(const std::string& fuente, const std::string& clave) const;

    Partes p_;
    grilla::Repartidor rep_;
    grilla::Manual manual_;
    std::set<std::string> hablan_;            ///< lo que dijo el gateway
    std::map<std::string, std::int64_t> voz_; ///< clave → la última vez que habló
    json ultimoPedido_;                       ///< lo último que se le mandó al gateway
    json estado_ = json::object();
    QTimer reloj_;  ///< el programa y las fuentes cambian por muchos caminos: se mira seguido
    QTimer pronto_; ///< junta varios avisos seguidos en una sola vuelta
};

} // namespace ba::app::escenas
