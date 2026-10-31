// ============================================================================
// src/app/escenas/RespaldoEntradas.hpp — qué hacer si una entrada al aire se queda sin señal (modelo)
//
// Por entrada (Config/respaldo-entradas.json): avisar, ARMAR una escena de
// respaldo (para el TAKE) o CORTAR a ella, después de `ms` sin señal. Una sola
// vez por caída: cuando la entrada vuelve, se rearma. Corta y arma por el
// conmutador (con sus reglas). Avisa con señales: la interfaz muestra.
//
// Es del dueño del motor (F3-03): mira la señal de las entradas en el grafo de video y
// corta por el conmutador, los dos del dueño; vive en su hilo (lo adopta la ventanilla) y
// revisa cada segundo con su reloj, con el turno. Así una cámara al aire que se cae se
// cubre aunque la interfaz esté trabada. La interfaz se entera por las señales (la lista
// sin señal viaja en sinSenalCambio); respaldo y setRespaldo leen y escriben el archivo
// y se pueden llamar desde la interfaz.
// ============================================================================
#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>

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

    /// Una vuelta: mira qué entradas no tienen señal y aplica los respaldos (en el dueño,
    /// con el turno; la corre su reloj, cada segundo).
    void revisar();

   signals:
    /// Cambió la lista de entradas sin señal.
    void sinSenalCambio(const QStringList& entradas);
    /// Qué pasó con una entrada al aire sin señal (para mostrarlo).
    void aviso(const QString& texto);

   private:
    nlohmann::json leer() const;

    QTimer reloj_{this}; ///< se muda con el objeto al hilo del dueño
    AppRuntime& rt_;
    video::VideoGrafos& video_;
    Conmutador& conmutador_;
    QStringList sinSenal_;
    QHash<QString, qint64> sinSenalDesde_; ///< entrada -> cuándo se perdió
    QSet<QString> hecho_;                  ///< entradas cuyo respaldo ya se aplicó en esta caída
    QSet<QString> tuvoSenal_;              ///< las que tuvieron señal alguna vez: solo esas «se quedan sin»
};

} // namespace ba::app::escenas
