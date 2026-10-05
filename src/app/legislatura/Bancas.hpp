// SPDX-License-Identifier: GPL-2.0-or-later
//
// Bancas: la fila de oradores PRESENCIALES (los que hablan desde su banca, sin
// conexión WebRTC). Cada banca sale del Cuerpo (legislatura/Cuerpo): su preset
// PTZ, su micrófono, su nombre y cargo validados.
//
// Poner una banca al aire es UNA acción:
//   1. la PTZ va al preset de la banca, SOLO si esa cámara no está al aire
//      (interlock: si está, no se mueve y se avisa);
//   2. la escena que muestra esa cámara queda ARMADA en el preview (modo
//      «armar», de fábrica; sale con TAKE) o se corta («directo»);
//   3. el zócalo toma nombre, cargo y bloque de la persona ({orador});
//   4. arranca el reloj de su tiempo;
//   5. se abre el micrófono de su banca y se cierra el de la anterior.
// Todo lo hace una persona (el operador o lo que él pidió): lo automático pasa
// por las guardas de aire del MCP.
#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <vector>

namespace ba::app::legislatura {

class Cuerpo;

class Bancas : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap estado READ estado NOTIFY cambio)
   public:
    /// Lo que Bancas necesita del resto de la app.
    struct Ganchos {
        std::function<bool(const std::string& camara)> camaraAlAire;
        std::function<std::string(const std::string& camara, const std::string& posicion)> irAPosicion; ///< "" o error
        std::function<std::string(const std::string& camara)> escenaDe; ///< la escena que muestra esa cámara ("" si ninguna)
        std::function<void(const std::string& escena)> armar;
        std::function<void(const std::string& escena)> cortar;
        std::function<void(const std::string& fuente, bool abierto)> microfono;
    };
    Bancas(Cuerpo& cuerpo, Ganchos g, QObject* parent = nullptr);

    /// {cola: [integrantes], al_aire: integrante|null, desde_ms, usado_s, asignado_s, bancas: [todas]}.
    QVariantMap estado() const;
    nlohmann::json estadoJson() const;

    /// Suma a la fila (por banca, id o nombre). "" o el error.
    Q_INVOKABLE QString agregar(const QString& clave);
    Q_INVOKABLE void quitar(const QString& clave);
    /// La pone al aire: `modo` "armar" (de fábrica) o "directo". Devuelve "" o el
    /// error; los avisos (cámara al aire, sin preset) van en `aviso`.
    Q_INVOKABLE QString alAire(const QString& clave, const QString& modo = "armar");
    /// Saca a la del aire (cierra su micrófono y para el reloj).
    Q_INVOKABLE void terminar();
    /// Solo la cámara: lleva la PTZ al preset de la banca (para tenerla lista),
    /// sin armar ni abrir micrófonos. No mueve la cámara que está al aire.
    /// "" o el error.
    Q_INVOKABLE QString apuntar(const QString& clave);
    /// La primera de la fila al aire.
    Q_INVOKABLE QString siguiente(const QString& modo = "armar");
    Q_INVOKABLE void setAsignado(int segundos) { asignadoS_ = segundos; emit cambio(); }
    Q_INVOKABLE QString ultimoAviso() const { return QString::fromStdString(aviso_); }

    /// El orador presencial al aire para los zócalos ({nombre, cargo, bloque, validado}); {} si no hay.
    nlohmann::json oradorParaZocalo() const;

   signals:
    void cambio();

   private:
    Cuerpo& cuerpo_;
    Ganchos g_;
    std::vector<std::string> cola_; ///< ids de integrantes
    std::string alAire_;           ///< id del que habla
    long long desdeMs_ = 0;
    int asignadoS_ = 180;
    std::string aviso_;
};

} // namespace ba::app::legislatura
