// SPDX-License-Identifier: GPL-2.0-or-later
//
// SesionRemota: la banca virtual de cada integrante (docs/SESION-REMOTA.md).
// Un legislador entra desde el navegador (en la sala o afuera, da lo mismo) con
// su link personal y su PIN; la presidencia o el operador lo ve por la cámara y
// lo habilita; habilitado, cuenta como presente (y en las fases siguientes pide
// la palabra, habla y vota).
//
// Esta clase es la fuente de verdad del lado de la app:
//   * las credenciales: link + PIN por integrante, guardados SOLO como hash con
//     sal (Config/sesion-remota.json). El link y el PIN se muestran una vez, al
//     generarlos;
//   * el estado de cada uno: conectado, habilitado (por quién), dónde está
//     (sala/afuera), desde cuándo;
//   * el registro de la sesión (quién entró, quién lo habilitó, intentos
//     fallidos, bloqueos), que va anexado al acta.
//
// El borde de red es el gateway (src/gateway/Sesion.hpp): esta clase no habla
// con él directamente; pide lo que hace falta por las señales (`enviar`,
// `cortar`, `credencialesCambio`) y recibe sus avisos por `evento()`.
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace ba::app::legislatura {

class Cuerpo;

class SesionRemota : public QObject {
    Q_OBJECT
    /// [{id, nombre, banca, credencial, conectado, estado, lugar, ip, desde_ms, habilitado_por, bloqueado}]
    Q_PROPERTY(QVariantList participantes READ participantesUi NOTIFY cambio)
    /// Habilitados y conectados: los que cuentan como presentes por acá.
    Q_PROPERTY(int presentes READ presentes NOTIFY cambio)
    Q_PROPERTY(int conectados READ conectados NOTIFY cambio)
    /// El origen público para los links (lo da el gateway): "https://…".
    Q_PROPERTY(QString origen READ origenUi NOTIFY cambio)

   public:
    using json = nlohmann::json;
    SesionRemota(const Cuerpo& cuerpo, std::filesystem::path datos, QObject* parent = nullptr);

    // --- Credenciales -------------------------------------------------------------
    /// Genera (o regenera: el link anterior deja de andar) el link y el PIN de un
    /// integrante. Devuelve {link, pin, id} UNA vez, o {error}.
    json generar(const std::string& integrante);
    bool revocar(const std::string& integrante);
    /// Usuario y contraseña (para entrar sin el link, tipeando): la contraseña se
    /// genera (12 caracteres fáciles de dictar) y se muestra UNA vez. {usuario, clave} o {error}.
    json generarClave(const std::string& integrante, const std::string& usuario = {});
    /// Los datos de la conexión: {usuario?, email?, telefono?, nota?}. "" o el error.
    std::string setDatos(const std::string& integrante, const json& datos);
    bool tieneCredencial(const std::string& integrante) const;
    /// La lista que va al gateway: [{id, nombre, banca, sal, hash_token, hash_pin}].
    json paraGateway() const;
    void setOrigen(const std::string& origen);
    std::string origen() const { return origen_; }

    // --- Estado ---------------------------------------------------------------------
    /// Un aviso del gateway (evento SSE "sesion"): conexion, entrada, intento, bloqueo.
    void evento(const json& ev);
    /// Habilita (cuenta como presente) o deshabilita. `quien`: operador, presidencia, mcp…
    std::string habilitar(const std::string& integrante, bool si, const std::string& quien);
    /// Lo saca de la sesión (corta su conexión). No revoca el link.
    std::string expulsar(const std::string& integrante, const std::string& quien, const std::string& motivo);
    json participantes() const;
    int presentes() const;
    int conectados() const;
    /// Los integrantes habilitados y conectados (para no contarlos dos veces).
    std::vector<std::string> presentesIds() const;
    /// Las bancas de los habilitados y conectados.
    std::vector<std::string> presentesBancas() const;

    // --- Registro (va al acta) ---------------------------------------------------------
    json registro() const { return registro_; }
    std::string anexoActa() const;
    void limpiarRegistro();
    /// Segundos que se guarda la habilitación de alguien que se cayó (una recarga,
    /// un corte de red): si vuelve antes, no hay que habilitarlo de nuevo.
    int graciaS() const { return graciaS_; }

    // --- Para la interfaz --------------------------------------------------------------
    QVariantList participantesUi() const;
    QString origenUi() const { return QString::fromStdString(origen_); }
    Q_INVOKABLE QVariantMap generarUi(const QString& integrante);
    Q_INVOKABLE bool revocarUi(const QString& integrante);
    Q_INVOKABLE QVariantMap generarClaveUi(const QString& integrante, const QString& usuario);
    Q_INVOKABLE QString setDatosUi(const QString& integrante, const QVariantMap& datos);
    Q_INVOKABLE QString habilitarUi(const QString& integrante, bool si);
    Q_INVOKABLE QString expulsarUi(const QString& integrante, const QString& motivo);
    /// La foto de su cámara (la última que mandó): la pide la interfaz al gateway
    /// y, cuando llega (`fotoLista`), la toma con `fotoUrl` (data:image/jpeg…).
    Q_INVOKABLE void pedirFoto(const QString& integrante);
    Q_INVOKABLE QString fotoUrl(const QString& integrante) const;
    void ponerFoto(const QString& integrante, const QByteArray& jpeg);

   signals:
    void cambio();
    /// Hay que reenviarle al gateway la lista de credenciales.
    void credencialesCambio();
    /// Mandarle un mensaje a un participante ("*" = a todos).
    void enviar(const QString& integrante, const nlohmann::json& msg);
    void cortar(const QString& integrante, const QString& motivo);
    void fotoPedida(const QString& integrante);
    void fotoLista(const QString& integrante);
    /// Algo para avisarle al operador (un bloqueo, alguien que espera).
    void aviso(const QString& texto, const QString& nivel);

   private:
    struct Estado {
        bool conectado = false;
        bool habilitado = false;
        std::string habilitadoPor, lugar, ip;
        std::int64_t desdeMs = 0;      // conectado desde
        std::int64_t cayoMs = 0;       // se desconectó (para la gracia)
        std::int64_t bloqueadoHasta = 0;
    };
    void cargar();
    void guardar() const;
    void anotar(const std::string& que, const std::string& integrante, const json& extra = {});
    json integrante(const std::string& id) const;
    void mandarEstado(const std::string& id);

    const Cuerpo& cuerpo_;
    std::filesystem::path archivo_;
    std::filesystem::path archivoRegistro_;
    json credenciales_ = json::object(); // id -> {sal, hash_token, hash_pin, creado_ms}
    std::map<std::string, Estado> estado_;
    json registro_ = json::array();
    std::string origen_;
    int graciaS_ = 120;
    std::map<QString, QByteArray> fotos_;
};

} // namespace ba::app::legislatura
