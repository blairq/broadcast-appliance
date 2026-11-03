// ============================================================================
// src/app/sala/ResumenSala.hpp — la foto de la sala, procesada FUERA de la interfaz
//
// El gateway (B) manda la foto entera de la sala varias veces por segundo («room»: cada
// participante con su red, el chat, los archivos, el uso). Con 100+ invitados, procesarla
// en el hilo de la interfaz (las listas, los oradores, la foto entera a JSON y de vuelta,
// comparaciones de todo contra todo) trababa la interfaz 3, 13, 26 s (la PC de pruebas, el
// 2026-11-02: «corría ConferenceClient, evento 43»).
//
// Ahora la foto se procesa en el hilo del lector de eventos (ConferenceClient/LectorSala):
//
//   - `ResumenSala::construir`: lo que la vista y los modelos derivan de la foto (las filas
//     de cada lista, ya con solo sus roles; los nombres por lugar; los destinos del chat; el
//     uso; el monitoreo del lobby; la misma foto en nlohmann::json para las reglas y el
//     vigía). Pura.
//   - `ResumenSala::diferencia`: contra lo que la interfaz YA TIENE (la base: el último que
//     tomó), qué partes cambiaron (`Parte`) y, por lista, el plan de cambios por fila y rol
//     (JsonListModel::planear). A la interfaz le llega un paquete chico: aplica los pasos
//     (proporcional a lo que cambió) y avisa solo las partes que cambiaron.
//   - `BuzonSala`: LA ÚLTIMA GANA. Si llega una foto mientras la interfaz no tomó la
//     anterior, la anterior se descarta (su diferencia se recalcula contra la base): nunca
//     se encolan fotos en el hilo de la interfaz. Lo de un solo disparo que trae una foto
//     (B nueva: republicar) se acumula en la que queda.
//
// La red de cada invitado (kbps, pérdida, RTT: cambia en cada foto) va en los lugares a lo
// sumo una vez por segundo (`kRedCadaMs`); el semáforo (el color), en cuanto cambia.
//
// Solo Qt Core y nlohmann: se prueba sin interfaz (tests/test_resumen_sala.cpp).
// ============================================================================
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "ui/JsonListModel.hpp"

namespace ba::app::sala {

/// Lo que puede cambiar de una foto a otra (cada oyente mira las suyas).
enum Parte : unsigned {
    Sala = 1u << 0,          ///< room, pin, guestUrl, lobby, locked, maxGuests, slotsFree, entrando, entradaPorSegundo
    Gente = 1u << 1,         ///< quién está y cómo (todo de cada participante salvo la red y el uso) y la fila
    Espera = 1u << 2,        ///< la lista del lobby
    Adentro = 1u << 3,       ///< la lista de los de adentro
    Lugares = 1u << 4,       ///< los lugares de invitado (incluye la red, a lo sumo 1 Hz)
    Oradores = 1u << 5,      ///< la fila de oradores y los que están al aire
    OradoresCfg = 1u << 6,   ///< la configuración del modo oradores
    Archivos = 1u << 7,      ///< los archivos compartidos
    Chat = 1u << 8,          ///< mensajes nuevos (o el historial se reinició)
    Uso = 1u << 9,           ///< el uso (gente, video, CPU) y el reloj de la sala
    Monitor = 1u << 10,      ///< a quién del lobby se escucha
    Participantes = 1u << 11, ///< cuántos, los nombres por lugar, los destinos del chat
    Directa = 1u << 12,      ///< la entrada directa (prueba de sala)
    Todo = 0xFFFFu,
};

/// Las claves (roles) de cada lista de la vista, en su orden (ConferenceViewModel).
const QStringList& clavesEspera();
const QStringList& clavesAdentro();
const QStringList& clavesArchivos();
const QStringList& clavesOradores();
const QStringList& clavesLugares();

/// La red de los lugares se refresca a lo sumo cada tanto (ms).
inline constexpr std::int64_t kRedCadaMs = 1000;
/// El reloj de la sala se corrige solo si se movió más que esto (ms): la foto llega con
/// demora variable y cada diferencia chica despertaba al uso.
inline constexpr double kRelojTolMs = 250;

struct ResumenSala {
    QJsonObject foto; ///< la foto entera, como llegó (quien la necesita entera la lee de acá, a pedido)
    std::shared_ptr<const nlohmann::json> json; ///< la misma, para las reglas y el vigía de alarmas

    // Las filas de cada lista (solo sus roles y el id).
    QJsonArray espera, adentro, archivos, oradores, lugares;
    int nLugares = 0; ///< con cuántos lugares se armó `lugares`

    QJsonArray gente; ///< los participantes sin lo que cambia siempre (red, usedMs), y la fila
    QVariantList firmaSala;
    QVariantMap speakerCfg;
    bool speakerMode = false;
    QStringList slotNames;
    QVariantList chatTargets;
    int conectados = 0; ///< peopleConnected
    QVariantMap uso;
    double clockOffset = 0;
    QVariantList onAir;
    std::vector<bool> monitor; ///< [0..8]: lobby k se escucha
    QJsonArray chat;
    qint64 ultimoSeq = 0;
    std::int64_t redMs = 0; ///< cuándo se tomó la red de los lugares
    bool directa = false;
    /// Lo que va al bus de eventos (sala/participantes): id, nombre, estado, mic, cam, mano.
    nlohmann::json participantesBus = nlohmann::json::array();

    struct Opciones {
        int nLugares = 0;
        std::int64_t ahoraMs = 0;
        /// La URL de la foto de un participante (las tarjetas de oradores); vacía si no hay.
        std::function<QString(const QString& id)> avatar;
    };
    /// Lo derivado de `foto`. `anterior` (el último armado, puede ser nulo) da la red y el
    /// reloj si todavía no toca refrescarlos.
    static std::shared_ptr<const ResumenSala> construir(const QJsonObject& foto, const ResumenSala* anterior, const Opciones& o);
};

/// Lo que le llega a la interfaz: lo nuevo y cómo pasar a eso desde lo que tiene.
struct PaqueteSala {
    std::shared_ptr<const ResumenSala> r;
    unsigned partes = 0;
    PlanFilas espera, adentro, archivos, oradores, lugares;
    QJsonArray chatNuevos;     ///< los mensajes que no tenía (en orden)
    bool chatReinicio = false; ///< la secuencia volvió atrás: B se reinició, el historial se reemplaza
    /// Los que llegaron al lobby (no estaban en la base): {id, name, prueba}.
    QList<QJsonObject> llegaronAlLobby;
    bool primera = false; ///< no había base (la primera foto)
    bool nueva = false;   ///< es otra B (o no pudo reenviar lo perdido): republicar
    QString epoch;        ///< el arranque de la B con la que se habla
    std::uint64_t numero = 0; ///< cuál foto del lector es (para los tests y el diagnóstico)

    /// `base` puede ser nula (la primera).
    static std::shared_ptr<PaqueteSala> diferencia(const std::shared_ptr<const ResumenSala>& base,
                                                   const std::shared_ptr<const ResumenSala>& r);
};

/// Entre el lector (deposita) y la interfaz (toma). Ver arriba: la última gana.
class BuzonSala {
   public:
    /// En el hilo del lector: una foto nueva. true si hay que avisarle a la interfaz (no
    /// había una esperando: si había, esta la reemplaza y el aviso ya está en camino).
    bool depositar(const QJsonObject& foto, std::int64_t ahoraMs);
    /// En el hilo del lector: rearma la última foto (cambió la cantidad de lugares). Igual
    /// que depositar.
    bool rehacer(std::int64_t ahoraMs);
    /// En la interfaz: el paquete que espera (la última foto), o nulo. Lo tomado pasa a ser
    /// la base de las diferencias que siguen.
    std::shared_ptr<const PaqueteSala> tomar();

    /// Lo último armado por el lector (cualquier hilo).
    std::shared_ptr<const ResumenSala> ultimo() const;
    void setLugares(int n);
    int lugares() const;
    void setAvatar(std::function<QString(const QString&)> fn);

    struct Cuenta {
        std::uint64_t depositadas = 0, tomadas = 0, descartadas = 0;
    };
    Cuenta cuenta() const;

   private:
    bool poner(const QJsonObject& foto, std::int64_t ahoraMs, bool nueva);

    mutable std::mutex mu_;
    std::shared_ptr<const ResumenSala> base_;   ///< lo que tiene la interfaz
    std::shared_ptr<const ResumenSala> ultimo_; ///< lo último armado
    std::shared_ptr<PaqueteSala> pendiente_;
    QString epoch_;      ///< el arranque de la B con la que se habla
    bool nuevaPendiente_ = false;
    int nLugares_ = 0;
    std::function<QString(const QString&)> avatar_;
    Cuenta cuenta_;
};

} // namespace ba::app::sala
