// SPDX-License-Identifier: GPL-2.0-or-later
//
// Acciones: el catálogo de cosas que se hacen «de un golpe» (TAKE, FTB, armar
// una escena, siguiente orador, siguiente punto, mutear un micrófono, subir una
// capa DSK, correr el prompter…) y los ATAJOS de teclado que las disparan.
//
// Cada acción es una herramienta MCP con argumentos fijos, o una «interna»
// que necesita mirar el estado (alternar el mudo de una fuente, subir o bajar
// una capa). La ejecución pasa por el mismo camino que todo lo demás (McpCore,
// como una persona: «atajo»).
//
// Muchas salidas (docs/planes/PLAN-SALIDAS.md): los atajos manejan UNA salida
// (`salida`, "" = la por omisión) y lo que corren va con esa en el contexto
// (CallContext::salida). Una acción con «@<salida>» al final («take@cam2») va a esa
// sin importar cuál manejan. «salida:siguiente», «salida:anterior» y «salida:<id>»
// cambian la que manejan (no se guarda: al arrancar, la por omisión).
//
// La línea de tiempo (PLAN-LINEA-DE-TIEMPO 3.5 D): GO, HOLD, mover el próximo,
// saltar y un adlib por ítem del estante, cada una también por salida
// («linea_go@sala»: la salida va en los argumentos de la herramienta). El feedback
// lo publica LineaViewModel en el bus (`linea/<salida>/estado`).
//
// Atajos (Config/atajos.json): [{tecla: "Ctrl+Shift+T", accion, global}]. Los
// locales funcionan con la ventana del programa al frente (un Shortcut de QML
// por atajo); los GLOBALES, aunque la ventana no tenga el foco (macOS:
// RegisterEventHotKey; Windows: RegisterHotKey; Linux: todavía no). Un pedal
// de prompter que se presenta como teclado se asigna igual.
#pragma once

#include <QObject>
#include <QVariantList>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ba::app::control {

class AtajosGlobales;

class Acciones : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList atajos READ atajosUi NOTIFY cambio)
    Q_PROPERTY(bool globalesDisponibles READ globalesDisponibles CONSTANT)
    /// La salida que manejan los atajos ("" = la por omisión).
    Q_PROPERTY(QString salida READ salidaUi WRITE setSalidaUi NOTIFY salidaCambio)
   public:
    using json = nlohmann::json;
    using Listo = std::function<void(bool ok, const std::string& texto)>;
    struct Ganchos {
        /// `salida`: la de la acción o la que manejan los atajos ("" = la por omisión).
        std::function<void(const std::string& herramienta, const json& args, const std::string& salida, Listo listo)>
            ejecutar;
        /// Las salidas: [{id, nombre, color, por_omision}], la por omisión primera.
        std::function<json()> salidas;
        std::function<std::vector<std::string>()> escenas;
        std::function<std::vector<std::string>()> fuentesAudio;
        std::function<bool(const std::string& fuente, const std::string& salida)> mudo;
        /// ¿La capa está arriba en esa salida? ("" = la por omisión: cada una sube las suyas).
        std::function<bool(int capa, const std::string& salida)> dskArriba;
        /// El estante de la línea de tiempo: [{id, texto, esPaso}] (lo pone LineaViewModel).
        std::function<json()> estante;
    };
    Acciones(const std::filesystem::path& datos, QObject* parent = nullptr);
    ~Acciones() override;
    Ganchos& ganchos() { return g_; }

    /// [{id, nombre, grupo, herramienta?, args?}] (las de escenas y fuentes, con lo que hay ahora).
    json catalogo() const;
    /// La herramienta y los argumentos de una acción ({herramienta, args, salida, salida_pedida},
    /// {elegir_salida} para las que cambian la salida, o {error}). `salida`: la de «@<salida>»
    /// (salida_pedida) o la que manejan los atajos.
    json resolver(const std::string& id) const;
    /// Ejecuta una acción por su id (como una persona: el atajo). "" (mandada) o el error.
    std::string ejecutar(const std::string& id, Listo listo = {});

    json atajos() const { return atajos_; }
    const std::string& salida() const { return salida_; }
    /// Cambia la salida que manejan los atajos (id; «siguiente», «anterior»; "" = la
    /// por omisión). "" o el error.
    std::string setSalida(const std::string& salida);
    QString salidaUi() const { return QString::fromStdString(salida_); }
    void setSalidaUi(const QString& s) { setSalida(s.toStdString()); }
    /// Asigna `tecla` (texto de QKeySequence, p. ej. "Ctrl+Shift+T" o "F5") a `accion`. "" o el error (conflicto incluido).
    std::string asignar(const std::string& tecla, const std::string& accion, bool global);
    bool quitar(const std::string& tecla);
    bool globalesDisponibles() const;

    QVariantList atajosUi() const;
    Q_INVOKABLE QVariantList catalogoUi() const;
    Q_INVOKABLE QString ejecutarUi(const QString& id);
    Q_INVOKABLE QString asignarUi(const QString& tecla, const QString& accion, bool global);
    Q_INVOKABLE bool quitarUi(const QString& tecla);
    /// El atajo de una acción («Ctrl+T»), para los tooltips. "" si no tiene.
    Q_INVOKABLE QString atajoDe(const QString& accion) const;

   signals:
    void cambio();
    void salidaCambio();

   private:
    void guardar() const;
    void registrarGlobales();
    std::filesystem::path archivo_;
    Ganchos g_;
    json atajos_ = json::array();
    std::string salida_;
    /// Si `id` es el de una salida (o su nombre), su id; si no, "".
    std::string salidaExistente(const std::string& id) const;
    json resolverSinSalida(const std::string& id, const std::string& salida) const;
    std::unique_ptr<AtajosGlobales> globales_;
};

/// Atajos que funcionan aunque la ventana no tenga el foco (por sistema).
class AtajosGlobales {
   public:
    virtual ~AtajosGlobales() = default;
    /// Registra `tecla` (QKeySequence portable); `fn` se llama en el hilo de la interfaz. false si no se pudo.
    virtual bool registrar(const std::string& tecla, std::function<void()> fn) = 0;
    virtual void limpiar() = 0;
    static std::unique_ptr<AtajosGlobales> crear(); ///< nullptr si el sistema no tiene
};

} // namespace ba::app::control
