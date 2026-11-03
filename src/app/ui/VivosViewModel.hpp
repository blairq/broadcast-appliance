// ============================================================================
// src/app/ui/VivosViewModel.hpp — las cuentas de las plataformas y los vivos de cada salida
//
// La vista de vivos::ServicioVivos (vivos/Vivos.hpp, docs/funciones/streaming-cuentas.md):
//
//   - `cuentas`: las de todas las plataformas, una fila por cuenta (clave «uid» =
//     «plataforma/cuenta»), con su estado (conectada, vencida, sin permiso…). Las de cada
//     plataforma, en `cuentasPor` (la sección «Cuentas» de cada página).
//   - `vivos`: los vivos de la cuenta que se está mirando (`vivosDe`): el selector de
//     destinos de la página Salidas los pide con cargarVivos.
//   - `destinos`: cada destino con su estado (conectando, en vivo, reintentando…), la
//     salud que informa la plataforma y el bitrate que sale; los de cada salida, en
//     `porSalida` (la tarjeta de cada salida los muestra con un ListaConClave).
//
// El modelo avisa por fila y por rol (ui/ListaObjetos): la vista se enlaza a las
// propiedades y los roles y hace lo que se pide con los Q_INVOKABLE (que avisan con
// `notify` o `asignado`). Lo mismo, con el resultado, para el MCP (mcp/McpVivosTools).
//
// Las cuentas son del aparato (cifradas en Credenciales/); los destinos, del evento
// (Config/destinos.json): se guardan por la ventanilla del motor, como los demás.
// ============================================================================
#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "vivos/Vivos.hpp"

namespace ba::app {
class AppRuntime;
class EmisionViewModel;
class ListaObjetos;
class PlataformaViewModel;
class VentanillaMotor;
class YouTubeViewModel;
class TwitchViewModel;
class FacebookViewModel;
namespace vivos {
class PlataformaVivo;
}
} // namespace ba::app

namespace ba::app {

class VivosViewModel : public QObject {
    Q_OBJECT
    /// Todas las cuentas: roles uid, plataforma, cuenta, nombre, canal, estado, estadoTexto,
    /// principal, conectada, conectando.
    Q_PROPERTY(QObject* cuentas READ cuentasQml CONSTANT)
    /// {youtube: <lista>, twitch: <lista>, facebook: <lista>}: las de cada plataforma.
    Q_PROPERTY(QVariantMap cuentasPor READ cuentasPor CONSTANT)
    /// Los vivos de la cuenta mirada: roles uid (el id del vivo), titulo, estado, privacidad,
    /// inicio, url.
    Q_PROPERTY(QObject* vivos READ vivosQml CONSTANT)
    /// De quién son `vivos`: {plataforma, cuenta, cargando, error, aparte (tiene vivos
    /// aparte: YouTube y Facebook; Twitch no)}.
    Q_PROPERTY(QVariantMap vivosDe READ vivosDe NOTIFY vivosDeCambio)
    /// Todos los destinos con su estado: roles uid, nombre, salida, plataforma,
    /// plataformaNombre, cuenta, cuentaNombre, vivo, vivoTitulo, estado, estadoTexto, kbps,
    /// activo, reconectando, respaldo, salud, saludTexto, choque.
    Q_PROPERTY(QObject* destinos READ destinosQml CONSTANT)
    /// {salida: [filas de `destinos`]}: los de cada salida.
    Q_PROPERTY(QVariantMap porSalida READ porSalida NOTIFY porSalidaCambio)
   public:
    struct Partes {
        AppRuntime& rt;
        VentanillaMotor& ventanilla;
        YouTubeViewModel& youtube;
        TwitchViewModel& twitch;
        FacebookViewModel& facebook;
        /// El bitrate de cada destino (EmisionViewModel::saludActual), cuando ya existe.
        std::function<EmisionViewModel*()> emision;
    };
    explicit VivosViewModel(Partes p, QObject* parent = nullptr);
    ~VivosViewModel() override;

    QObject* cuentasQml() const;
    QVariantMap cuentasPor() const;
    QObject* vivosQml() const;
    QVariantMap vivosDe() const { return vivosDe_; }
    QObject* destinosQml() const;
    QVariantMap porSalida() const { return porSalida_; }

    vivos::ServicioVivos& servicio() { return *servicio_; }
    vivos::RegistroCuentas& registro() { return *registro_; }

    // --- La interfaz (los errores, por notify) ------------------------------------------
    Q_INVOKABLE bool agregarCuenta(const QString& plataforma, const QString& nombre);
    Q_INVOKABLE bool renombrarCuenta(const QString& plataforma, const QString& cuenta, const QString& nombre);
    /// Revoca su token, lo borra y la saca de la lista (la principal no se quita).
    Q_INVOKABLE bool quitarCuenta(const QString& plataforma, const QString& cuenta);
    /// Conectar o reautorizar (abre el navegador).
    Q_INVOKABLE void conectarCuenta(const QString& plataforma, const QString& cuenta);
    Q_INVOKABLE void cancelarConexion(const QString& plataforma, const QString& cuenta);
    Q_INVOKABLE void desconectarCuenta(const QString& plataforma, const QString& cuenta);
    /// Pide los vivos de la cuenta (llegan en `vivos`).
    Q_INVOKABLE void cargarVivos(const QString& plataforma, const QString& cuenta);
    /// Crea un vivo en la cuenta y, si `salida` no está vacía, lo asigna a esa salida.
    Q_INVOKABLE void crearVivoUi(const QString& salida, const QString& plataforma, const QString& cuenta, const QString& titulo,
                                 const QString& privacidad, const QString& inicio);
    /// Asigna un vivo existente de la cuenta a la salida. El resultado, con `asignado`.
    Q_INVOKABLE void asignarVivoUi(const QString& salida, const QString& plataforma, const QString& cuenta, const QString& vivo,
                                   const QString& titulo);

    // --- Con el resultado (el MCP) --------------------------------------------------------
    using AlAsignar = std::function<void(const std::string& error, const std::string& id)>;
    void asignar(vivos::Asignacion a, AlAsignar listo);
    void conectar(const std::string& plataforma, const std::string& cuenta, vivos::Listo listo);
    void listarVivos(const std::string& plataforma, const std::string& cuenta, vivos::Listo listo);
    void crearVivo(const std::string& plataforma, const std::string& cuenta, const nlohmann::json& datos, vivos::Listo listo);
    std::string agregar(const std::string& plataforma, const std::string& nombre, std::string* id);
    std::string renombrar(const std::string& plataforma, const std::string& cuenta, const std::string& nombre);
    std::string quitar(const std::string& plataforma, const std::string& cuenta);
    /// El estado de los destinos (todas las salidas o una), como lo ve el operador.
    nlohmann::json estado(const std::string& salida = {}) const;

   signals:
    void vivosDeCambio();
    void porSalidaCambio();
    /// Volvió una asignación de la interfaz: "" o el error (también va por notify).
    void asignado(const QString& error);
    void notify(const QString& texto, const QString& nivel);

   private:
    vivos::Plataforma* plataforma(const std::string& id) const;
    PlataformaViewModel* paginaDe(const std::string& id) const;
    void rehacerCuentas();
    void rehacerDestinos();
    /// La huella de la clave guardada de un destino (cacheada: descifrar cuesta).
    std::string huellaDe(const std::string& destino) const;
    nlohmann::json kbps() const;
    void avisarError(const std::string& e);

    Partes p_;
    std::unique_ptr<vivos::RegistroCuentas> registro_;
    std::unique_ptr<vivos::ServicioVivos> servicio_;
    std::unique_ptr<vivos::PlataformaVivo> youtube_, twitch_, facebook_;
    std::unique_ptr<ListaObjetos> cuentas_, vivos_, destinos_;
    std::map<std::string, std::unique_ptr<ListaObjetos>> cuentasPor_;
    QVariantMap vivosDe_;
    QVariantMap porSalida_;
    QTimer sondeo_; ///< la salud que informa cada plataforma (cada 30 s, si algo sale)
    mutable std::map<std::string, std::string> huellas_;
    nlohmann::json listaAntes_; ///< los destinos guardados (para olvidar las huellas si cambian)
};

} // namespace ba::app
