// SPDX-License-Identifier: GPL-2.0-or-later
//
// Notificador: avisos FUERA del programa cuando salta un disparador («se cayó
// la transmisión», «sin quórum», «el disco se llena»), para el responsable que
// no está mirando la pantalla.
//
//   webhook:  POST JSON {titulo, texto, ms, origen} a una URL (sirve para
//             Slack, Discord, n8n, un servidor propio…)
//   telegram: un bot (token) y un chat (chat_id)
//   smtp:     un mail por SMTP con TLS (SMTPS en el 465 o STARTTLS en el 587)
//
// Config/notificadores.json guarda lo que no es secreto; el token del bot, la
// URL del webhook (puede llevar una clave adentro) y la contraseña del mail
// van cifrados en Credenciales/ (CredentialStore) como «notificador-<id>».
#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QVariantList>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <mutex>
#include <string>

namespace ba::app::agent {

class Notificador : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList lista READ listaUi NOTIFY cambio)
   public:
    using json = nlohmann::json;
    using Listo = std::function<void(bool ok, const std::string& detalle)>;
    Notificador(const std::filesystem::path& datos, QObject* parent = nullptr);

    /// [{id, tipo, nombre, …sin secretos, tiene_secreto}].
    json lista() const; ///< desde cualquier hilo
    /// Suma (o pisa por `id`) un notificador. `secreto`: token, URL o contraseña. Devuelve el id o {error}.
    json agregar(const json& def, const std::string& secreto);
    bool borrar(const std::string& id);
    /// Manda un aviso. `listo` dice si llegó (o por qué no).
    void enviar(const std::string& id, const std::string& titulo, const std::string& texto, Listo listo);

    QVariantList listaUi() const;
    Q_INVOKABLE QString agregarUi(const QVariantMap& def, const QString& secreto);
    Q_INVOKABLE bool borrarUi(const QString& id) { return borrar(id.toStdString()); }
    /// Manda uno de prueba; el resultado llega por `probado`.
    Q_INVOKABLE void probarUi(const QString& id);

   signals:
    void cambio();
    void probado(const QString& id, bool ok, const QString& detalle);

   private:
    void guardar() const;
    const json* buscar(const std::string& id) const;
    std::string secreto(const std::string& id) const;
    void enviarSmtp(const json& n, const std::string& clave, const std::string& titulo, const std::string& texto, Listo listo);
    std::filesystem::path archivo_, credenciales_;
    /// La cambia solo su hilo (agregar, borrar), con mu_; lista() la lee desde cualquiera
    /// (el MCP en el pool, F3-03) con mu_. Los demás la leen en su hilo, sin candado.
    json lista_ = json::array();
    mutable std::mutex mu_;
    QNetworkAccessManager nam_;
};

} // namespace ba::app::agent
