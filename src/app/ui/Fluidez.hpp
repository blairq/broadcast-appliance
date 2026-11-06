// ============================================================================
// src/app/ui/Fluidez.hpp — ¿los videos se ven parejos? La app se mide sola (modelo)
//
// Cuántos cuadros por segundo no alcanza: 15 que llegan de a dos cada 133 ms se ven a
// saltos. Lo que se ve es el INTERVALO entre un cuadro y el siguiente. Cada tramo del
// camino de un video lo anota siempre (diag/RitmoCuadros.hpp: un entero en un anillo, sin
// candados) y esto lo junta una vez por segundo:
//
//   grilla    por cada celda de cada grilla de personas, lo del gateway (cada 3 s, GET
//             /api/v1/grillas): lo que llegó de la red, lo que salió del decodificador, lo
//             que se guardó y lo que se PINTÓ en el mosaico (más los salteados), contra lo
//             que manda su capa (baja 15 cps, alta 30); y el ritmo de los mosaicos publicados;
//   fuentes   por clave del servicio de cuadros (program, guest:N, …): cada cuánto llega
//             un cuadro nuevo a la interfaz;
//   vistas    por cada vista de video que se ve (VideoItem): cada cuánto toma uno, cada
//             cuánto lo sube para dibujarlo y los de la fuente que nunca tomó (salteados);
//   ventanas  cada cuánto la ventana pone un cuadro en la pantalla (frameSwapped).
//
// «A saltos»: en la ventana de los últimos 128 intervalos, más del kHuecosPct % son huecos
// (más de 1,5 veces el período que debería tener: el de su capa o su tope, o si no se
// sabe, la mediana de la misma fuente) o el p95 pasa de 2 períodos. Para avisar al
// operador tiene que sostenerse kSostenerS segundos (y se va después de kSoltarS parejos):
// un aviso por video que se traba, no ruido. Lo que lee el MCP (video_fluidez), la
// interfaz y la Prueba de sala es `estado()` (los nombres son claves: no se traducen).
// ============================================================================
#pragma once

#include <QObject>
#include <QPointer>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <string>

namespace ba::app {

class ConferenceClient;
class FrameService;

class Fluidez : public QObject {
    Q_OBJECT
    /// estado() para QML (se renueva una vez por segundo).
    Q_PROPERTY(QVariantMap estado READ estadoQml NOTIFY cambio)
    /// Cuántos videos vienen a saltos ahora (sostenido).
    Q_PROPERTY(int aSaltos READ aSaltos NOTIFY cambio)
   public:
    static constexpr double kHuecosPct = 10.0;
    static constexpr int kSostenerS = 5;
    static constexpr int kSoltarS = 10;

    Fluidez(FrameService& frames, ConferenceClient& sala, QObject* parent = nullptr);

    /// Una vez por segundo (StudioViewModel::alSegundo).
    void tic();
    /// Lo último que se juntó (ver arriba).
    const nlohmann::json& estado() const { return estado_; }
    QVariantMap estadoQml() const { return estadoQml_; }
    int aSaltos() const { return aSaltos_; }

   signals:
    void cambio();
    /// Un video empezó a venir a saltos (sostenido) o se arregló.
    void notify(const QString& texto, const QString& nivel);

   private:
    /// La histéresis de cada video (por su clave estable).
    struct Vigia {
        int malos = 0, buenos = 0;
        bool avisado = false;
        bool visto = false; ///< en este tic
    };
    /// Lleva la cuenta de `clave` con lo de este tic; true si está «a saltos» sostenido.
    bool vigilar(const std::string& clave, const std::string& nombre, bool mal);

    FrameService& frames_;
    QPointer<ConferenceClient> sala_;
    nlohmann::json estado_ = nlohmann::json::object();
    nlohmann::json grilla_ = nlohmann::json::array(); ///< lo último del gateway
    QVariantMap estadoQml_;
    int aSaltos_ = 0;
    int tics_ = 0;
    bool pidiendo_ = false;
    std::map<std::string, Vigia> vigias_;
};

} // namespace ba::app
