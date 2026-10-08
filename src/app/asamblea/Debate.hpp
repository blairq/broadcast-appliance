// SPDX-License-Identifier: GPL-2.0-or-later
//
// Debate: dos personas frente a frente, moderadas por la presidencia. Pueden ser
// dos de la sala, dos remotos (sesión remota, desde el navegador) o uno y uno.
//
// Iniciarlo es UNA acción:
//   1. termina al orador que estaba al aire (la fila de Bancas queda como estaba);
//   2. si hay alguno de la sala y la central está conectada, la deja en modo
//      operador con dos micrófonos abiertos como máximo (lo que tenía se guarda y
//      vuelve al terminar);
//   3. a los remotos se les da la palabra: su cámara y su micrófono entran a la
//      app por su canal (el de A por el 1 —«Remoto con la palabra»—, el de B por
//      el 2 —«Remoto con la palabra 2»—; con uno solo, por el 1) y la siguen
//      teniendo todo el debate, así su imagen no se corta cuando habla el otro;
//   4. las cámaras de los de la sala van al preset de su banca (si no están al aire);
//   5. la escena configurada para ese tipo de debate (sala-sala, sala-remoto,
//      remoto-remoto) queda armada (o se corta, si así se configuró).
//
// El TURNO lo da la presidencia: «a», «b», «ambos» o «nadie». Al que no le toca
// se le cierra el micrófono (en la central, o su audio en la mezcla y en su
// celular), con un reloj por turno que avisa al último minuto y al terminar
// (no corta solo: decide la presidencia).
#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <string>

namespace ba::app::asamblea {

class Cuerpo;

class Debate : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap estado READ estado NOTIFY cambio)
   public:
    using json = nlohmann::json;
    struct Ganchos {
        /// Habla desde el navegador (conectado y habilitado en la sesión remota).
        std::function<bool(const std::string& id)> esRemoto;
        /// Darle o quitarle la palabra por la sesión remota (en orden: el primero va al canal 1).
        std::function<void(const std::string& id, bool palabra, int segundos)> palabraRemota;
        /// El canal (1 o 2) por el que entra un remoto con la palabra; 0 si no la tiene.
        std::function<int(const std::string& id)> canalRemoto;
        /// Abrir o cerrar una entrada de audio de la mezcla (el audio de un remoto).
        std::function<void(const std::string& fuente, bool abierto)> microfono;
        /// El micrófono de la banca en la central; `terminar` además la saca de los oradores.
        std::function<bool(const std::string& banca, bool abierto, bool terminar)> microfonoBanca;
        /// Su cámara al preset de su banca (sin armar nada). "" o el aviso.
        std::function<std::string(const std::string& id)> apuntar;
        std::function<void(const std::string& escena)> armar;
        std::function<void(const std::string& escena)> cortar;
        /// La central en modo debate (true: guarda sus ajustes y la pone en operador con 2
        /// abiertos) o como estaba (false). Lo resuelve la app; puede tardar.
        std::function<void(bool debate)> central;
        /// Termina al orador al aire (Bancas).
        std::function<void()> terminarOrador;
        /// Un mensaje al celular de un remoto ({type: "debate", ...}).
        std::function<void(const std::string& id, const json& msg)> avisarRemoto;
        /// «ultimo_minuto» | «terminado» en la unidad de la central de su banca.
        std::function<void(const std::string& banca, const std::string& aviso)> avisoCentral;
        std::function<void(const std::string& id, const std::string& aviso)> avisoRemoto;
    };
    Debate(Cuerpo& cuerpo, std::filesystem::path datos, Ganchos g, QObject* parent = nullptr);

    /// {activo, tipo, a: {id, nombre, banca, remoto, canal}, b: {...}, turno, turno_s,
    ///  restante_s, escena, config: {escenas: {sala_sala, sala_remoto, remoto_remoto}, turno_s, directo}}.
    json estadoJson() const;
    QVariantMap estado() const;

    /// `a` y `b`: banca, id o nombre del Cuerpo. `turno` "a" (de fábrica), "b", "ambos" o
    /// "nadie". "" o el error; los avisos (cámara al aire, sin escena) en `aviso`.
    std::string iniciar(const std::string& a, const std::string& b, const std::string& turno = "a");
    std::string turno(const std::string& t);
    /// Le suma (o resta) segundos al turno en curso.
    void sumar(int segundos);
    void terminar();
    bool activo() const { return activo_; }
    bool participa(const std::string& id) const { return activo_ && (id == a_ || id == b_); }

    /// {escenas: {sala_sala, sala_remoto, remoto_remoto}, turno_s, directo}: se mezcla y se guarda.
    std::string configurar(const json& cambios);
    json config() const { return config_; }

    Q_INVOKABLE QString iniciarUi(const QString& a, const QString& b, const QString& turno);
    Q_INVOKABLE QString turnoUi(const QString& t) { return QString::fromStdString(turno(t.toStdString())); }
    Q_INVOKABLE void sumarUi(int s) { sumar(s); }
    Q_INVOKABLE void terminarUi() { terminar(); }
    Q_INVOKABLE QString configurarUi(const QVariantMap& cambios);

   signals:
    void cambio();
    void aviso(const QString& texto, const QString& nivel);

   private:
    json lado(const std::string& id) const;
    std::string tipo() const;
    void aplicarTurno();
    void cargar();
    void guardar() const;
    void tic();

    Cuerpo& cuerpo_;
    std::filesystem::path archivo_;
    Ganchos g_;
    json config_;
    bool activo_ = false;
    std::string a_, b_;
    bool aRemoto_ = false, bRemoto_ = false;
    std::string turno_ = "nadie";
    std::string escena_;
    std::string aviso_;
    long long turnoDesdeMs_ = 0;
    int turnoS_ = 0; ///< el de este turno (con lo que se le sumó)
    bool avisoMinuto_ = false, avisoFin_ = false;
    QTimer reloj_;
};

} // namespace ba::app::asamblea
