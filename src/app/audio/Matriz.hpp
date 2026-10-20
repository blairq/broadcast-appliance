// ============================================================================
// src/app/audio/Matriz.hpp — la matriz de mezcla: fuentes × buses
//
// FILAS: cada fuente con audio, tomada a la SALIDA DE SU CADENA DE FILTROS
// (el rack LSP de la sección Audio): lo mismo que va a la mezcla. Las
// entradas virtuales también son filas, y como son fuentes de audio de verdad
// aparecen en Audio con su propio rack.
//
// COLUMNAS (buses):
//   * las 6 PISTAS del motor. La 1 es el programa (emisión y grabación); de la 2
//     a la 6 las toma la grabación individual cuando hace falta. Un cruce a
//     una pista es la asignación NATIVA de la fuente a esa pista: prender o
//     apagar, sin agregar nada al camino del audio (cero retardo).
//   * SALIDAS VIRTUALES: un bus nuestro que sale a un dispositivo del equipo
//     (otra placa, auriculares, un cable virtual hacia otra aplicación).
//   * ENTRADAS VIRTUALES: un bus nuestro cuya mezcla vuelve a la app como una
//     fuente (una submezcla: «todos los micrófonos», con un compresor encima).
//
// Los buses virtuales los mezcla un hilo propio a partir del audio de cada
// fuente después de su cadena (una toma del motor: escucharEntrada); una
// entrada virtual vuelve al motor por la cola de su `bavoz`. Eso agrega un
// colchón de ~20-30 ms, y SOLO a lo que pasa por un bus virtual: lo que va a
// las pistas nativas no se entera. Nivel, pan y mute existen en los cruces de
// los buses virtuales, y se muestran solo si se activan en ese cruce.
//
// Sin lazos: una entrada virtual no puede mandarse al bus del que sale (ni
// armar un círculo de buses).
//
// Se guarda en Config/audio-matriz.json. Hilos: todo lo de la interfaz en el
// principal; el audio de las fuentes llega en el de GStreamer (solo se copia)
// y la mezcla en el suyo.
// ============================================================================
#pragma once

#include <QObject>
#include <QSet>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::app::audio {

class EquipoAudio;

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
    /// Las pistas 2-6 plegadas en la vista (por defecto: solo se ve la 1).
    Q_PROPERTY(bool pistasPlegadas READ pistasPlegadas WRITE setPistasPlegadas NOTIFY cambio)

   public:
    /// Las fuentes que usan OTROS (las tarjetas «Entrada» de los racks): una
    /// entrada de placa que alguien trae así también abre su captura.
    void setUsadasPorOtros(std::function<std::set<std::string>()> f) { usadasPorOtros_ = std::move(f); }
    using json = nlohmann::json;
    static constexpr int kPistas = 6;
    static constexpr int kMaxVirtuales = 16;

    explicit Matriz(std::filesystem::path dataRoot, QObject* parent = nullptr);
    ~Matriz() override;

    QVariantList filas() const;
    QVariantList columnas() const;
    QVariantMap cruces() const;
    /// El pico (dB) de un bus virtual o una salida física (los de las pistas los da
    /// el motor: Motor::niveles). -100 si no existe o no suena. Barato: para los
    /// vúmetros del Mezclador.
    double picoDbDe(const std::string& columna) const;
    /// A qué destinos va una fuente (ids de columnas: pista<n>, buses, físicas).
    QStringList enviosDe(const QString& fuente) const;
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
    /// Una vista empieza o deja de mostrar los medidores (los mide mientras haya
    /// alguna: la matriz de la página Audio escondida no le apaga los medidores a la
    /// del Mezclador). Si la vista se destruye sin avisar, se la saca sola.
    Q_INVOKABLE void usar(QObject* vista, bool si);
    Q_INVOKABLE void refrescar();
    /// Prender o apagar un cruce. Devuelve el error o "".
    Q_INVOKABLE QString alternar(const QString& fuente, const QString& bus);
    Q_INVOKABLE QString setCruce(const QString& fuente, const QString& bus, bool activo);
    /// PFL: sumar UN canal a los auriculares un rato ("" lo apaga). Reemplaza al PFL
    /// anterior, pero NO toca la escucha fija (el cruce Escucha: la voz del asistente
    /// sigue sonando). Lo usan el Mezclador, la tarjeta y audio_escuchar; el aviso
    /// llega por el motor y la matriz, las tiras y el PFL lo ven.
    Q_INVOKABLE QString ponerPfl(const QString& fuente);
    /// El motor cambió el audio de una fuente (BroadcastEngine::alCambiarAudioFuente:
    /// escucha, pistas, volumen, mute, retardo o «alta»), lo haya pedido quien sea.
    /// Desde cualquier hilo; los de una misma vuelta se atienden juntos en el hilo de
    /// la matriz: se anota y se guarda, a una fuente que nace se le pone lo guardado, y
    /// si cambió el ruteo sale un solo cambio().
    void avisarFuente(const std::string& fuente, const char* que);
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
    /// Para diagnosticar: el nivel de cada fila (lo que entrega), el de cada bus, las
    /// tomas enganchadas y el estado de los dispositivos del equipo.
    json diagnostico() const;
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
    std::function<std::set<std::string>()> usadasPorOtros_;
    std::set<std::string> ultimosOtros_; ///< lo último que se miró (para no reabrir en cada vuelta)
    /// La entrada del motor de una entrada virtual (lee la cola de su `bavoz`).
    bool crearEntradaVirtual(const std::string& nombre);

    std::filesystem::path root_;
    std::string pistas_[kPistas];
    std::string usoPista_[kPistas];
    std::vector<std::unique_ptr<Bus>> buses_;
    std::unique_ptr<EquipoAudio> equipo_;
    bool pistasPlegadas_ = true;
    std::map<std::string, std::shared_ptr<std::atomic<float>>> picosEquipo_; ///< columna física -> pico
    QSet<QObject*> vistas_; ///< las que están mostrando los medidores (usar)
    std::map<std::pair<std::string, std::string>, Envio> envios_;            ///< (fuente, bus)
    std::vector<std::string> filas_;                                         ///< fuentes conocidas

    // Tomas: el audio de cada fuente con algún envío a un bus virtual.
    mutable std::mutex tomasLock_;
    std::map<std::string, std::shared_ptr<Toma>> tomas_;

    std::mutex planLock_;
    std::condition_variable planCv_; ///< hay un plan con buses (o hay que salir)
    std::shared_ptr<const Plan> plan_;
    std::atomic<bool> salir_{false};
    std::thread hilo_;

    // Lo guardado del audio de cada fuente, ESTÉ O NO (una placa desenchufada, un
    // invitado que todavía no entró): sus pistas, la escucha fija, volumen, mute y
    // retardo. Se le pone cuando aparece (aplicarGuardado) y se anota cuando cambia.
    struct Canal {
        std::optional<std::uint32_t> pistas;
        std::optional<bool> escucha;
        std::optional<float> volumen;
        std::optional<bool> mute;
        std::optional<int> retardoMs;
    };
    mutable std::map<std::string, Canal> canales_;
    std::set<std::string> aplicadas_; ///< las que ya tienen lo guardado (desde ahí se anotan)
    std::mutex avisosM_;
    std::map<std::string, std::set<std::string>> avisos_; ///< avisarFuente: lo que falta atender
    QTimer guardarLuego_; ///< el fader se mueve seguido: se guarda un rato después
    void aplicarGuardado(const std::string& fuente);
    void anotar(const std::string& fuente) const; ///< del motor a canales_
    void atenderAvisos();

    bool cargando_ = false; ///< no guardar a medio cargar
    QTimer lento_;          ///< engancha las tomas de fuentes que aparecen después

    // Medidores
    bool activa_ = false;
    QTimer timer_;
    QVariantMap medidores_;
};

} // namespace ba::app::audio
