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
   public:
    using json = nlohmann::json;
    using Listo = std::function<void(bool ok, const std::string& texto)>;
    struct Ganchos {
        std::function<void(const std::string& herramienta, const json& args, Listo listo)> ejecutar;
        std::function<std::vector<std::string>()> escenas;
        std::function<std::vector<std::string>()> fuentesAudio;
        std::function<bool(const std::string& fuente)> mudo;
        std::function<bool(int capa)> dskArriba;
    };
    Acciones(const std::filesystem::path& datos, QObject* parent = nullptr);
    ~Acciones() override;
    Ganchos& ganchos() { return g_; }

    /// [{id, nombre, grupo, herramienta?, args?}] (las de escenas y fuentes, con lo que hay ahora).
    json catalogo() const;
    /// La herramienta y los argumentos de una acción ({herramienta, args} o {error}).
    json resolver(const std::string& id) const;
    /// Ejecuta una acción por su id (como una persona: el atajo). "" (mandada) o el error.
    std::string ejecutar(const std::string& id, Listo listo = {});

    json atajos() const { return atajos_; }
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

   private:
    void guardar() const;
    void registrarGlobales();
    std::filesystem::path archivo_;
    Ganchos g_;
    json atajos_ = json::array();
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
