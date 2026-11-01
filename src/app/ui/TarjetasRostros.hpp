// ============================================================================
// src/app/ui/TarjetasRostros.hpp — los cuadros que mira Personas y las tarjetas «Rostros»
//
// De dónde salen las caras que reconoce la libreta (ui/PersonasViewModel), sin las reglas
// (rostros/QuienEs) ni la vista:
//   - las CÁMARAS DEL ESTUDIO («entrada:<cam>») las mira su tarjeta «Rostros» (el elemento
//     barostros en la cadena de la entrada, ver VideoGrafos): se le pone a las cámaras que
//     se usan (la que mira la página, el anotado, la puerta, las que se siguen) y se saca al
//     dejar de usarlas; lo que ve llega por el bus de la entrada (en el bucle del motor) y
//     se contesta a quien lo esté esperando; a cada tarjeta se le devuelve QUIÉN ES cada
//     cara (las etiquetas, por la ventanilla del motor);
//   - las DEMÁS CLAVES (el programa, un invitado, el lobby) se miran tomando el último cuadro
//     de FrameService y pasándolo al reconocimiento (ui/Reconocedor, en su propio hilo).
// Todo contesta en el hilo de la interfaz.
// ============================================================================
#pragma once

#if BA_HAVE_ROSTROS

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <QImage>
#include <QObject>
#include <QString>

#include "ui/Reconocedor.hpp"

typedef struct _GstSample GstSample; // gst/gst.h

namespace ba::rostros {
class QuienEs;
}

namespace ba::app {

class AppRuntime;
class StudioViewModel;

class TarjetasRostros : public QObject {
    Q_OBJECT
   public:
    /// Las caras de un feed, el cuadro que se miró (para la foto) y el motivo si algo falló.
    using Analisis = std::function<void(std::vector<Reconocedor::Visto>, const QImage&, const QString&)>;

    TarjetasRostros(AppRuntime& rt, StudioViewModel& vm, Reconocedor& rec, rostros::QuienEs& quien,
                    QObject* parent = nullptr);
    ~TarjetasRostros() override;

    /// Las cámaras que Personas usa por su cuenta (el anotado, la puerta): entrada → para qué.
    void setUsos(std::function<std::map<std::string, std::string>()> f) { usos_ = std::move(f); }

    /// Las caras de un feed, miradas FUERA del hilo de la interfaz.
    /// :param descartable: true para una mirada periódica que se puede saltear si ya hay
    ///        otra en vuelo; false para lo que alguien espera.
    void pedirAnalisis(const QString& feed, bool descartable, Analisis luego);
    /// La página mira `feed`: si es una cámara del estudio, lleva su tarjeta mientras tanto.
    void mirando(const QString& feed);
    /// Nadie mira: la cámara del estudio que miraba la página deja de tener su tarjeta.
    void dejarDeMirar();
    /// Una cámara que se sigue (PTZ): lleva su tarjeta mientras `usar`.
    void usarCamara(const QString& entrada, bool usar);
    /// Pone o saca las tarjetas según lo que se usa ahora.
    void sincronizar();
    /// El nombre legible de una clave de video ("Programa", "Cámara · Cam 1").
    QString etiquetaDe(const QString& feed) const;

    /// Lo que vio la tarjeta de una entrada (su mensaje del bus, ya en este hilo).
    void alRostros(const QString& entrada, const std::string& json, GstSample* cuadro);

   signals:
    void error(const QString& texto);

   private:
    AppRuntime& rt_;
    StudioViewModel& vm_;
    Reconocedor& rec_;
    rostros::QuienEs& quien_;
    std::function<std::map<std::string, std::string>()> usos_;
    /// La última cámara (entrada) que pidió la página, y cuándo: su tarjeta se mantiene
    /// mientras se la mire.
    QString feedMirado_;
    std::int64_t miradoMs_ = 0;
    /// Las cámaras que se están siguiendo (Seguidor): llevan su tarjeta mientras tanto.
    std::set<QString> seguidas_;
    /// Quien espera lo próximo que vea la tarjeta de una cámara.
    struct Espera {
        int id = 0;
        Analisis luego;
    };
    std::map<QString, std::vector<Espera>> esperas_;
    int proximaEspera_ = 1;
    int escucha_ = 0; ///< los mensajes de las tarjetas (motor::escucharElementos)
};

} // namespace ba::app

#endif // BA_HAVE_ROSTROS
