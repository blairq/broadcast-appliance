// ============================================================================
// src/app/audio/Matriz.hpp — la matriz de mezcla: fuentes × buses
//
// FILAS: cada fuente con audio, tomada a la SALIDA DE SU CADENA DE FILTROS
// (el rack LSP de la sección Audio): lo mismo que va a la mezcla. Las
// entradas virtuales también son filas, y como son fuentes de audio de verdad
// aparecen en Audio con su propio rack.
//
// COLUMNAS (buses):
//   * las 6 PISTAS de OBS. La 1 es el programa (emisión y grabación); de la 2
//     a la 6 las toma la grabación individual cuando hace falta. Un cruce a
//     una pista es la asignación NATIVA de la fuente a esa pista: prender o
//     apagar, sin agregar nada al camino del audio (cero retardo).
//   * SALIDAS VIRTUALES: un bus nuestro que sale a un dispositivo del equipo
//     (otra placa, auriculares, un cable virtual hacia otra aplicación).
//   * ENTRADAS VIRTUALES: un bus nuestro cuya mezcla vuelve a la app como una
//     fuente (una submezcla: «todos los micrófonos», con un compresor encima).
//
// Los buses virtuales los mezcla un hilo propio a partir del audio de cada
// fuente después de sus filtros (el callback de captura de OBS). Eso agrega un
// colchón de ~20-30 ms, y SOLO a lo que pasa por un bus virtual: lo que va a
// las pistas nativas no se entera. Nivel, pan y mute existen en los cruces de
// los buses virtuales, y se muestran solo si se activan en ese cruce.
//
// Sin lazos: una entrada virtual no puede mandarse al bus del que sale (ni
// armar un círculo de buses).
//
// Se guarda en Config/audio-matriz.json. Hilos: todo lo de la interfaz en el
// principal; el audio de las fuentes llega en el de OBS (solo se copia) y la
// mezcla en el suyo.
// ============================================================================
#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

struct obs_source;
struct obs_volmeter;
struct audio_data;

namespace ba::app::audio {

class EquipoAudio;

/// Registra el tipo de fuente de las entradas virtuales (al arrancar libobs).
void registrarEntradaVirtual();

class Matriz : public QObject {
    Q_OBJECT
    /// [{nombre, virtual: bool}]
    Q_PROPERTY(QVariantList filas READ filas NOTIFY cambio)
    /// [{id, nombre, tipo: pista|salida|entrada, indice, uso, dispositivo, fuente}]
    Q_PROPERTY(QVariantList columnas READ columnas NOTIFY cambio)
    /// "fuente|bus" -> {activo, ajustes, nivelDb, pan, mute}
    Q_PROPERTY(QVariantMap cruces READ cruces NOTIFY cambio)
    /// "fila:<fuente>" / "bus:<id>" -> pico en dBFS (solo con la matriz a la vista).
    Q_PROPERTY(QVariantMap medidores READ medidores NOTIFY medidoresCambio)
    /// Dispositivos de salida del equipo: [{id, nombre}].
    Q_PROPERTY(QVariantList dispositivos READ dispositivos NOTIFY cambio)
    /// Las entradas y salidas físicas en la matriz (EquipoAudio): mostrarlas o no,
    /// y cada dispositivo [{id, nombre, tipo, canales, visible, abierto, enApp}].
    Q_PROPERTY(bool mostrarEquipo READ mostrarEquipo WRITE setMostrarEquipo NOTIFY cambio)
    Q_PROPERTY(QVariantList equipo READ equipo NOTIFY cambio)
    /// Las pistas 2-6 de OBS plegadas en la vista (por defecto: solo se ve la 1).
    Q_PROPERTY(bool pistasPlegadas READ pistasPlegadas WRITE setPistasPlegadas NOTIFY cambio)

   public:
    using json = nlohmann::json;
    static constexpr int kPistas = 6;
    static constexpr int kMaxVirtuales = 16;

    explicit Matriz(std::filesystem::path dataRoot, QObject* parent = nullptr);
    ~Matriz() override;

    QVariantList filas() const;
    QVariantList columnas() const;
    QVariantMap cruces() const;
    QVariantMap medidores() const { return medidores_; }
    QVariantList dispositivos() const;
    bool mostrarEquipo() const;
    void setMostrarEquipo(bool on);
    QVariantList equipo() const;
    bool pistasPlegadas() const { return pistasPlegadas_; }
    void setPistasPlegadas(bool on);
    /// Mostrar u ocultar un dispositivo del equipo (lo oculto no se abre).
    Q_INVOKABLE void setVisibleDispositivo(const QString& id, bool visible);

    /// La matriz está a la vista: medidores y relectura de fuentes.
    Q_INVOKABLE void setActiva(bool on);
    Q_INVOKABLE void refrescar();
    /// Prender o apagar un cruce. Devuelve el error o "".
    Q_INVOKABLE QString alternar(const QString& fuente, const QString& bus);
    Q_INVOKABLE QString setCruce(const QString& fuente, const QString& bus, bool activo);
    /// Mostrar (o no) nivel, pan y mute de un cruce de bus virtual.
    Q_INVOKABLE void setAjustes(const QString& fuente, const QString& bus, bool on);
    Q_INVOKABLE void ajustar(const QString& fuente, const QString& bus, double nivelDb, double pan,
                             bool mute);
    Q_INVOKABLE QString agregarSalidaVirtual(const QString& nombre, const QString& dispositivo);
    Q_INVOKABLE QString agregarEntradaVirtual(const QString& nombre);
    Q_INVOKABLE QString quitarBus(const QString& id);
    Q_INVOKABLE QString renombrar(const QString& id, const QString& nombre);
    Q_INVOKABLE QString setDispositivo(const QString& id, const QString& dispositivo);

    /// Para MCP: todo el estado en JSON.
    json estado() const;
    /// Dónde está cada pista ocupada por la grabación individual (lo informa el motor).
    void setUsoPista(int indice, const std::string& uso);

   signals:
    void cambio();
    void medidoresCambio();

   private:
    struct Bus;
    struct Envio {
        float ganancia = 1.0f;
        float pan = 0.0f;
        bool mute = false;
        bool ajustes = false; ///< la interfaz muestra los controles
        double nivelDb = 0.0;
    };
    struct Toma; // el audio de una fuente camino a los buses
    struct Plan; // lo que lee el hilo de mezcla

    Bus* buscar(const std::string& id);
    const Bus* buscar(const std::string& id) const;
    bool haceLazo(const std::string& fuente, const std::string& bus) const;
    void guardar() const;
    void cargar();
    void armarPlan();
    void sincronizarTomas();
    void bucleMezcla();
    void leerMedidores();
    std::vector<std::string> fuentesConAudio() const;
    /// Abre lo del equipo que se usa y cierra lo que no.
    void usarEquipo();
    static void alAudio(void* param, obs_source* src, const ::audio_data* data, bool muted);

    std::filesystem::path root_;
    std::string pistas_[kPistas];
    std::string usoPista_[kPistas];
    std::vector<std::unique_ptr<Bus>> buses_;
    std::unique_ptr<EquipoAudio> equipo_;
    bool pistasPlegadas_ = true;
    std::map<std::string, std::shared_ptr<std::atomic<float>>> picosEquipo_; ///< columna física -> pico
    std::map<std::pair<std::string, std::string>, Envio> envios_;            ///< (fuente, bus)
    std::vector<std::string> filas_;                                         ///< fuentes conocidas

    // Tomas: callback de captura por fuente con algún envío a un bus virtual.
    std::mutex tomasLock_;
    std::map<std::string, std::shared_ptr<Toma>> tomas_;

    std::mutex planLock_;
    std::shared_ptr<const Plan> plan_;
    std::atomic<bool> salir_{false};
    std::thread hilo_;

    bool cargando_ = false; ///< no guardar a medio cargar
    QTimer lento_;          ///< engancha las tomas de fuentes que aparecen después

    // Medidores
    bool activa_ = false;
    QTimer timer_;
    QVariantMap medidores_;
    std::map<std::string, obs_volmeter*> volmeters_;
    std::map<std::string, std::shared_ptr<std::atomic<float>>> picos_;
};

} // namespace ba::app::audio
