// src/app/sala/ClavesEvento.cpp — las claves de las superficies, del evento (ver el .hpp)

#include "sala/ClavesEvento.hpp"

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>

#include "crypto/Sha256.hpp"
#include "ipc/ProtocoloSala.hpp"
#include "security/CredentialStore.hpp"
#include "security/SecretsStore.hpp"
#include "sala/Pedido.hpp"
#include "ui/ConferenceClient.hpp"

namespace ba::app::sala {

namespace {
constexpr const char* kCampos[] = {"prompter", "correccion", "rotulos", "publico", "tablero"};
}

ClavesEvento::ClavesEvento(ConferenceClient& sala, std::function<std::filesystem::path()> carpeta, QObject* parent)
    : QObject(parent), sala_(sala), carpeta_(std::move(carpeta)) {
    sala_.registrarRepublicador("claves", this, [this]() { mandar(); });
    eventoCambio();
}

std::string ClavesEvento::nombreEnAlmacen(const std::filesystem::path& carpeta) {
    const std::string ruta = carpeta.lexically_normal().generic_string();
    return "claves-sala-" + ba::crypto::sha256Hex(ruta.data(), ruta.size()).substr(0, 32);
}

nlohmann::json ClavesEvento::leerOCrear(const std::filesystem::path& carpeta) {
    const auto almacen = ba::security::CredentialStore::bundle();
    const std::string nombre = nombreEnAlmacen(carpeta);
    std::string guardado;
    nlohmann::json c = almacen.read(nombre, guardado) ? nlohmann::json::parse(guardado, nullptr, false) : nlohmann::json();
    if (!c.is_object()) c = nlohmann::json::object();
    bool faltaba = false;
    for (const char* k : kCampos) {
        if (c.value(k, std::string()).size() >= 32) continue;
        std::string hex;
        if (!ba::security::randomSecretHex(16, hex)) return nullptr; // sin entropía: las de la sala
        c[k] = hex;
        faltaba = true;
    }
    if (faltaba && !almacen.write(nombre, c.dump())) return nullptr; // sin guardarlas no sobrevivirían
    return c;
}

void ClavesEvento::eventoCambio() {
    const std::filesystem::path carpeta = carpeta_ ? carpeta_() : std::filesystem::path();
    if (carpeta == deEstas_ && (!claves_.is_null() || carpeta.empty())) return;
    // Se cerró el evento que había (no el arranque sin ninguno: la sala puede tener una
    // votación viva que A todavía no guardó): que la sala olvide sus claves y lo suyo.
    const bool cerro = carpeta.empty() && !deEstas_.empty();
    deEstas_ = carpeta;
    claves_ = nullptr;
    if (carpeta.empty() && !cerro) return;
    const int pedido = ++pedido_;
    auto ultimo = ultimoPedido_;
    ultimo->store(pedido);
    QPointer<ClavesEvento> yo(this);
    QThreadPool::globalInstance()->start([yo, carpeta, pedido, ultimo]() {
        nlohmann::json c = carpeta.empty() ? nlohmann::json{{"sinEvento", true}} : leerOCrear(carpeta);
        // Las del evento en curso, para que una sala que arranca ya las tenga (no si llegó
        // otro pedido después: el de él es el que vale).
        if (ultimo->load() == pedido && !c.is_null()) {
            const auto almacen = ba::security::CredentialStore::bundle();
            if (carpeta.empty()) almacen.remove(ba::ipc::kClavesEnCurso);
            else almacen.write(ba::ipc::kClavesEnCurso, c.dump());
        }
        QMetaObject::invokeMethod(
            QCoreApplication::instance(), // de vuelta en el hilo de la interfaz
            [yo, c = std::move(c), pedido]() {
                if (!yo || pedido != yo->pedido_ || c.is_null()) return;
                yo->claves_ = c;
                yo->mandar();
            },
            Qt::QueuedConnection);
    });
}

void ClavesEvento::mandar() {
    if (claves_.is_null() || !sala_.online()) return;
    pedir(sala_, "POST", "/api/v1/claves", claves_, {});
}

} // namespace ba::app::sala
