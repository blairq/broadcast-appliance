// ============================================================================
// src/app/agent/ChatEnVivo.cpp — ver ChatEnVivo.hpp
// ============================================================================
#include "agent/ChatEnVivo.hpp"

#include <QCoreApplication>
#include <QPointer>

#include <algorithm>
#include <thread>

#include "ajustes/Ajustes.hpp"
#include "i18n/I18n.hpp"
#include "net/HttpsClient.hpp"
#include "platform/Hilo.hpp"

namespace ba::app::agent {

using nlohmann::json;

struct ChatEnVivo::Trabajo {
    FuenteMirada fuente;
    std::string cuerpo;
    QPointer<QObject> contexto;
    AlTexto alTexto;
    AlTerminar alTerminar;
    std::shared_ptr<std::atomic<bool>> cancel;
    /// Su lugar contra el servidor ya se devolvió (lo devuelve el primero: el hilo al
    /// terminar o cancelar(), que no espera a que el hilo se entere).
    std::atomic<bool> liberado{false};
    bool andando = false; ///< con m_
};

void ChatEnVivo::Pedido::cancelar() {
    cancel_->store(true);
    if (soltar_) soltar_();
}

ChatEnVivo& ChatEnVivo::el() {
    static ChatEnVivo c;
    return c;
}

ChatEnVivo::ChatEnVivo() { paralelo_ = std::clamp(ba::ajustes::entero("registro.agenteParalelo", 2), 1, 8); }

void ChatEnVivo::setParalelo(int n) {
    std::lock_guard<std::mutex> l(m_);
    paralelo_ = std::clamp(n, 1, 8);
}

int ChatEnVivo::paralelo() const {
    std::lock_guard<std::mutex> l(m_);
    return paralelo_;
}

int ChatEnVivo::enCurso(const std::string& url) const {
    std::lock_guard<std::mutex> l(m_);
    const auto it = servidores_.find(url);
    return it == servidores_.end() ? 0 : it->second.andando;
}

int ChatEnVivo::enCola(const std::string& url) const {
    std::lock_guard<std::mutex> l(m_);
    const auto it = servidores_.find(url);
    return it == servidores_.end() ? 0 : static_cast<int>(it->second.cola.size());
}

std::shared_ptr<ChatEnVivo::Pedido> ChatEnVivo::pedir(const FuenteMirada& f, const json& cuerpo, QObject* contexto,
                                                      AlTexto alTexto, AlTerminar alTerminar) {
    auto t = std::make_shared<Trabajo>();
    t->fuente = f;
    t->cuerpo = cuerpo.dump(-1, ' ', false, json::error_handler_t::replace);
    t->contexto = contexto;
    t->alTexto = std::move(alTexto);
    t->alTerminar = std::move(alTerminar);
    auto p = std::make_shared<Pedido>();
    t->cancel = p->cancel_;
    std::weak_ptr<Trabajo> debil = t;
    p->soltar_ = [this, debil]() {
        const auto t = debil.lock();
        if (!t) return;
        bool liberar = false;
        {
            std::lock_guard<std::mutex> l(m_);
            Servidor& s = servidores_[t->fuente.url];
            const auto it = std::find(s.cola.begin(), s.cola.end(), t);
            if (it != s.cola.end()) s.cola.erase(it); // en cola: nunca sale
            else liberar = t->andando && !t->liberado.exchange(true);
        }
        if (liberar) this->liberar(t->fuente.url);
    };
    bool ya = false;
    {
        std::lock_guard<std::mutex> l(m_);
        Servidor& s = servidores_[f.url];
        if (s.andando < paralelo_) {
            ++s.andando;
            t->andando = true;
            ya = true;
        } else {
            s.cola.push_back(t);
        }
    }
    if (ya) arrancar(t);
    return p;
}

void ChatEnVivo::liberar(const std::string& url) {
    std::shared_ptr<Trabajo> siguiente;
    {
        std::lock_guard<std::mutex> l(m_);
        Servidor& s = servidores_[url];
        s.andando = std::max(0, s.andando - 1);
        while (!s.cola.empty() && s.andando < paralelo_) {
            auto t = s.cola.front();
            s.cola.pop_front();
            if (t->cancel->load()) continue;
            ++s.andando;
            t->andando = true;
            siguiente = t;
            break;
        }
    }
    if (siguiente) arrancar(siguiente);
}

void ChatEnVivo::arrancar(const std::shared_ptr<Trabajo>& t) {
    // Un hilo por pedido, como Mirada: el cliente HTTP bloquea mientras lee, y los pedidos que
    // van juntos son pocos (paralelo).
    std::thread([this, t]() {
        ba::platform::nombrarHilo("chat-modelo");
        ba::registro::LectorSse lector;
        std::string url = t->fuente.url;
        while (!url.empty() && url.back() == '/') url.pop_back();
        net::HttpRequest req;
        req.method = "POST";
        req.url = url + "/chat/completions";
        req.headers = {{"Content-Type", "application/json"}, {"Accept", "text/event-stream"}};
        if (!t->fuente.clave.empty()) req.headers.push_back({"Authorization", "Bearer " + t->fuente.clave});
        req.body = t->cuerpo;
        // Por operación de red: el tiempo de la conversación lo mide quien pidió (AgenteTotem)
        // y cancela; esto solo asegura que el hilo no quede colgado para siempre.
        req.timeoutMs = ba::ajustes::sAMs("registro.agenteRedS", 30);
        req.cancel = t->cancel.get();
        const auto entregar = [t](std::function<void()> f) {
            QMetaObject::invokeMethod(
                qApp,
                [t, f = std::move(f)]() {
                    if (t->cancel->load() || t->contexto.isNull()) return;
                    f();
                },
                Qt::QueuedConnection);
        };
        bool avisado = false;
        req.bodyChunk = [&lector, &entregar, &avisado, t](const char* d, std::size_t n) {
            std::string nuevo = lector.agregar(std::string(d, n));
            // La primera vez se avisa aunque no haya texto: un modelo que solo llama
            // herramientas también empezó a contestar (quien espera deja de contar).
            if ((!nuevo.empty() || !avisado) && t->alTexto) {
                avisado = true;
                entregar([t, nuevo = std::move(nuevo)]() { t->alTexto(nuevo); });
            }
        };
        const net::HttpResponse r = net::HttpsClient::request(req);
        lector.terminar();
        std::string error;
        if (t->cancel->load()) error = "cancelado";
        else if (r.status == 0) error = ba::i18n::llenar(ba::i18n::traducir("asistente", "no contesta: %1"), {r.error});
        else if (r.status >= 400) {
            // Sin bodyChunk (no es 2xx), el cuerpo viene entero: lo que dice el servidor ayuda.
            const json j = json::parse(r.body, nullptr, false);
            std::string msg = j.is_object() && j.contains("error") && j["error"].is_object()
                                  ? j["error"].value("message", std::string())
                                  : std::string();
            error = msg.empty() ? ba::i18n::llenar(ba::i18n::traducir("asistente", "el servidor contestó %1"),
                                                   {std::to_string(r.status)})
                                : ba::i18n::llenar(ba::i18n::traducir("asistente", "el servidor contestó %1: %2"),
                                                   {std::to_string(r.status), msg});
        } else if (!lector.turno().error.empty()) {
            error = lector.turno().error;
        }
        if (!t->liberado.exchange(true)) liberar(t->fuente.url);
        if (error != "cancelado" && t->alTerminar)
            entregar([t, turno = lector.turno(), error]() { t->alTerminar(turno, error); });
    }).detach();
}

} // namespace ba::app::agent
