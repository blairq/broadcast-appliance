// ============================================================================
// src/app/escenas/RespaldoEntradas.hpp — qué hacer si una entrada al aire se queda sin señal (modelo)
//
// Por entrada (Config/respaldo-entradas.json): avisar, ARMAR una escena de
// respaldo (para el TAKE) o CORTAR a ella, después de `ms` sin señal. Una sola
// vez por caída y por salida: cuando la entrada vuelve, se rearma. Mira el programa de
// CADA salida que sale y corta y arma por el conmutador de esa salida (con sus reglas).
// Avisa con señales: la interfaz muestra.
//
// Es del dueño del motor (F3-03): mira la señal de las entradas en el grafo de video y
// corta por el conmutador, los dos del dueño; vive en su hilo (lo adopta la ventanilla) y
// revisa cada segundo con su reloj, con el turno. Así una cámara al aire que se cae se
// cubre aunque la interfaz esté trabada. La interfaz se entera por las señales (la lista
// sin señal viaja en sinSenalCambio). Lo configurado se lee del archivo UNA vez y vive en
// memoria (lo cambia setRespaldo, que también lo guarda): respaldo, todos y setRespaldo se
// pueden llamar desde cualquier hilo (con candado).
// ============================================================================
#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <mutex>

namespace ba::app {
class AppRuntime;
}
namespace ba::app::video {
class VideoGrafos;
}

namespace ba::app::escenas {

class Conmutadores;

class RespaldoEntradas : public QObject {
    Q_OBJECT
   public:
    RespaldoEntradas(AppRuntime& rt, video::VideoGrafos& video, Conmutadores& conmutadores, QObject* parent = nullptr);

    /// El respaldo de una entrada: {escena, al_perder_aire, ms} o {}.
    nlohmann::json respaldo(const std::string& entrada) const;
    /// Los de todas: {entrada: {escena, al_perder_aire, ms}}.
    nlohmann::json todos() const;
    /// `al_perder_aire`: avisar, armar o cortar; `ms` de 200 a 60000. "" o por qué no.
    std::string setRespaldo(const std::string& entrada, const std::string& escena, const std::string& alPerderAire,
                            int ms);

    /// Una vuelta: mira qué entradas no tienen señal y aplica los respaldos (en el dueño,
    /// con el turno; la corre su reloj, cada segundo).
    void revisar();

   signals:
    /// Cambió la lista de entradas sin señal.
    void sinSenalCambio(const QStringList& entradas);
    /// Qué pasó con una entrada al aire sin señal (para mostrarlo).
    void aviso(const QString& texto);
    /// Se guardó el respaldo de una entrada (desde donde sea: la vista, el MCP).
    void respaldosCambio();

   private:
    std::filesystem::path archivo() const;

    QTimer reloj_{this}; ///< se muda con el objeto al hilo del dueño
    AppRuntime& rt_;
    video::VideoGrafos& video_;
    Conmutadores& conmutadores_;
    mutable std::mutex mu_;
    nlohmann::json respaldos_ = nlohmann::json::object(); ///< lo configurado, por entrada (con mu_)
    QStringList sinSenal_;
    QHash<QString, qint64> sinSenalDesde_; ///< entrada -> cuándo se perdió
    QSet<QString> hecho_;                  ///< salida + entrada cuyo respaldo ya se aplicó en esta caída
    QSet<QString> tuvoSenal_;              ///< las que tuvieron señal alguna vez: solo esas «se quedan sin»
};

} // namespace ba::app::escenas
