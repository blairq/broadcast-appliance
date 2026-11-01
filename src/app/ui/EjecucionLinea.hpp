// ============================================================================
// src/app/ui/EjecucionLinea.hpp — lo que cada pieza de la línea le hace al sistema
//
// La corrida (evento/Corrida) no sabe ejecutar nada a propósito y el núcleo de órdenes
// (ui/ConduccionLinea) decide qué pieza toca; acá se cierra la costura: cada pieza se
// traduce a lo que hay que hacerle al sistema —poner una escena en el conmutador de su
// salida, la música, el video, el audio de una mezcla, un gráfico, el carrusel, el
// teleprompter, el orador de la sala, una capa DSK, una PTZ, el orden del día, una
// votación, la playlist, una herramienta del asistente, una pantalla— y se deshace cuando
// la pieza termina. La traducción es HONESTA: una pieza de un tipo que todavía no sabemos
// ejecutar no se hace la que sí — lo dice, queda en la bitácora, y la línea sigue. En vivo,
// un cue que falla en silencio es peor que uno que avisa.
//
// Lo que toca el aire se PIDE al dueño del motor por la ventanilla y lo que se lee del motor
// sale de su foto (los medios que suenan: la parte Medidas, que avisa).
// ============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <QObject>
#include <QString>

#include <nlohmann/json.hpp>

#include "emision/FotoMotor.hpp"
#include "evento/Corrida.hpp"
#include "evento/Evento.hpp"

namespace ba::app {
class AppRuntime;
class CarruselViewModel;
class ConduccionLinea;
class ConferenceViewModel;
class GuionViewModel;
class VentanillaMotor;
namespace graficos {
class Emisor;
class Vinculos;
} // namespace graficos
namespace escenas {
class Conmutador;
} // namespace escenas

class EjecucionLinea : public QObject {
    Q_OBJECT
   public:
    using Herramienta =
        std::function<void(const std::string&, const nlohmann::json&, std::function<void(bool, const std::string&)>)>;

    EjecucionLinea(ConduccionLinea& n, GuionViewModel& guion, AppRuntime& rt, VentanillaMotor& ventanilla,
                   ConferenceViewModel* sala, graficos::Emisor* graficos, QObject* parent = nullptr);
    ~EjecucionLinea() override;

    void setHerramienta(Herramienta fn) { herramienta_ = std::move(fn); }
    void setVinculos(graficos::Vinculos* v) { vinculos_ = v; }
    void setCarruseles(CarruselViewModel* c) { carruseles_ = c; }

    /// Una corrida nueva: lo que hacían las piezas de la anterior ya no es de nadie.
    void reiniciar();
    /// Hace la pieza `z` de la acción `a` en su salida (no en ensayo: eso lo decide el núcleo).
    void pieza(const evento::linea::Pieza& z, const evento::linea::Accion& a, const QString& titulo);
    /// La pieza terminó su vida: se deshace lo que dejó (el gráfico, la música, la capa…).
    void sacar(const evento::linea::Accion& a);
    /// Arma la escena en el PVW de la salida (lo que viene).
    void armar(const std::string& escena, const std::string& salida);
    /// Saca los gráficos «al avanzar» de una salida ("" = de todas).
    void sacarAlAvanzar(const std::string& salida = {});
    /// ¿La escena al aire la puso la línea misma (no fue a mano)? Lo olvida al preguntar.
    bool esDeLaLinea(const std::string& salida, const std::string& escena);
    /// Cambió el aire: una cámara pudo haber salido del aire y las PTZ que la esperaban, ahora.
    void alAireCambio();
    /// La playlist cambió de estado: la pieza cuya playlist terminó sola, o "".
    std::string alPlaylist();
    /// Las PTZ que esperan que su cámara salga del aire.
    nlohmann::json ptzPendientes(const std::function<std::string(int64_t)>& hora) const;
    /// «mover» igual, «esperar» o «saltar» la PTZ de una pieza. "" o por qué no.
    std::string decidirPtz(const std::string& pieza, const std::string& decision);

   private:
    void ejecutarCue(const evento::Cue& c, const std::string& paso, const std::string& clave, const std::string& salida,
                     bool global);
    bool ejecutarPiezaLinea(const evento::linea::Pieza& z, const evento::linea::Accion& a, const QString& titulo);
    void tomarEscena(const std::string& escena, const nlohmann::json& datos, const std::string& salida,
                     std::function<void(const std::string& error)> listo);
    std::optional<std::string> salidaDelMotor(const std::string& salida) const;
    const FotoMotor::Salida* aireDe(const std::string& salida) const;
    void pedirAlConmutador(const char* que, const std::string& salida, std::function<std::string(escenas::Conmutador&)> orden,
                           std::function<void(const std::string& error)> listo);
    void olvidarEscenaDeLaLinea(const std::string& salida, const std::string& escena);
    static std::string prepararEscenaDeVideo(AppRuntime& rt, const std::string& escenaV, const std::string& fuenteV, bool* creada);
    void moverPtz(const std::string& clave, bool forzar);
    /// Los medios de las piezas que terminan con su medio: se miran en la foto del motor
    /// cuando cambia la parte Medidas (solo mientras haya alguno).
    void mirarMedios();
    std::string musicaDe(const std::string& salida) const;
    std::string escenaVideoDe(const std::string& salida) const;
    std::string videoDe(const std::string& salida) const;
    std::string salidaDe(const evento::linea::Accion& a) const;

    struct PtzPendiente {
        std::string salida, paso, pieza, camara, posicion;
        double segundos = -1;
        int64_t desdeMs = 0;
    };

    ConduccionLinea& n_;
    GuionViewModel& guion_;
    AppRuntime& rt_;
    VentanillaMotor& ventanilla_;
    ConferenceViewModel* sala_ = nullptr;
    graficos::Emisor* graficos_ = nullptr;
    graficos::Vinculos* vinculos_ = nullptr;
    CarruselViewModel* carruseles_ = nullptr;
    Herramienta herramienta_;

    std::map<std::string, std::vector<std::pair<std::uint64_t, int>>> alAvanzar_; ///< por salida
    std::map<std::string, std::pair<std::uint64_t, int>> graficosDePieza_;
    std::map<std::string, std::string> musicaDePieza_; ///< salida → clave de la pieza
    std::map<std::string, std::pair<int, std::string>> dskDePieza_;
    std::map<std::string, PtzPendiente> ptzPendientes_;
    std::map<std::string, std::string> mediosVigilados_; ///< pieza → el medio («medio:<pieza>.termino»)
    std::set<std::string> mediosAndando_;
    std::string playlistDePieza_;
    bool playlistAndando_ = false;
    std::map<std::string, std::multiset<std::string>> escenasDeLaLinea_;
};

} // namespace ba::app
