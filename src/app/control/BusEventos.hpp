// SPDX-License-Identifier: GPL-2.0-or-later
//
// BusEventos: lo que pasa en la app, por temas jerárquicos, para que lo usen
// las superficies de control (la botonera de la tablet, un Stream Deck), los
// plugins, los disparadores y el asistente (MCP).
//
// Temas (los publica la app; los de los plugins van bajo plugin/<id>/<canal>):
//   aire/escena, aire/armada, aire/tally          lo que está al aire y armado
//   salida/stream, salida/grabacion               transmisión y grabación
//   audio/nivel/<fuente> (10 Hz como mucho)       pico, volumen y mudo
//   audio/loudness                                M/S/I, true peak, LRA (1 Hz)
//   alarma/<id>                                   alarmas de los disparadores
//   entrada/senal                                 entradas sin señal
//   oradores/al_aire                              la banca o el orador al aire
//   sala/participantes                            la sala de invitados
//
// Cada tema guarda su último valor (para que el que se conecta tarde se ponga
// al día). Publicar el mismo valor no avisa a nadie.
//
// Patrones de suscripción: «+» o «*» es un nivel cualquiera; «#» (o «*» al
// final) es todo lo que sigue. «aire/#», «audio/nivel/+», «#».
#pragma once

#include <QObject>
#include <QString>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>

namespace ba::app::control {

class BusEventos : public QObject {
    Q_OBJECT
   public:
    using json = nlohmann::json;
    using Oyente = std::function<void(const std::string& tema, const json& valor)>;

    explicit BusEventos(QObject* parent = nullptr);

    /// Publica (si cambió o si `siempre`). Desde el hilo de la interfaz.
    void publicar(const std::string& tema, const json& valor, bool siempre = false);
    /// {tema, valor, ms, seq} o null.
    json ultimo(const std::string& tema) const;
    /// Los temas que coinciden con el patrón (todos si vacío), con su último valor.
    json temas(const std::string& patron = {}) const;
    /// Los eventos con seq > `desde` que coinciden con alguno de `patrones`.
    json desde(std::uint64_t desde, const std::vector<std::string>& patrones) const;
    std::uint64_t seq() const;

    int suscribir(const std::string& patron, Oyente fn);
    void desuscribir(int id);

    static bool coincide(const std::string& patron, const std::string& tema);

    /// Los últimos para la vista (la pestaña Eventos): [{tema, valor, hora}].
    /// Lo último publicado, lo más nuevo primero. Sin filtro ni `conMediciones`, sin las
    /// mediciones (setMediciones): se publican cada segundo y tapaban los hechos.
    Q_INVOKABLE QVariantList recientesUi(const QString& filtro = {}, bool conMediciones = false) const;
    /// Los temas que son mediciones (audio/nivel/#, audio/loudness…), no hechos.
    void setMediciones(std::vector<std::string> patrones);
    /// Las de fábrica: lo que se publica muchas veces por segundo (o cada segundo) y no es un hecho.
    static std::vector<std::string> medicionesDeFabrica() {
        return {"audio/nivel/#", "audio/loudness", "oradores/tiempo", "salida/salud", "aire/tally"};
    }
    Q_INVOKABLE QVariantList temasUi(const QString& filtro = {}) const;

   signals:
    void publicado(const QString& tema);

   private:
    mutable std::mutex mu_;
    std::vector<std::string> mediciones_; ///< ver setMediciones
    struct Entrada {
        json valor;
        std::int64_t ms = 0;
        std::uint64_t seq = 0;
    };
    std::map<std::string, Entrada> ultimos_;
    std::deque<json> recientes_; ///< {tema, valor, ms, seq}
    std::uint64_t seq_ = 0;
    std::map<int, std::pair<std::string, Oyente>> oyentes_;
    int proximo_ = 1;
};

} // namespace ba::app::control
