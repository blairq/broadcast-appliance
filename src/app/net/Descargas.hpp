// ============================================================================
// src/app/net/Descargas.hpp — TODO lo que el programa baja, en un solo lugar
//
// Pedido del dueño (2026-10-11): «un módulo de descarga único, que se pueda ver
// el estado, que se pongan en cola, que si algo ya está en cola, bajándose o
// bajado no lo vuelva a bajar, y que controle actualizaciones». Antes cada parte
// tenía su descargador con su propia traba (los modelos, los agentes, la
// superresolución, los plugins): una sola descarga a la vez y la segunda se
// rechazaba.
//
// Cómo se usa: quien quiere bajar algo arma un Pedido con
//   - `id`: QUÉ es, único ("modelo:BlairQ/x", "agente:pi@1.1.0"). Lo que tiene el
//     mismo id no se encola dos veces;
//   - `yaEsta()`: si ya está instalado (esa versión): no se baja;
//   - `trabajo(ctx)`: lo que hace, en un hilo de la cola. Para bajar cada archivo
//     usa ctx.bajar() (sigue donde se cortó, con Range) y avisa con ctx.avance();
//     mira ctx.cancelado();
//   - `alTerminar(ok, error)`: en el hilo de la interfaz.
// La cola corre de a dos (cada pedido baja a su propia carpeta). Lo que
// terminó queda en el historial (Runtime/descargas.json).
//
// Actualizaciones: cada tipo registra cómo averiguar si hay algo más nuevo
// (registrarBuscador). buscarActualizaciones() los corre en el fondo y lo que
// encuentra queda en `actualizaciones` con su Pedido listo para encolar.
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

#include <QObject>
#include <QVariantList>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ba::app::net {

class Descargas : public QObject {
    Q_OBJECT
    /// [{id, titulo, tipo, version, estado, progreso, texto, bytes, total, kbps, error, desde, hasta, reintentable}]
    Q_PROPERTY(QVariantList lista READ lista NOTIFY cambio)
    Q_PROPERTY(int activas READ activas NOTIFY cambio)
    /// [{id, titulo, tipo, version, actual, detalle}]
    Q_PROPERTY(QVariantList actualizaciones READ actualizaciones NOTIFY actualizacionesCambio)
    Q_PROPERTY(bool buscando READ buscando NOTIFY actualizacionesCambio)

   public:
    enum class Estado { EnCola, Bajando, Listo, Error, Cancelado };

    /// Lo que recibe el trabajo (en su hilo).
    class Contexto {
       public:
        bool cancelado() const { return cancelado_->load(); }
        /// Avance de TODO el pedido: fracción (0..1) y texto.
        void avance(double fraccion, const std::string& texto);
        /// Baja `url` a `destino` (un archivo). Usa `destino.parte` y, si ya hay
        /// una parte de un intento anterior, sigue desde ahí. `base` y `total` son
        /// para el avance del pedido entero (bytes ya hechos de otros archivos y el
        /// total). "" o el error. NO verifica: eso lo sabe quien pide.
        std::string bajar(const std::string& url, const std::filesystem::path& destino, std::uint64_t base = 0,
                          std::uint64_t total = 0, const std::string& nombre = {});
        const std::atomic<bool>* banderaCancelar() const { return cancelado_.get(); }
        /// Para lo que corre un proceso que vive en el hilo de la interfaz (pip, el
        /// laboratorio de superresolución: plugins::Hijo). `arrancar` corre en la
        /// interfaz y llama a `listo("" o el error)` cuando termina (desde la
        /// interfaz o de donde sea); si se cancela, `cortar` (en la interfaz). Este
        /// hilo de la cola espera; la interfaz no.
        std::string enLaInterfaz(std::function<void(std::function<void(const std::string&)> listo)> arrancar,
                                 std::function<void()> cortar = {});

       private:
        friend class Descargas;
        Descargas* d_ = nullptr;
        std::string id_;
        std::shared_ptr<std::atomic<bool>> cancelado_;
    };

    struct Pedido {
        std::string id, titulo, tipo, version;
        std::function<bool()> yaEsta;                        ///< (en el hilo de la interfaz) ¿ya está esa versión?
        std::function<std::string(Contexto&)> trabajo;       ///< en un hilo de la cola: "" o el error
        std::function<void(bool ok, const std::string& error)> alTerminar; ///< en el hilo de la interfaz
        /// Ids que tienen que terminar antes (si están en la cola o bajándose).
        std::vector<std::string> despuesDe;
    };

    enum class Resultado { Encolada, YaEnCola, Bajando, YaEsta, Invalida };
    static const char* texto(Resultado r);

    struct Actualizacion {
        std::string actual;  ///< lo que hay instalado
        std::string detalle; ///< para mostrar
        Pedido pedido;       ///< listo para encolar (su `version` es la nueva)
        /// Si está, actualizar() la llama (en el hilo de la interfaz) en vez de
        /// encolar `pedido` (cuando quien baja arma su propio pedido).
        std::function<void()> aplicar;
    };
    using Buscador = std::function<std::vector<Actualizacion>()>; ///< corre en un hilo de fondo

    /// La única. La crea iniciar() (en el hilo de la interfaz, al arrancar).
    static Descargas& la();
    static void iniciar(const std::filesystem::path& raizDatos);

    Resultado pedir(Pedido p);
    /// ¿Ese id está en cola o bajándose?
    bool ocupado(const std::string& id) const;
    /// El avance de un id que está andando (0..1, texto); false si no está.
    bool avanceDe(const std::string& id, double* fraccion, std::string* texto) const;
    Q_INVOKABLE void cancelar(const QString& id);
    Q_INVOKABLE bool reintentar(const QString& id);
    Q_INVOKABLE void limpiarTerminadas();

    void registrarBuscador(const std::string& tipo, Buscador b);
    Q_INVOKABLE void buscarActualizaciones();
    Q_INVOKABLE bool actualizar(const QString& id);

    QVariantList lista() const;
    int activas() const;
    QVariantList actualizaciones() const;
    bool buscando() const { return buscando_.load(); }
    nlohmann::json estado() const;

    ~Descargas() override;

   signals:
    void cambio();
    void actualizacionesCambio();
    /// Una descarga terminó (para avisos generales).
    void terminada(const QString& id, const QString& titulo, bool ok, const QString& error);

   private:
    explicit Descargas(std::filesystem::path raiz);
    struct Item {
        Pedido pedido;
        Estado estado = Estado::EnCola;
        double progreso = 0;
        std::string texto, error;
        std::uint64_t bytes = 0, total = 0;
        double kbps = 0;
        std::int64_t desde = 0, hasta = 0; ///< ms (época)
        std::shared_ptr<std::atomic<bool>> cancelado = std::make_shared<std::atomic<bool>>(false);
        bool reintentable = false;
    };
    void correr();                   ///< un obrero
    std::shared_ptr<Item> tomar();   ///< el próximo que puede correr (con mu_)
    void avisar();                   ///< cambio, a lo sumo cuatro por segundo
    void guardarHistorial();   ///< en un hilo de la cola (escribe al disco)
    void guardarEnElFondo();   ///< lo mismo, pedido desde la interfaz
    std::mutex muHistorial_;
    void leerHistorial();
    QVariantMap aMapa(const Item& i) const;

    std::filesystem::path raiz_;
    mutable std::mutex mu_;
    std::condition_variable hay_;
    std::vector<std::shared_ptr<Item>> items_; ///< en orden de llegada (cola, andando, historial)
    std::vector<std::thread> obreros_;
    bool fin_ = false;
    std::atomic<bool> pendienteAviso_{false};
    std::map<std::string, Buscador> buscadores_;
    std::vector<Actualizacion> actualizaciones_;
    std::atomic<bool> buscando_{false};
    std::thread buscador_;
};

} // namespace ba::app::net
