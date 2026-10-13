// ============================================================================
// src/app/escenas/Conmutador.hpp — el conmutador de escenas (modelo)
//
// Qué escena está al aire, cuál está armada (el bus de preview) y cómo se pasa
// de una a otra. Es la mesa de corte: la GUI (botones, T-bar, doble clic), el
// MCP (scene_switch, take, scene_build…), la playlist y el respaldo de entradas
// cambian de escena por acá, y todos con las mismas reglas:
//
//   - la transición de entrada de la escena (si tiene y no se pidió otra);
//   - el audio que sigue al video (AFV): las fuentes marcadas suben con la escena
//     que las tiene y bajan con la que no, en el tiempo de la transición;
//   - después de un TAKE, la que salió queda armada (volver es un solo golpe);
//   - la escena al aire se guarda (vuelve igual tras un corte de luz).
//
// Las transiciones de entrada y el AFV se guardan en Config/transiciones.json.
// Avisa con señales (alAire, armadaCambio, afvCambio): los view models se
// actualizan con ellas; el conmutador no sabe nada de la interfaz.
// ============================================================================
#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace ba::app {
class AppRuntime;
}

namespace ba::app::escenas {

class Conmutador : public QObject {
    Q_OBJECT
   public:
    explicit Conmutador(AppRuntime& rt, QObject* parent = nullptr);

    /// Pone `escena` al aire (corte directo, sin pasar por el preview), con la
    /// transición pedida (`tipo` vacío = la de entrada de la escena o la global).
    /// "" = hecho; si no, por qué no.
    std::string poner(const std::string& escena, const std::string& tipo = {}, int ms = -1);
    /// La arma en el bus de preview. Error si ya está al aire (el TAKE no tendría sentido).
    std::string armar(const std::string& escena);
    /// Pasa la armada al aire; la que sale queda armada. "" o por qué no.
    std::string take(const std::string& tipo = {}, int ms = -1);
    /// La T-bar: `t` de 0 a 1 hacia la armada; `soltar` la termina. "" o por qué no.
    std::string tbar(double t, bool soltar);

    std::string alAire() const;
    std::string armada() const { return armada_; }

    /// La transición con la que entra `escena` cuando no se pide otra.
    /// `tipo`: cut, fade, swipe, slide, fade_to_color; vacío = la global.
    std::string setTransicionEntrada(const std::string& escena, const std::string& tipo, int ms);
    nlohmann::json transicionEntrada(const std::string& escena) const; ///< {tipo, ms} o {}

    /// Audio que sigue al video: la fuente sube con la escena que la tiene.
    void setAfv(const std::string& fuente, bool sigue);
    bool afv(const std::string& fuente) const { return afv_.contains(QString::fromStdString(fuente)); }
    std::vector<std::string> afvFuentes() const;

   signals:
    /// Cambió la escena al aire (ya guardada).
    void alAireCambio(const QString& escena);
    /// Cambió la escena armada (vacía = ninguna).
    void armadaCambio(const QString& escena);
    void afvCambio();

   private:
    /// La parte común de poner/take: transición de entrada, AFV, guardar y avisar.
    std::string cortar(const std::string& escena, const std::string& tipo, int ms, const char* via);
    void aplicarAfv(const std::string& destino, int ms);
    void setArmadaYAvisar(const std::string& escena);
    void guardar() const;

    AppRuntime& rt_;
    std::string armada_;
    nlohmann::json transicionesEntrada_ = nlohmann::json::object();
    QSet<QString> afv_;
    QHash<QString, float> afvVolumen_; ///< el volumen que tenía cada fuente AFV antes de bajarla
};

} // namespace ba::app::escenas
