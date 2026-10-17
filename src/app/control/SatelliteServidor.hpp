// SPDX-License-Identifier: GPL-2.0-or-later
//
// SatelliteServidor: el lado SERVIDOR de la «Satellite API» de Bitfocus
// Companion (https://companion.free/for-developers/Satellite-API/). Con esto,
// Companion Satellite —instalado por el operador en otra máquina o en una
// Raspberry Pi junto a sus Stream Deck, Loupedeck, X-keys, Infinitton…—
// maneja nuestra botonera sin que escribamos drivers: cada ADD-DEVICE es un
// aparato más de la Botonera (perfil y página asignables), cada KEY-PRESS es
// una tecla apretada y cada tecla se dibuja con el estado de su celda
// (KEY-STATE con BITMAP RGB, COLOR y TEXT).
//
// Modo simple (KEYS_TOTAL/KEYS_PER_ROW), API 1.7: es lo que habla cualquier
// Satellite desde Companion 3.4. TCP, puerto 16622 de fábrica.
//
// El protocolo NO tiene autenticación: se acepta solo desde la red local y de
// las IP que una persona permitió (la primera vez que una IP nueva agrega un
// aparato, queda en «pendientes» para aprobarla en la página Botonera o en
// Servicios; si la rechaza, no vuelve a pedir hasta que se reinicie). Todo
// queda en la auditoría de la botonera. Apagado de fábrica.
//
// Config/botonera-satellite.json: {activo, puerto, ip_permitidas[]}.
#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>

class QTcpServer;
class QTcpSocket;

namespace ba::app::control {

class Botonera;

class SatelliteServidor : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool activo READ activo NOTIFY cambio)
    Q_PROPERTY(int puerto READ puerto NOTIFY cambio)
    Q_PROPERTY(QStringList ipPermitidas READ ipPermitidas NOTIFY cambio)
    Q_PROPERTY(QStringList pendientes READ pendientes NOTIFY cambio)
    /// Los pedidos que esperan a una persona: [{id (la IP), ip, nombre (el aparato), ms}].
    Q_PROPERTY(QVariantList solicitudes READ solicitudesUi NOTIFY cambio)
    Q_PROPERTY(QVariantList clientes READ clientesUi NOTIFY cambio)
    Q_PROPERTY(QString error READ error NOTIFY cambio)
   public:
    using json = nlohmann::json;
    SatelliteServidor(const std::filesystem::path& datos, Botonera& botonera, QObject* parent = nullptr);
    ~SatelliteServidor() override;

    bool activo() const { return activo_; }
    int puerto() const { return puerto_; }
    QStringList ipPermitidas() const;
    QStringList pendientes() const;
    QVariantList solicitudesUi() const;
    QString error() const { return error_; }
    QVariantList clientesUi() const;
    json estado() const;

    /// Prende o apaga, cambia el puerto. "" o el error.
    Q_INVOKABLE QString configurar(bool activo, int puerto);
    /// Una persona permite (o saca) una IP.
    Q_INVOKABLE void permitir(const QString& ip, bool si);
    /// Una persona rechaza el pedido de una IP: sale de pendientes y no vuelve a
    /// pedir hasta que se reinicie el programa (o se la permita a mano).
    Q_INVOKABLE void rechazar(const QString& ip);
    /// Las IP permitidas, todas juntas (solo una persona: amplía el acceso).
    Q_INVOKABLE void setIpPermitidas(const QStringList& ips);

    /// La botonera le manda algo a un aparato de Satellite ("sat:<ip>/<id>").
    void mensaje(const std::string& dispositivo, const json& msg);

    /// El texto de una línea del protocolo como mapa (para las pruebas).
    static std::map<std::string, std::string> argumentos(const std::string& linea, std::string* comando);

   signals:
    void cambio();

   private:
    struct Aparato {
        std::string id;      ///< DEVICEID
        int total = 32, porFila = 8, bitmaps = 72;
        bool colores = false, texto = false;
        json ultimo;         ///< el último estado de la botonera
        QHash<int, QString> enviado; ///< tecla -> firma de lo último mandado
    };
    struct Cliente {
        QTcpSocket* s = nullptr;
        QByteArray buf;
        std::string ip;
        std::map<std::string, Aparato> aparatos;
    };
    void cargar();
    void guardar() const;
    void abrir();
    void cerrar();
    void nueva();
    void leer(QTcpSocket* s);
    void linea(Cliente& c, const std::string& l);
    void enviar(Cliente& c, const std::string& l);
    void dibujar(Cliente& c, Aparato& a, bool forzar);
    QByteArray tecla(const json& celda, int lado, bool esperando, bool fase) const;
    /// Las caras ya decodificadas (data: URL -> imagen), para no decodificar en cada titileo.
    mutable std::map<std::string, QImage> caras_;
    std::string idApp(const Cliente& c, const std::string& deviceId) const { return "sat:" + c.ip + "/" + deviceId; }
    bool permitida(const std::string& ip) const;

    std::filesystem::path archivo_;
    std::filesystem::path datos_; // las caras propias: Media/botonera/<id>/
    Botonera& bot_;
    bool activo_ = false;
    int puerto_ = 16622;
    std::set<std::string> permitidas_;
    struct Pedido {
        std::string aparato; ///< PRODUCT_NAME (o DEVICEID) del primero que pidió
        std::int64_t ms = 0;
    };
    std::map<std::string, Pedido> pendientes_; ///< ip -> el pedido
    std::set<std::string> rechazadas_;         ///< en esta corrida: no vuelven a pedir
    QString error_;
    std::unique_ptr<QTcpServer> srv_;
    std::map<QTcpSocket*, Cliente> clientes_;
    QTimer titilar_;
    QTimer animar_; // las caras de video corto, a 8 cuadros/s
    bool fase_ = true;
};

} // namespace ba::app::control
