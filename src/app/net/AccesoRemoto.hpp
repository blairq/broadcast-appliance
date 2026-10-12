// ============================================================================
// src/app/net/AccesoRemoto.hpp — El túnel de pinggy visto desde la interfaz
//
// Capa de SERVICIO para Configuración → Acceso remoto (y la herramienta MCP
// acceso_remoto_estado). Ver docs/planes/PINGGY.md.
//   - Estado: GET /api/remoto de la API de control de la sala (B), cada 5 s
//     mientras alguien mira (`siguiendo`), o a pedido. Un gateway viejo sin esa
//     ruta (404) se muestra como «la sala no informa», sin avisos de error.
//   - Token de túnel: SOLO en Credenciales/ cifrado («pinggy-token»). Se guarda
//     o se borra; nunca se devuelve, ni se escribe en JSON ni en el registro.
//     La vista solo sabe si está guardado o falta. El asistente no lo toca.
//   - API key de pinggy (monitoreo, plan Pro): igual, «pinggy-api-key». Con
//     ella, «Ver en pinggy» consulta /session/active del panel de pinggy.
//   - DNS de un dominio propio: las instrucciones para el que administra el DNS
//     (texto copiable) y una verificación del CNAME (QDnsLookup, sin
//     herramientas del sistema).
// ============================================================================
#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include <functional>

namespace ba::app {

class AppRuntime;
class ConferenceClient;

class AccesoRemoto : public QObject {
    Q_OBJECT
    /// {disponible, sala, pinggy:{activo, estado, urls, urlPublica, error, turn:{...}}, texto}
    Q_PROPERTY(QVariantMap estado READ estado NOTIFY estadoChanged)
    /// [{nombre, url}]: la dirección de cada página (invitados, sesión remota, público…)
    Q_PROPERTY(QVariantList enlaces READ enlaces NOTIFY estadoChanged)
    Q_PROPERTY(bool tokenGuardado READ tokenGuardado NOTIFY credencialesChanged)
    Q_PROPERTY(bool apiKeyGuardada READ apiKeyGuardada NOTIFY credencialesChanged)
    /// El token de túnel y la API key son la MISMA clave: casi seguro se pegó la API
    /// key donde va el token (y así el túnel no abre).
    Q_PROPERTY(bool clavesIguales READ clavesIguales NOTIFY credencialesChanged)
    /// El token del túnel de Cloudflare (Configuración → Cloudflare), cifrado en Credenciales/.
    Q_PROPERTY(bool tokenCloudflareGuardado READ tokenCloudflareGuardado NOTIFY credencialesChanged)
    /// Mientras sea true, consulta el estado cada 5 s (la vista lo prende al mostrarse).
    Q_PROPERTY(bool siguiendo READ siguiendo WRITE setSiguiendo NOTIFY siguiendoChanged)
    /// {estado: ""|"verificando"|"ok"|"mal"|"error", texto}
    Q_PROPERTY(QVariantMap dns READ dns NOTIFY dnsChanged)
    /// {estado: ""|"consultando"|"ok"|"error", texto}
    Q_PROPERTY(QVariantMap panel READ panel NOTIFY panelChanged)
    /// «Probar dirección»: {estado: probando|ok|mal|error, texto}.
    Q_PROPERTY(QVariantMap prueba READ prueba NOTIFY pruebaChanged)
   public:
    AccesoRemoto(AppRuntime& rt, ConferenceClient& cli, QObject* parent = nullptr);

    QVariantMap estado() const { return estado_; }
    QVariantList enlaces() const { return enlaces_; }
    bool tokenGuardado() const;
    bool apiKeyGuardada() const;
    bool clavesIguales() const;
    bool tokenCloudflareGuardado() const;
    bool siguiendo() const { return siguiendo_; }
    void setSiguiendo(bool on);
    QVariantMap dns() const { return dns_; }
    QVariantMap panel() const { return panel_; }
    QVariantMap prueba() const { return prueba_; }

    Q_INVOKABLE void refrescar();
    /// Guarda el token de túnel (cifrado). "" si salió bien, si no el motivo.
    Q_INVOKABLE QString guardarToken(const QString& token);
    Q_INVOKABLE QString borrarToken();
    Q_INVOKABLE QString guardarTokenCloudflare(const QString& token);
    Q_INVOKABLE QString borrarTokenCloudflare();
    Q_INVOKABLE QString guardarApiKey(const QString& clave);
    Q_INVOKABLE QString borrarApiKey();
    /// Las instrucciones para el que administra el DNS (dominio propio).
    Q_INVOKABLE QString instruccionesDns(const QString& host, const QString& destino) const;
    /// Resuelve el CNAME de `host` y dice si apunta a `destino` (resultado en `dns`).
    Q_INVOKABLE void verificarDns(const QString& host, const QString& destino);
    /// Consulta los túneles activos en el panel de pinggy con la API key (resultado en `panel`).
    Q_INVOKABLE void verEnPinggy();
    /// Prueba si pinggy da esa dirección con el token guardado: abre un túnel de
    /// prueba corto (sin desplazar a otro) y compara la dirección que devuelve.
    /// Con la API key, mira antes si ya la usa un túnel activo de la cuenta.
    /// Resultado en `prueba`. `tipo`: azar|pinggy|propio.
    Q_INVOKABLE void probarDireccion(const QString& tipo, const QString& host);

    /// Para el MCP: el estado de la sala más la configuración (nunca el token).
    void consultar(std::function<void(nlohmann::json)> done);

   signals:
    void estadoChanged();
    void credencialesChanged();
    void siguiendoChanged();
    void dnsChanged();
    void panelChanged();
    void pruebaChanged();

   private:
    void pedirEstado(std::function<void(bool ok, const QVariantMap& r)> done);
    void aplicarEstado(bool ok, const QVariantMap& r);
    QVariantMap configuracion() const;

    AppRuntime& rt_;
    ConferenceClient& cli_;
    QTimer poll_;
    bool siguiendo_ = false;
    QString ruta_ = "/api/remoto"; ///< la que contestó (o la primera a probar)
    QVariantMap estado_;
    QVariantList enlaces_;
    QVariantMap dns_;
    QVariantMap panel_;
    QVariantMap prueba_;
    void ponerPrueba(const QString& estado, const QString& texto);
    void probarConTunel(const QString& host);
};

} // namespace ba::app
