// ============================================================================
// src/app/resolucion/ResolucionModel.hpp — el plan de resolución de todas las entradas
//
// Junta lo que publican el motor (la cadena de cada entrada, en orden, con lo que
// negoció cada tarjeta), las escenas (dónde se ve cada entrada y a qué tamaño) y
// los dispositivos (los modos de cada cámara), y arma el plan de cada una
// (resolucion::planear). `niveles` pinta las tarjetas en el analizador de latencia
// y en el constructor de Video (el mismo dato para las dos vistas). Solo lectura:
// no cambia nada (docs/planes/PLAN-RESOLUCION.md, fase 2).
// Se recalcula cada 2 s mientras alguna vista lo mira (mirar(true/false)), y en el
// momento cuando lo pide el MCP.
// ============================================================================
#pragma once

#include "resolucion/Plan.hpp"

#include <QObject>
#include <QTimer>
#include <QVariantMap>

#include <chrono>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ba::app::resolucion {

class ResolucionModel : public QObject {
    Q_OBJECT
    /// El peor nivel de cada tarjeta, con los motivos: {id del nodo del grafo | "entrada:<nombre>":
    /// {nivel: "mejorable"|"mal_armado"|"mal", motivos: [texto]}}. El nivel de la entrada (la
    /// fuente) también queda en el id del nodo «entrada» de su grafo.
    Q_PROPERTY(QVariantMap niveles READ niveles NOTIFY cambio)
    /// El plan de cada entrada (resolucion::planear), por nombre.
    Q_PROPERTY(QVariantMap planes READ planes NOTIFY cambio)

   public:
    struct NodoGrafo {
        std::string id, tipo, etiqueta, grupo;
    };
    struct Fuentes {
        std::function<nlohmann::json()> motor; ///< Motor::instantanea() (con "entradas")
        /// Por escena: lo que se ve y su rectángulo en px del lienzo.
        std::function<std::vector<std::pair<std::string, std::vector<std::pair<std::string, Tam>>>>()> escenas;
        std::function<std::string(const std::string&)> camaraDe; ///< el id del dispositivo de una entrada ("" si no es cámara)
        std::function<std::vector<ba::motor::ModoVideo>(const std::string&)> modosDe; ///< de un dispositivo
        std::function<nlohmann::json(const std::string&)> interior; ///< CatalogoGst::interior de un elemento
        std::function<std::vector<NodoGrafo>()> grafo;
        std::function<Tam()> lienzo;
        std::function<double()> fps;
    };
    explicit ResolucionModel(Fuentes f, QObject* parent = nullptr);

    QVariantMap niveles() const { return niveles_; }
    QVariantMap planes() const { return planesUi_; }
    /// Una vista empieza (true) o deja (false) de mirar: mientras alguien mira se recalcula cada 2 s.
    Q_INVOKABLE void mirar(bool si);
    /// Lo recalcula ahora y devuelve {entradas: {nombre: plan}, niveles}.
    nlohmann::json recalcular();
    /// El plan de una entrada (recalculado ahora). null si no existe.
    nlohmann::json plan(const std::string& entrada);

   signals:
    void cambio();

   private:
    Fuentes f_;
    QTimer reloj_;
    int mirando_ = 0;
    nlohmann::json ultimo_ = nlohmann::json::object();
    QVariantMap niveles_, planesUi_;
    // Los modos de cada cámara (el monitor de dispositivos cuesta: se guardan un rato).
    std::map<std::string, std::pair<std::chrono::steady_clock::time_point, std::vector<ba::motor::ModoVideo>>> modos_;
    std::map<std::string, nlohmann::json> interiores_;
    const std::vector<ba::motor::ModoVideo>& modosDe(const std::string& camara);
    const nlohmann::json& interiorDe(const std::string& elemento);
};

} // namespace ba::app::resolucion
