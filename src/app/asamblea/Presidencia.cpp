// src/app/asamblea/Presidencia.cpp — la presidencia y la sesión remota frente a la sala (ver el .hpp)

#include "asamblea/Presidencia.hpp"

#include "asamblea/Bancas.hpp"
#include "asamblea/ConferenciaDcn.hpp"
#include "asamblea/Cuerpo.hpp"
#include "asamblea/CuerpoViewModel.hpp"
#include "asamblea/Debate.hpp"
#include "asamblea/OrdenDelDia.hpp"
#include "asamblea/Quorum.hpp"
#include "asamblea/SesionRemota.hpp"
#include "asamblea/Votaciones.hpp"
#include "control/BusEventos.hpp"
#include "emision/BroadcastEngine.hpp"
#include "sala/Pedido.hpp"
#include "ui/ConferenceClient.hpp"

#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QUrl>

#include <algorithm>

namespace ba::app::asamblea {

namespace {
const auto sinRespuesta = [](const std::string&, const nlohmann::json&) {};
} // namespace

Presidencia::Presidencia(Partes p, QObject* parent)
    : QObject(parent), p_(std::move(p)), mediosSeq_(QDateTime::currentMSecsSinceEpoch()) {
    conectar();
    sincronizar();
}

Presidencia::~Presidencia() {
    if (oyentePunto_ != 0) p_.bus.desuscribir(oyentePunto_);
}

void Presidencia::conectar() {
    SesionRemota* s = &p_.sesion;
    // Los avisos de la banca virtual que manda el gateway (conexiones, entradas, intentos…).
    connect(&p_.sala, &ConferenceClient::sesionEvento, this, [s](const QJsonObject& o) {
        const json ev = json::parse(QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString(), nullptr, false);
        if (ev.is_object()) s->evento(ev);
    });
    connect(s, &SesionRemota::enviar, this,
            [this](const QString& id, const json& msg) { mandarA(id.toStdString(), msg); });
    connect(s, &SesionRemota::mediosA, this, &Presidencia::mandarMedios);
    connect(s, &SesionRemota::cortar, this, [this](const QString& id, const QString& motivo) {
        sala::pedir(p_.sala, "POST", "/api/v1/sesion/cortar", {{"id", id.toStdString()}, {"motivo", motivo.toStdString()}},
                    sinRespuesta);
    });
    connect(s, &SesionRemota::fotoPedida, this, [this](const QString& id) {
        QPointer<Presidencia> yo(this);
        p_.sala.getRaw("/api/v1/sesion/foto?id=" + QString::fromUtf8(QUrl::toPercentEncoding(id)),
                       [yo, id](bool ok, const QByteArray& b) {
                           if (ok && yo) yo->p_.sesion.ponerFoto(id, b);
                       });
    });
    // Pidió la palabra (o se arrepintió) desde el celular: la fila única.
    connect(s, &SesionRemota::pedidoPalabra, this, [this](const QString& id, bool pedir) {
        if (pedir) p_.bancas.agregar(id);
        else p_.bancas.quitar(id);
    });
    connect(s, &SesionRemota::credencialesCambio, this, &Presidencia::sincronizar);
    connect(s, &SesionRemota::cambio, this, [this]() { aplicarVistas(); });
    connect(&p_.sala, &ConferenceClient::onlineChanged, this, [this](bool on) {
        if (on) sincronizar();
    });
    // La consola web de la presidencia: el estado (como mucho 4 veces por segundo) y lo que pide.
    consolaTimer_.setSingleShot(true);
    consolaTimer_.setInterval(250);
    connect(&consolaTimer_, &QTimer::timeout, this, &Presidencia::mandarEstadoConsola);
    connect(s, &SesionRemota::cambio, this, &Presidencia::programarEstado);
    connect(&p_.bancas, &Bancas::cambio, this, &Presidencia::programarEstado);
    connect(this, &Presidencia::presidenciaHablaCambio, this, &Presidencia::programarEstado);
    if (p_.debate) {
        connect(p_.debate, &Debate::cambio, this, &Presidencia::programarEstado);
        connect(p_.debate, &Debate::cambio, this, [this]() { aplicarVistas(); });
    }
    connect(s, &SesionRemota::volvio, this, [this](const QString& id) {
        // Después de la palabra que le reenvía SesionRemota: el turno del debate manda.
        if (p_.debate && p_.debate->participa(id.toStdString())) {
            QPointer<Presidencia> yo(this);
            QTimer::singleShot(0, this, [yo]() {
                if (yo && yo->p_.debate) yo->p_.debate->reenviar();
            });
        }
    });
    connect(&p_.cuerpo, &CuerpoViewModel::cambio, this, &Presidencia::programarEstado);
    if (p_.quorum) connect(p_.quorum, &Quorum::cambio, this, &Presidencia::programarEstado);
    connect(s, &SesionRemota::consolaConexion, this, [this](const QString& id, bool on) {
        if (!on) return;
        mandarEstadoConsola();
        enlaceProgramaConsola(id.toStdString());
    });
    connect(s, &SesionRemota::accionConsola, this,
            [this](const QString& id, const json& m) { accionConsola(id.toStdString(), m); });
    // El punto del orden del día en el celular de cada uno.
    oyentePunto_ = p_.bus.suscribir("sesion/punto", [this](const std::string&, const json& punto) { avisarPunto(punto); });
    // La prioridad del presidente en su unidad.
    connect(&p_.dcn, &ConferenciaDcn::cambio, this, &Presidencia::prioridadCambio);
}

// --- La presidencia sobre todos ------------------------------------------------------

void Presidencia::silenciarRemotos() {
    // En un debate, callarlos es darle el turno a nadie (su imagen sigue entrando).
    if (p_.debate && p_.debate->activo()) p_.debate->turno("nadie");
    // El orador al aire, si habla por el celular, termina (eso le suelta los medios).
    const json aa = p_.bancas.estadoJson().value("al_aire", json());
    if (aa.is_object() && p_.sesion.tienePalabra(aa.value("id", std::string()))) p_.bancas.terminar();
    for (const auto& id : p_.sesion.conPalabraTodos())
        if (!p_.debate || !p_.debate->participa(id)) p_.sesion.darPalabra(id, false, 0);
}

void Presidencia::silenciarTodos(const std::string& quien, Listo listo) {
    p_.bancas.terminar();
    if (p_.debate && p_.debate->activo()) p_.debate->turno("nadie"); // los dos callados, el debate sigue
    silenciarRemotos();
    p_.bus.publicar("presidencia/silencio", {{"origen", quien}}, false);
    if (!p_.dcn.conectada()) {
        if (listo) listo({});
        return;
    }
    p_.dcn.control("cerrar_todos", {{"incluir_presidente", false}}, [listo](bool ok, const json&, const std::string& e) {
        if (listo) listo(ok ? std::string() : "Central: " + e);
    });
}

int Presidencia::unidadPresidente() const {
    for (const auto& u : p_.dcn.estado().value("unidades", json::array()))
        if (u.value("tipo", std::string()) == "presidente") return u.value("unidad", 0);
    return 0;
}

void Presidencia::hablaPresidencia(bool activo, const std::string& quien, Listo listo) {
    const auto avisar = [listo](const std::string& e) {
        if (listo) listo(e);
    };
    if (!p_.dcn.conectada()) return avisar("Sin la central no hay micrófono de la presidencia que abrir.");
    const int u = unidadPresidente();
    if (u == 0) return avisar("La central no tiene una unidad de presidente.");
    QPointer<Presidencia> yo(this);
    if (activo)
        silenciarTodos(quien, [yo](const std::string& e) {
            if (yo && !e.empty()) emit yo->aviso(QString::fromStdString(e), "error");
        });
    p_.dcn.control("microfono", {{"unidad", u}, {"abierto", activo}, {"confirmar", true}},
                   [yo, activo, avisar](bool ok, const json&, const std::string& e) {
                       if (!yo) return;
                       if (!ok) return avisar("Central: " + e);
                       yo->presidenciaHabla_ = activo;
                       emit yo->presidenciaHablaCambio();
                       avisar({});
                   });
    p_.bus.publicar("presidencia/habla", {{"activa", activo}, {"origen", quien}}, false);
}

void Presidencia::prioridadCambio() {
    // El presidente apretó prioridad en su unidad: la central ya calló a los suyos; los
    // remotos también se callan (si no, el que habla por el celular seguía sonando).
    const json pr = p_.dcn.estado().value("prioridad", json::object());
    const bool prio = pr.is_object() && pr.value("activa", false);
    if (prio && !prioridadCentral_) {
        silenciarRemotos();
        emit aviso("Prioridad del presidente: todos callados (también los remotos)", "info");
        p_.bus.publicar("presidencia/silencio", {{"origen", "prioridad"}}, false);
    }
    prioridadCentral_ = prio;
}

std::string Presidencia::hablarARemoto(const std::string& integrante, bool activo) {
    if (activo && !(p_.microfonoOperador && p_.microfonoOperador()))
        return "Para hablarle hace falta tu micrófono: elegilo en Sala → Tu cámara y micrófono.";
    if (activo && !p_.sala.online()) return "La sala (el gateway) no está andando.";
    sala::pedir(p_.sala, "POST", "/api/v1/sesion/hablar", {{"id", integrante}, {"activo", activo}}, sinRespuesta);
    // Su celular muestra «Te están hablando» y abre el sonido aunque esté en la sala.
    p_.sesion.ptt(integrante, activo, "operador");
    return {};
}

// --- Las consolas ----------------------------------------------------------------------

void Presidencia::mandarA(const std::string& destino, const json& msg) {
    sala::pedir(p_.sala, "POST", "/api/v1/sesion/enviar", {{"id", destino}, {"msg", msg}}, sinRespuesta);
}

void Presidencia::programarEstado() {
    if (p_.sesion.hayConsolas() && !consolaTimer_.isActive()) consolaTimer_.start();
}

Presidencia::json Presidencia::estadoConsola() const {
    json e{{"type", "estado"}, {"ahora_ms", QDateTime::currentMSecsSinceEpoch()}};
    e["cuerpo"] = p_.cuerpo.cuerpo().nombre();
    e["recinto"] = p_.cuerpo.cuerpo().recinto(true);
    e["remotos"] = p_.sesion.participantes();
    e["presidencia_habla"] = presidenciaHabla_;
    if (p_.debate) e["debate"] = p_.debate->estadoJson();
    e["con_central"] = p_.dcn.conectada();
    // La fila: cada uno con si habla desde el navegador.
    const json b = p_.bancas.estadoJson();
    const auto persona = [this](const json& i) {
        const std::string id = i.value("id", std::string());
        return json{{"id", id}, {"nombre", i.value("nombre", std::string())}, {"banca", i.value("banca", std::string())},
                    {"bloque", i.value("bloque", std::string())}, {"remoto", p_.sesion.presente(id)}};
    };
    json cola = json::array();
    for (const auto& i : b.value("cola", json::array())) cola.push_back(persona(i));
    const json aa = b.value("al_aire", json());
    e["fila"] = {{"cola", cola}, {"al_aire", aa.is_object() ? persona(aa) : json()}, {"desde_ms", b.value("desde_ms", 0LL)},
                 {"asignado_s", b.value("asignado_s", 0)}, {"asignado_fabrica_s", b.value("asignado_fabrica_s", 0)}};
    // La central.
    if (p_.dcn.conectada()) {
        const json d = p_.dcn.estado();
        e["central"] = {{"conectada", true}, {"abiertos", d.value("abiertos", json::array())},
                        {"pedidos", d.value("pedidos", json::array())}, {"votacion", d.value("votacion", json::object())}};
    } else {
        e["central"] = {{"conectada", false}};
    }
    e["votacion"] = p_.sesion.votacion();
    e["voto_s"] = p_.sesion.votoSegundos();
    if (p_.quorum) e["quorum"] = p_.quorum->estado();
    if (p_.ordenDia) {
        const json actual = p_.ordenDia->actual();
        e["punto"] = actual.is_object() ? actual.value("titulo", std::string()) : std::string();
        json prox = json::array();
        for (const auto& x : p_.ordenDia->puntos())
            if (x.value("estado", std::string()) == "pendiente" && prox.size() < 8)
                prox.push_back({{"n", x.value("n", 0)}, {"titulo", x.value("titulo", std::string())}});
        e["proximos"] = prox;
    }
    return e;
}

void Presidencia::mandarEstadoConsola() {
    if (!p_.sesion.hayConsolas() || !p_.sala.online()) return;
    mandarA("consolas", estadoConsola());
}

void Presidencia::enlaceProgramaConsola(const std::string& consola) {
    QPointer<Presidencia> yo(this);
    // El programa en vivo (el mismo de los televisores, /cast/), con su token.
    sala::pedir(p_.sala, "GET", "/api/v1/sesion/cast", {}, [yo, consola](const std::string& e, const json& r) {
        if (!yo || !e.empty() || !r.value("ok", false)) return;
        yo->mandarA(consola, {{"type", "cast"}, {"url", r.value("url", std::string())}});
    });
}

void Presidencia::accionConsola(const std::string& consola, const json& m) {
    const std::string t = m.value("type", std::string());
    const std::string id = m.value("id", std::string());
    const std::string quien = "presidencia (" + (consola.size() > 8 ? consola.substr(8) : consola) + ")";
    // Lo que falla vuelve a la consola que lo pidió; lo que contesta después (la central)
    // también se le avisa al operador, como siempre.
    QPointer<Presidencia> yo(this);
    const auto responder = [yo, consola](const std::string& e) {
        if (yo && !e.empty()) yo->mandarA(consola, {{"type", "error"}, {"texto", e}});
    };
    const auto responderDespues = [yo, responder](const std::string& e) {
        if (!yo || e.empty()) return;
        responder(e);
        emit yo->aviso(QString::fromStdString(e), "error");
    };
    std::string error;
    if (t == "hola") {
        mandarEstadoConsola();
        enlaceProgramaConsola(consola);
    } else if (t == "habilitar" || t == "deshabilitar") {
        error = p_.sesion.habilitar(id, t == "habilitar", quien);
    } else if (t == "expulsar") {
        error = p_.sesion.expulsar(id, quien, m.value("motivo", std::string()));
    } else if (t == "dar_palabra") {
        error = p_.bancas.alAire(QString::fromStdString(id), "armar", m.value("segundos", -1)).toStdString();
    } else if (t == "terminar") {
        p_.bancas.terminar();
    } else if (t == "debate" && p_.debate) {
        const std::string acc = m.value("accion", std::string());
        if (acc == "iniciar")
            error = p_.debate->iniciar(m.value("a", std::string()), m.value("b", std::string()), m.value("turno", std::string("a")));
        else if (acc == "turno")
            error = p_.debate->turno(m.value("turno", std::string()));
        else if (acc == "sumar")
            p_.debate->sumar(m.value("segundos", 60));
        else if (acc == "terminar")
            p_.debate->terminar();
    } else if (t == "silenciar_todos") {
        silenciarTodos(quien, responderDespues);
    } else if (t == "hablar_presidencia") {
        hablaPresidencia(m.value("activo", true), quien, responderDespues);
    } else if (t == "quitar_fila") {
        p_.bancas.quitar(QString::fromStdString(id));
    } else if (t == "siguiente") {
        error = p_.bancas.siguiente("armar").toStdString();
    } else if (t == "sumar_tiempo") {
        p_.bancas.sumarTiempo(m.value("segundos", 60));
    } else if (t == "tiempo") {
        p_.bancas.setAsignado(m.value("asignado_s", 180));
    } else if (t == "votacion" && p_.votaciones) {
        // Central o celulares lo decide asamblea::Votaciones.
        p_.votaciones->accion(m.value("accion", std::string()), m, quien, responderDespues);
    } else if (t == "ptt") {
        // «Apretar para hablar»: le avisa al integrante; el audio y el video van
        // directo entre la consola y su navegador (WebRTC, señalizado por el gateway).
        p_.sesion.ptt(id, m.value("activo", false), consola);
    } else if (t == "foto_cuerpo") {
        const QString ruta = p_.cuerpo.rutaFoto(QString::fromStdString(id));
        QFile f(ruta);
        if (!ruta.isEmpty() && f.open(QIODevice::ReadOnly) && f.size() < 200000)
            mandarA(consola, {{"type", "foto_cuerpo"}, {"id", id}, {"jpeg", f.readAll().toBase64().toStdString()}});
    } else if (t == "cast") {
        enlaceProgramaConsola(consola);
    }
    responder(error);
    if (!consolaTimer_.isActive()) consolaTimer_.start();
}

// --- La sesión remota con el gateway ----------------------------------------------------

void Presidencia::sincronizar() {
    if (!p_.sala.online()) return;
    sala::pedir(p_.sala, "POST", "/api/v1/sesion/credenciales", {{"participantes", p_.sesion.paraGateway()}}, sinRespuesta);
    aplicarVistas(true);
    // Si el gateway se reinició no sabe quién tiene la palabra: se le vuelve a decir.
    json ids = json::array();
    for (const auto& i : p_.sesion.conPalabraTodos()) ids.push_back(i);
    sala::pedir(p_.sala, "POST", "/api/v1/sesion/medios", {{"ids", ids}, {"seq", ++mediosSeq_}}, sinRespuesta);
    // El origen de los links: el del túnel si hay; si no, el de la red local (HTTPS).
    QPointer<Presidencia> yo(this);
    sala::pedir(p_.sala, "GET", "/api/v1/sesion/origen?target=remote", {}, [yo](const std::string& e, const json& r) {
        if (!yo) return;
        if (e.empty() && r.value("ok", false)) {
            yo->p_.sesion.setOrigen(r.value("origen", std::string()));
            return;
        }
        sala::pedir(yo->p_.sala, "GET", "/api/v1/sesion/origen?target=local", {}, [yo](const std::string& e2, const json& r2) {
            if (yo && e2.empty() && r2.value("ok", false)) yo->p_.sesion.setOrigen(r2.value("origen", std::string()));
        });
    });
}

void Presidencia::mandarMedios(const QStringList& ids) {
    // Quiénes mandan medios a la sala: va solo la lista final de cada tanda (un debate
    // da la palabra a dos seguidos) y con número, así una que llega tarde no pisa
    // a la nueva (los POST viajan en paralelo y llegaban al revés).
    mediosPendientes_ = ids;
    if (mediosProgramados_) return;
    mediosProgramados_ = true;
    QTimer::singleShot(0, this, [this]() {
        mediosProgramados_ = false;
        json l = json::array();
        for (const QString& i : mediosPendientes_) l.push_back(i.toStdString());
        sala::pedir(p_.sala, "POST", "/api/v1/sesion/medios", {{"ids", l}, {"seq", ++mediosSeq_}}, sinRespuesta);
    });
}

void Presidencia::aplicarVistas(bool forzar) {
    if (!p_.sala.online()) return;
    // Las escenas se componen solo mientras haya algún remoto conectado.
    const bool alguien = p_.sesion.conectados() > 0;
    json vistas = p_.sesion.vistas();
    // Durante un debate, un canal más con la escena del debate: los remotos se ven entre
    // ellos (los dos del debate pasan solos a ese canal).
    if (p_.debate && p_.debate->activo()) {
        const std::string esc = p_.debate->estadoJson().value("escena", std::string());
        if (!esc.empty()) vistas.push_back({{"id", "debate"}, {"nombre", "Debate"}, {"fuente", esc}});
    }
    const std::string firma = (alguien ? "1" : "0") + vistas.dump(-1, ' ', false, json::error_handler_t::replace);
    if (!forzar && firma == firmaVistas_) return;
    firmaVistas_ = firma;
    p_.motor.soltarCanalesEscena("sesion-remota");
    json out = json::array();
    for (const auto& v : vistas) {
        const std::string fuente = v.value("fuente", std::string());
        int canal = -2;
        if (fuente == "programa") {
            canal = -1;
        } else if (alguien) {
            canal = p_.motor.tomarCanalEscena(fuente, "sesion-remota");
            if (canal < 0)
                emit aviso(QString::fromStdString("No hay canal libre para mandarles la escena «" + fuente +
                                                  "» a los remotos (las pantallas usan los 4)"),
                           "warning");
        }
        out.push_back({{"id", v.value("id", std::string())}, {"nombre", v.value("nombre", std::string())}, {"canal", canal}});
    }
    sala::pedir(p_.sala, "POST", "/api/v1/sesion/vistas", {{"vistas", out}}, sinRespuesta);
}

void Presidencia::avisarPunto(const json& p) {
    const std::string t = p.is_object() ? p.value("titulo", p.value("texto", std::string())) : std::string();
    const int n = p.is_object() ? p.value("n", 0) : 0;
    p_.sesion.info({{"punto", t.empty() ? std::string() : (n > 0 ? std::to_string(n) + ". " : "") + t}});
    // Y los que vienen (los temas a votar), hasta cinco.
    if (p_.ordenDia) {
        json prox = json::array();
        for (const auto& x : p_.ordenDia->puntos())
            if (x.value("estado", std::string()) == "pendiente" && prox.size() < 5)
                prox.push_back(std::to_string(x.value("n", 0)) + ". " + x.value("titulo", std::string()));
        p_.sesion.setProximos(prox);
    }
}

} // namespace ba::app::asamblea
