// SPDX-License-Identifier: GPL-2.0-or-later
//
// BusEventos: lo que pasa en la app, por temas jerárquicos, para que lo usen
// las superficies de control (la botonera de la tablet, un Stream Deck), los
// plugins, los disparadores y el asistente (MCP).
//
// Temas (los publica la app; los de los plugins van bajo plugin/<id>/<canal>):
//   aire/escena, aire/armada, aire/tally          lo que está al aire y armado (la salida por omisión)
//   aire/medios                                   [{fuente, restante_s}] lo que se reproduce al aire (ídem)
//   aire/dsk                                      las capas DSK, `visible` en la por omisión
//   aire/tbar                                     {hacia, posicion} la T-bar de la por omisión (posicion 0…1)
//   salida/stream, salida/grabacion               transmisión y grabación (la por omisión)
//   salidas                                       {lista: [{id, nombre, color, por_omision}]}
//   aire/<salida>/escena|armada|tally|medios|dsk|tbar  lo mismo, de CADA salida (también la por omisión)
//   salida/<salida>/estado                        {id, nombre, color, por_omision, escena, armada,
//                                                  transmitiendo, grabando}
//   salida/<salida>/stream, salida/<salida>/grabacion  {activo, …} de cada salida
//   audio/nivel/<fuente> (10 Hz como mucho; A PEDIDO)  pico, volumen y mudo (el fader de la por omisión)
//   audio/<salida>/nivel/<fuente> (A PEDIDO)      lo mismo con el fader y el mudo de esa salida
//   audio/loudness                                M/S/I, true peak, LRA (1 Hz)
//   alarma/<id>                                   alarmas de los disparadores
//   entrada/senal                                 entradas sin señal
//   oradores/al_aire                              la banca o el orador al aire
//   sesion/punto, sesion/votacion                 el punto en tratamiento; el resultado oficial
//   votacion/estado                               {estado: sin|abierta|pausada|cerrada, numero, tema}
//   votacion/abierta, votacion/cerrada            true/false (las condiciones de la línea de tiempo)
//   sala/participantes                            la sala de invitados
//   linea/<salida>/estado                        la línea de tiempo de esa salida: {estado, hold,
//                                                  alAire{paso, nombre}, proximo{paso, nombre,
//                                                  automatico, entraSoloEnSeg}, overUnderSeg,
//                                                  proximoHardStart?} (LineaViewModel)
//   linea/<salida>/proximo                        solo el próximo (o null)
//   linea/<salida>/overunder                      {seg, fin, finPlaneado, corrida} (cada segundo
//                                                  mientras se pasa: una medición)
//   linea/<salida>/propuesta                      lo que propone el productor para llegar a
//                                                  horario (ui/LineaProductor), o null
//
// Cada tema guarda su último valor (para que el que se conecta tarde se ponga
// al día). Publicar el mismo valor no avisa a nadie.
//
// Muchas salidas (docs/planes/PLAN-SALIDAS.md): los temas sin id siguen siendo
// los de la salida por omisión, así lo que existe no se rompe. Una superficie que
// maneja otra salida lee los mismos temas pasados por temaDeSalida.
//
// Patrones de suscripción: «+» o «*» es un nivel cualquiera; «#» (o «*» al
// final) es todo lo que sigue. «aire/#», «audio/nivel/+», «#».
//
// A PEDIDO: lo que se mide sin parar (los niveles) se publica solo mientras alguien lo
// quiere, para que la app quieta no trabaje (ni despierte a la interfaz) para nadie.
// Lo quiere quien lo declara con `interesar` y quien se suscribe con un patrón que no
// sea el de todo; los que escuchan todo («#»: los que reenvían o anotan, como los
// plugins, la bitácora o la botonera) declaran aparte lo que de verdad miran. Quien
// produce pregunta `hayInteres`/`hayInteresEn` y se entera por `interesCambio`; al
// dejar de publicar, `olvidar` saca los valores viejos (nadie lee un nivel de antes
// como si fuera de ahora).
#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariant> // QVariantList de los Q_INVOKABLE: el moc necesita el tipo completo

#include <nlohmann/json.hpp>

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <set>
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

    /// Quiere lo que caiga en `patron` (sin oyente: lee con `ultimo`). Devuelve el id.
    int interesar(const std::string& patron);
    void desinteresar(int id);
    /// Si alguien quiere el tema `tema` (concreto).
    bool hayInteres(const std::string& tema) const;
    /// Si alguien quiere algún tema de la familia `familia` (un patrón: «audio/nivel/#»).
    bool hayInteresEn(const std::string& familia) const;
    /// Olvida los últimos valores de los temas que caen en `patron`.
    void olvidar(const std::string& patron);

    static bool coincide(const std::string& patron, const std::string& tema);
    /// Si algún tema cae en los dos patrones a la vez.
    static bool seCruzan(const std::string& a, const std::string& b);

    /// Los últimos para la vista (la pestaña Eventos): [{tema, valor, hora}].
    /// Lo último publicado, lo más nuevo primero. Sin filtro ni `conMediciones`, sin las
    /// mediciones (setMediciones): se publican cada segundo y tapaban los hechos.
    Q_INVOKABLE QVariantList recientesUi(const QString& filtro = {}, bool conMediciones = false) const;
    /// Los temas que son mediciones (audio/nivel/#, audio/loudness…), no hechos.
    void setMediciones(std::vector<std::string> patrones);
    /// Las de fábrica: lo que se publica muchas veces por segundo (o cada segundo) y no es un hecho.
    static std::vector<std::string> medicionesDeFabrica() {
        return {"audio/nivel/#", "audio/+/nivel/#", "audio/loudness", "oradores/tiempo", "salida/salud", "aire/tally",
                "aire/+/tally", "aire/tbar", "aire/+/tbar", "linea/+/overunder"};
    }
    /// El tema de `tema` para la salida `salida` (su id): «aire/tally» → «aire/<salida>/tally»
    /// (lo mismo con escena, armada, medios, dsk, tbar, salida/stream y salida/grabacion),
    /// «audio/nivel/<f>» → «audio/<salida>/nivel/<f>» y «{{salida}}» → el
    /// id. Si `porOmision`, los temas sin id ya son los suyos y quedan como están. Así
    /// una celda hecha para «la» salida sigue a la que maneje su superficie.
    static std::string temaDeSalida(const std::string& tema, const std::string& salida, bool porOmision);
    /// Publica los temas de cada salida (ver arriba). `salidas`: [{id, nombre, color,
    /// por_omision, escena, armada, fuentes_programa, fuentes_preview, transmitiendo,
    /// grabando, reconectando, medios?, dsk?, tbar?}]. Los de una salida que ya no está se
    /// olvidan (también sus niveles, que se publican a pedido aparte).
    void publicarSalidas(const json& salidas);
    Q_INVOKABLE QVariantList temasUi(const QString& filtro = {}) const;

   signals:
    void publicado(const QString& tema);
    /// Cambió quién quiere qué (interesar, desinteresar, suscribir, desuscribir).
    void interesCambio();

   private:
    /// Lo que cuenta como interés de una suscripción (no el patrón de todo).
    static bool esDeTodo(const std::string& patron) { return patron.empty() || patron == "#" || patron == "*"; }
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
    std::map<int, std::string> intereses_; ///< ver interesar
    int proximo_ = 1;
    std::set<std::string> salidasPublicadas_; ///< ver publicarSalidas
};

/// Lo que una pieza quiere del bus, como conjunto (los temas que lee una página de la
/// botonera, lo que escuchan los plugins): `poner` declara lo nuevo y suelta lo que ya no.
/// Al destruirse suelta todo. Del hilo de la interfaz, como el bus.
class InteresEnBus {
   public:
    explicit InteresEnBus(BusEventos* bus = nullptr) : bus_(bus) {}
    ~InteresEnBus() { poner({}); }
    InteresEnBus(const InteresEnBus&) = delete;
    InteresEnBus& operator=(const InteresEnBus&) = delete;
    /// Cambia de bus (suelta lo del anterior).
    void setBus(BusEventos* bus) {
        poner({});
        bus_ = bus;
    }
    void poner(const std::set<std::string>& patrones) {
        for (auto it = ids_.begin(); it != ids_.end();)
            if (!patrones.count(it->first)) {
                if (bus_) bus_->desinteresar(it->second);
                it = ids_.erase(it);
            } else {
                ++it;
            }
        if (!bus_) return;
        for (const std::string& p : patrones)
            if (!ids_.count(p)) ids_[p] = bus_->interesar(p);
    }

   private:
    QPointer<BusEventos> bus_;
    std::map<std::string, int> ids_;
};

} // namespace ba::app::control
