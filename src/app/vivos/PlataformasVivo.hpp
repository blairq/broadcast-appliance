// ============================================================================
// src/app/vivos/PlataformasVivo.hpp — YouTube, Twitch y Facebook con varias cuentas
//
// Las plataformas de verdad de vivos::ServicioVivos (vivos/Vivos.hpp). Cada una tiene un
// cliente HTTPS por cuenta: el de la PRINCIPAL es el de su página (YouTubeViewModel…,
// así la página y los vivos de las salidas comparten sesión y token), y el de cada cuenta
// más se crea cuando hace falta con su propia credencial (credencialDeCuenta). La app de
// la plataforma (el cliente OAuth) es una sola: si la página la cambia, las demás cuentas
// la releen.
//
// Hilos: todo en el de la interfaz (los clientes contestan ahí).
// ============================================================================
#pragma once

#include <QObject>
#include <QPointer>
#include <QString>

#include <map>
#include <memory>
#include <string>

#include "vivos/Vivos.hpp"

namespace ba::app {
class YouTubeClient;
class TwitchClient;
class FacebookClient;
} // namespace ba::app

namespace ba::app::vivos {

/// Lo común: el aviso de que cambió algo de alguna cuenta (conectó, se venció…).
class PlataformaVivo : public QObject, public Plataforma {
    Q_OBJECT
   public:
    using QObject::QObject;
   signals:
    void cambio();
};

/// Lo que se repite en las tres: la principal prestada y las demás propias.
template <class Cliente>
class Clientes {
   public:
    Clientes(Cliente& principal, QString dir, std::string plataforma)
        : principal_(&principal), dir_(std::move(dir)), plataforma_(std::move(plataforma)) {}
    /// El de la cuenta (lo crea la primera vez, sin padre: es de esta lista). `nuevo` dice
    /// si recién se creó.
    Cliente& de(const std::string& cuenta, bool* nuevo = nullptr) const {
        if (nuevo != nullptr) *nuevo = false;
        if (cuenta.empty() || cuenta == kPrincipal) return *principal_;
        auto it = otros_.find(cuenta);
        if (it == otros_.end()) {
            it = otros_.emplace(cuenta, std::make_unique<Cliente>(dir_, nullptr, QString::fromStdString(credencialDeCuenta(plataforma_, cuenta)))).first;
            if (nuevo != nullptr) *nuevo = true;
        }
        return *it->second;
    }
    Cliente& principal() const { return *principal_; }
    void olvidar(const std::string& cuenta) { otros_.erase(cuenta); }
    template <class F>
    void cadaOtro(F f) const {
        for (auto& [id, c] : otros_) f(*c);
    }

   private:
    Cliente* principal_;
    QString dir_;
    std::string plataforma_;
    mutable std::map<std::string, std::unique_ptr<Cliente>> otros_;
};

class YouTubeVivo final : public PlataformaVivo {
    Q_OBJECT
   public:
    /// `principal`: el cliente de la página (YouTubeViewModel::cliente()); `dir`: Credenciales/.
    YouTubeVivo(YouTubeClient& principal, const QString& dir, QObject* parent = nullptr);
    ~YouTubeVivo() override;
    std::string id() const override { return "youtube"; }
    bool vivosAparte() const override { return true; }
    bool configurada() const override;
    bool conectada(const std::string& cuenta) const override;
    bool conectando(const std::string& cuenta) const override;
    bool sinPermiso(const std::string& cuenta) const override;
    std::string canal(const std::string& cuenta) const override;
    void conectar(const std::string& cuenta, Listo listo) override;
    void cancelarConexion(const std::string& cuenta) override;
    void desconectar(const std::string& cuenta) override;
    void olvidar(const std::string& cuenta) override;
    void vivos(const std::string& cuenta, Listo listo) override;
    void crearVivo(const std::string& cuenta, const json& datos, Listo listo) override;
    void ingesta(const std::string& cuenta, const std::string& vivo, Listo listo) override;
    void salud(const std::string& cuenta, const std::string& vivo, Listo listo) override;

   private:
    YouTubeClient& cli(const std::string& cuenta) const;
    Clientes<YouTubeClient> c_;
    QString appAntes_; ///< la app de Google que tenían las demás (si cambia, la releen)
};

class TwitchVivo final : public PlataformaVivo {
    Q_OBJECT
   public:
    TwitchVivo(TwitchClient& principal, const QString& dir, QObject* parent = nullptr);
    ~TwitchVivo() override;
    std::string id() const override { return "twitch"; }
    bool vivosAparte() const override { return false; }
    bool configurada() const override;
    bool conectada(const std::string& cuenta) const override;
    bool conectando(const std::string& cuenta) const override;
    bool sinPermiso(const std::string&) const override { return false; }
    std::string canal(const std::string& cuenta) const override;
    void conectar(const std::string& cuenta, Listo listo) override;
    void cancelarConexion(const std::string& cuenta) override;
    void desconectar(const std::string& cuenta) override;
    void olvidar(const std::string& cuenta) override;
    void vivos(const std::string& cuenta, Listo listo) override;
    void crearVivo(const std::string& cuenta, const json& datos, Listo listo) override;
    void ingesta(const std::string& cuenta, const std::string& vivo, Listo listo) override;
    void salud(const std::string& cuenta, const std::string& vivo, Listo listo) override;

   private:
    TwitchClient& cli(const std::string& cuenta) const;
    Clientes<TwitchClient> c_;
    QString appAntes_;
};

class FacebookVivo final : public PlataformaVivo {
    Q_OBJECT
   public:
    FacebookVivo(FacebookClient& principal, const QString& dir, QObject* parent = nullptr);
    ~FacebookVivo() override;
    std::string id() const override { return "facebook"; }
    bool vivosAparte() const override { return true; }
    bool configurada() const override;
    bool conectada(const std::string& cuenta) const override;
    bool conectando(const std::string& cuenta) const override;
    bool sinPermiso(const std::string&) const override { return false; }
    std::string canal(const std::string& cuenta) const override;
    void conectar(const std::string& cuenta, Listo listo) override;
    void cancelarConexion(const std::string& cuenta) override;
    void desconectar(const std::string& cuenta) override;
    void olvidar(const std::string& cuenta) override;
    void vivos(const std::string& cuenta, Listo listo) override;
    void crearVivo(const std::string& cuenta, const json& datos, Listo listo) override;
    void ingesta(const std::string& cuenta, const std::string& vivo, Listo listo) override;
    void salud(const std::string& cuenta, const std::string& vivo, Listo listo) override;

   private:
    FacebookClient& cli(const std::string& cuenta) const;
    Clientes<FacebookClient> c_;
    QString appAntes_;
};

} // namespace ba::app::vivos
