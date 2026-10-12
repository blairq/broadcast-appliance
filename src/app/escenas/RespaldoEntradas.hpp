// ============================================================================
// src/app/escenas/RespaldoEntradas.hpp — qué hacer si una entrada al aire se queda sin señal (modelo)
//
// Por entrada (Config/respaldo-entradas.json): avisar, ARMAR una escena de
// respaldo (para el TAKE) o CORTAR a ella, después de `ms` sin señal. Una sola
// vez por caída: cuando la entrada vuelve, se rearma. Corta y arma por el
// conmutador (con sus reglas). Avisa con señales: la interfaz muestra.
// ============================================================================
#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <nlohmann/json.hpp>

namespace ba::app {
class AppRuntime;
}
namespace ba::app::video {
class VideoGrafos;
}

namespace ba::app::escenas {

class Conmutador;

class RespaldoEntradas : public QObject {
    Q_OBJECT
   public:
    RespaldoEntradas(AppRuntime& rt, video::VideoGrafos& video, Conmutador& conmutador, QObject* parent = nullptr);

    /// El respaldo de una entrada: {escena, al_perder_aire, ms} o {}.
    nlohmann::json respaldo(const std::string& entrada) const;
    /// `al_perder_aire`: avisar, armar o cortar; `ms` de 200 a 60000. "" o por qué no.
    std::string setRespaldo(const std::string& entrada, const std::string& escena, const std::string& alPerderAire,
                            int ms);

    /// Una vuelta: mira qué entradas no tienen señal y aplica los respaldos.
    void revisar();
    QStringList sinSenal() const { return sinSenal_; }

   signals:
    /// Cambió la lista de entradas sin señal.
    void sinSenalCambio(const QStringList& entradas);
    /// Qué pasó con una entrada al aire sin señal (para mostrarlo).
    void aviso(const QString& texto);

   private:
    nlohmann::json leer() const;

    AppRuntime& rt_;
    video::VideoGrafos& video_;
    Conmutador& conmutador_;
    QStringList sinSenal_;
    QHash<QString, qint64> sinSenalDesde_; ///< entrada -> cuándo se perdió
    QSet<QString> hecho_;                  ///< entradas cuyo respaldo ya se aplicó en esta caída
};

} // namespace ba::app::escenas
