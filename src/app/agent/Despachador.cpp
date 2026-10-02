// ============================================================================
// src/app/agent/Despachador.cpp — ver Despachador.hpp
// ============================================================================
#include "agent/Despachador.hpp"

#include <algorithm>

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>

#include "agent/Roles.hpp"
#include "diag/Registro.hpp"

namespace ba::app::agent {

using json = nlohmann::json;

namespace {

std::int64_t ahoraMs() { return QDateTime::currentMSecsSinceEpoch(); }

std::string hora(std::int64_t ms) {
    return QDateTime::fromMSecsSinceEpoch(ms).toString("HH:mm:ss").toStdString();
}

/// El nombre de la herramienta sin el prefijo del servidor MCP.
std::string sinPrefijo(std::string n) {
    const std::string pre = "mcp__broadcast__";
    if (n.rfind(pre, 0) == 0) n = n.substr(pre.size());
    return n;
}

std::string recortar(const std::string& s, std::size_t max = 400) {
    if (s.size() <= max) return s;
    return s.substr(0, max) + "…";
}

/// Texto plano de un `content` de tool_result (lista de bloques o string).
std::string textoDe(const json& content) {
    if (content.is_string()) return content.get<std::string>();
    std::string out;
    if (content.is_array())
        for (const auto& b : content)
            if (b.is_object() && b.value("type", "") == "text") out += b.value("text", "");
    return out;
}

QVariantMap aMapa(const json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(j.dump())).object().toVariantMap();
}
QVariantList aLista(const json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(j.dump())).array().toVariantList();
}

void registrar(diag::Nivel n, const std::string& rol, const std::string& texto) {
    diag::registro().linea(n, "agente." + rol, 0, texto);
}

} // namespace

struct Despachador::Corrida {
    std::string id;
    std::string rol;
    std::string origen;
    std::string encargo;
    std::vector<std::string> adjuntos; ///< archivos que van CON el encargo
    std::string estado = "esperando";  ///< esperando | corriendo | ok | error | detenida
    int profundidad = 0;
    std::int64_t creadaMs = 0;
    std::int64_t inicioMs = 0;
    std::int64_t finMs = 0;
    json pasos = json::array();
    std::string resultado; ///< el texto final del agente
    std::string resumen;   ///< lo que sigue a RESUMEN: (o el final, recortado)
    std::string error;
    int herramientas = 0;
    QProcess* proc = nullptr;
    QTimer* tope = nullptr;
    std::string buffer;
    std::string stderrTail;
};

/// Eventos que un rol junta antes de lanzar UNA corrida.
struct Despachador::Pendiente {
    std::vector<json> eventos;
    QTimer* timer = nullptr;
};

Despachador::Despachador(Roles& roles, Motores motores, QObject* parent)
    : QObject(parent), roles_(roles), motores_(std::move(motores)) {}

Despachador::~Despachador() {
    for (auto& c : corridas_)
        if (c->proc != nullptr) {
            c->proc->kill();
            c->proc->waitForFinished(1500);
        }
}

// --- Entrada: eventos y encargos -----------------------------------------------

bool Despachador::evento(const json& ev) {
    bool llm = false;
    for (const auto& n : ev.value("notify", json::array()))
        if (n == "llm") llm = true;
    // Un disparador sin "llm" es del operador (aviso en pantalla, acciones
    // directas): no despierta a nadie.
    if (!llm) return false;
    const std::string tipo = ev.value("type", "");
    const Rol* rol = roles_.porDisparador(tipo);
    if (rol == nullptr) return false;
    emit eventoTomado(QString::fromStdString(rol->id), aMapa(ev));

    // Sin agrupacion: una corrida por evento, ya.
    if (rol->agruparMs <= 0 || rol->agruparMax <= 1) {
        const std::string texto = "[" + hora(ev.value("atMs", std::int64_t{0})) + "] " +
                                  ev.value("llmText", ev.value("text", "")) + " (disparador " +
                                  ev.value("ruleId", "") + ", tipo " + tipo + ")";
        std::string err;
        if (encargar(rol->id, texto, "disparador:" + tipo, 0, &err).empty())
            registrar(diag::Nivel::Aviso, rol->id, "no se pudo encargar: " + err);
        return true;
    }
    // Con agrupacion: se junta lo que llegue en la ventana.
    auto& p = agrupando_[rol->id];
    if (!p) {
        p = std::make_unique<Pendiente>();
        p->timer = new QTimer(this);
        p->timer->setSingleShot(true);
        const std::string id = rol->id;
        connect(p->timer, &QTimer::timeout, this, [this, id]() { lanzar(id); });
    }
    p->eventos.push_back(ev);
    if (static_cast<int>(p->eventos.size()) >= rol->agruparMax) {
        p->timer->stop();
        lanzar(rol->id);
    } else if (!p->timer->isActive()) {
        p->timer->start(rol->agruparMs);
    }
    return true;
}

void Despachador::lanzar(const std::string& rol) {
    auto it = agrupando_.find(rol);
    if (it == agrupando_.end() || !it->second || it->second->eventos.empty()) return;
    std::string texto;
    for (const json& ev : it->second->eventos)
        texto += "[" + hora(ev.value("atMs", std::int64_t{0})) + "] " +
                 ev.value("llmText", ev.value("text", "")) + " (disparador " + ev.value("ruleId", "") +
                 ", tipo " + ev.value("type", "") + ")\n";
    const std::size_t n = it->second->eventos.size();
    it->second->eventos.clear();
    std::string err;
    if (encargar(rol, texto, n == 1 ? "disparador" : "disparadores (" + std::to_string(n) + ")", 0, &err)
            .empty())
        registrar(diag::Nivel::Aviso, rol, "no se pudo encargar: " + err);
}

std::string Despachador::encargar(const std::string& rolId, const std::string& texto,
                                  const std::string& origen, int profundidad, std::string* error,
                                  const std::vector<std::string>& adjuntos) {
    const Rol* rol = roles_.porId(rolId);
    if (rol == nullptr) {
        if (error != nullptr) *error = "no existe el rol `" + rolId + "`";
        return {};
    }
    if (!rol->activo) {
        if (error != nullptr) *error = "el rol `" + rolId + "` esta apagado (pagina Agentes)";
        return {};
    }
    if (profundidad > kProfundidadMax) {
        if (error != nullptr)
            *error = "un encargo de otro agente no puede encargar a un tercero (profundidad maxima " +
                     std::to_string(kProfundidadMax) + ")";
        return {};
    }
    if (texto.empty()) {
        if (error != nullptr) *error = "el encargo esta vacio";
        return {};
    }
    // El mismo origen ya esta en curso o esperando: no se duplica (dos
    // caminos —el disparador y el click del operador— pueden traer a la
    // misma persona).
    if (origen.rfind("lobby:", 0) == 0)
        for (const auto& c : corridas_)
            if (c->origen == origen && (c->estado == "esperando" || c->estado == "corriendo")) return c->id;

    auto c = std::make_unique<Corrida>();
    c->id = "c" + std::to_string(++seq_);
    c->rol = rolId;
    c->origen = origen;
    c->encargo = texto;
    c->adjuntos = adjuntos;
    c->profundidad = profundidad;
    c->creadaMs = ahoraMs();
    const std::string id = c->id;
    corridas_.push_front(std::move(c));
    while (corridas_.size() > kCorridasGuardadas) {
        // Nunca se tira una en curso.
        auto& ultima = corridas_.back();
        if (ultima->proc != nullptr) break;
        corridas_.pop_back();
    }
    registrar(diag::Nivel::Info, rolId, "encargo " + id + " (" + origen + "): " + recortar(texto, 160));
    if (enCursoDe(rolId) == nullptr)
        lanzarAhora(*porId(id));
    else
        cola_[rolId].push_back(id);
    emit cambio();
    return id;
}

// --- Lanzar y seguir una corrida -------------------------------------------------

std::string Despachador::contextoPara(const std::string& rol) const {
    std::string out;
    const auto it = contexto_.find(rol);
    if (it != contexto_.end() && it->second) {
        const std::string c = it->second();
        if (!c.empty()) out += c + "\n\n";
    }
    // Memoria como dato: los resumenes de las ultimas corridas de este rol.
    std::vector<std::string> previos;
    for (const auto& c : corridas_) {
        if (c->rol != rol || c->estado == "esperando" || c->estado == "corriendo") continue;
        if (c->resumen.empty()) continue;
        previos.push_back("- [" + hora(c->finMs) + ", " + c->estado + "] " + recortar(c->resumen, 300));
        if (previos.size() >= static_cast<std::size_t>(kResumenesDeContexto)) break;
    }
    if (!previos.empty()) {
        out += "Lo que paso antes hoy con tu rol (mas reciente primero):\n";
        for (const auto& p : previos)
            out += p + "\n";
        out += "\n";
    }
    return out;
}

void Despachador::lanzarAhora(Corrida& c) {
    const Rol* rol = roles_.porId(c.rol);
    if (rol == nullptr) {
        terminar(c, "error", "el rol desaparecio");
        return;
    }
    Lanzamiento l;
    l.rol = QString::fromStdString(rol->id);
    l.proveedor = QString::fromStdString(rol->proveedor);
    l.modelo = QString::fromStdString(rol->modelo);
    l.prompt = QString::fromStdString(rol->prompt);
    l.consola = rol->consola;
    l.encargo = QString::fromStdString(contextoPara(c.rol) + "ENCARGO:\n" + c.encargo);
    for (const std::string& f : c.adjuntos)
        l.adjuntos << QString::fromStdString(f);
    // El modelo local no es un programa: lo atiende la app (sin contexto ni
    // instrucciones del rol: solo el encargo, que es lo que entiende).
    if (motores_.local) {
        Lanzamiento crudo = l;
        crudo.encargo = QString::fromStdString(c.encargo);
        std::string resumen;
        if (motores_.local(crudo, c.origen, &resumen)) {
            c.inicioMs = ahoraMs();
            c.resultado = "RESUMEN: " + resumen;
            terminar(c, "ok", "");
            return;
        }
    }
    QString err;
    const term::Pty::Options o = motores_.lanzar ? motores_.lanzar(l, &err) : term::Pty::Options{};
    if (o.program.isEmpty()) {
        terminar(c, "error", err.isEmpty() ? "no se pudo preparar el agente" : err.toStdString());
        return;
    }
    c.proc = new QProcess(this);
    c.proc->setWorkingDirectory(o.workDir);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const QString& k : o.unsetEnv)
        env.remove(k);
    for (auto it = o.env.begin(); it != o.env.end(); ++it)
        env.insert(it.key(), it.value());
    c.proc->setProcessEnvironment(env);
    c.proc->setProgram(o.program);
    c.proc->setArguments(o.args);
    const std::string id = c.id;
    connect(c.proc, &QProcess::readyReadStandardOutput, this, [this, id]() {
        if (Corrida* cc = porId(id)) leerSalida(*cc);
    });
    connect(c.proc, &QProcess::readyReadStandardError, this, [this, id]() {
        Corrida* cc = porId(id);
        if (cc == nullptr || cc->proc == nullptr) return;
        cc->stderrTail += cc->proc->readAllStandardError().toStdString();
        if (cc->stderrTail.size() > 2000)
            cc->stderrTail = cc->stderrTail.substr(cc->stderrTail.size() - 2000);
    });
    connect(c.proc, &QProcess::finished, this, [this, id](int code, QProcess::ExitStatus st) {
        Corrida* cc = porId(id);
        if (cc == nullptr) return;
        leerSalida(*cc);
        if (!cc->buffer.empty()) {
            procesarLinea(*cc, cc->buffer);
            cc->buffer.clear();
        }
        if (cc->estado == "detenida") {
            terminar(*cc, "detenida", {});
        } else if (st != QProcess::NormalExit || code != 0) {
            std::string e = "el agente termino con codigo " + std::to_string(code);
            if (!cc->stderrTail.empty()) e += ": " + recortar(cc->stderrTail, 300);
            terminar(*cc, "error", e);
        } else {
            terminar(*cc, cc->error.empty() ? "ok" : "error", cc->error);
        }
    });
    connect(c.proc, &QProcess::errorOccurred, this, [this, id](QProcess::ProcessError e) {
        Corrida* cc = porId(id);
        if (cc == nullptr || e != QProcess::FailedToStart) return;
        terminar(*cc, "error", "no se pudo iniciar el agente (" + cc->proc->program().toStdString() + ")");
    });
    c.tope = new QTimer(this);
    c.tope->setSingleShot(true);
    connect(c.tope, &QTimer::timeout, this, [this, id]() {
        Corrida* cc = porId(id);
        if (cc == nullptr || cc->proc == nullptr) return;
        cc->error = "se paso del tope de " + std::to_string(kTopeMinutos) + " minutos";
        cc->proc->kill();
    });
    c.tope->start(kTopeMinutos * 60 * 1000);
    c.estado = "corriendo";
    c.inicioMs = ahoraMs();
    registrar(diag::Nivel::Info, c.rol,
              "corrida " + c.id + " arranca con " + o.program.toStdString() + " (" +
                  (rol->consola ? "con consola, " : "") +
                  (rol->proveedor.empty() ? "proveedor del asistente" : rol->proveedor) +
                  (rol->modelo.empty() ? "" : ", " + rol->modelo) + ")");
    // EOF EN STDIN, EN CUANTO EL PROCESO EXISTA. Sin esto opencode se queda
    // esperando que le escriban por la entrada —el encargo va por argumento, no
    // por stdin— y no arranca nunca: 0 lineas de salida, 0 herramientas,
    // minutos parado. Su propio aviso lo decia y tardamos en leerlo: «no stdin
    // data received in 3s... redirect stdin explicitly: < /dev/null to skip».
    //
    // El asistente nunca lo sufrio porque no usa QProcess para esto, asi que el
    // agujero solo aparecia al correr un ROL con el motor opencode. Con claude
    // tampoco se veia: ese no espera nada por la entrada.
    //
    // EN `started` Y NO DESPUES DE `start()`: `start()` es asincrono y cerrarle
    // el canal a un proceso que todavia no existe aborta el programa entero
    // (se vio: SIGABRT cuatro segundos despues de encargar, y la app se
    // reinicio sola perdiendo la corrida).
    const QString idProc = QString::fromStdString(c.id);
    connect(c.proc, &QProcess::started, this, [this, idProc]() {
        Corrida* cc = porId(idProc.toStdString());
        if (cc != nullptr && cc->proc != nullptr) cc->proc->closeWriteChannel();
    });
    c.proc->start();
    emit cambio();
}

void Despachador::leerSalida(Corrida& c) {
    if (c.proc == nullptr) return;
    c.buffer += c.proc->readAllStandardOutput().toStdString();
    std::size_t pos;
    while ((pos = c.buffer.find('\n')) != std::string::npos) {
        const std::string linea = c.buffer.substr(0, pos);
        c.buffer.erase(0, pos + 1);
        if (!linea.empty()) procesarLinea(c, linea);
    }
}

/// Un renglon JSON del motor -> pasos de la corrida. Dos dialectos:
///   Claude Code (stream-json): {type: system|assistant|user|result, ...}
///   opencode (--format json):  {type: text|tool_use|..., part: {...}} (se
///                              lee a la defensiva: lo que no se entiende se
///                              guarda crudo, con su `type`).
namespace {

/// El texto de una clave, CUALQUIERA sea su tipo. `json::value<std::string>`
/// con un default de texto TIRA `type_error` si el valor no es texto, y la
/// salida de un agente no tiene una forma garantizada: opencode manda `error`
/// como OBJETO ({name, data:{message,...}}) y eso abortaba la app entera.
std::string textoClave(const json& j, const char* clave) {
    if (!j.is_object() || !j.contains(clave)) return {};
    const json& v = j[clave];
    if (v.is_string()) return v.get<std::string>();
    if (v.is_null()) return {};
    // Un objeto o una lista: se busca el mensaje adentro, y si no hay se
    // devuelve como vino. Perder el motivo de un error es peor que mostrarlo
    // feo.
    if (v.is_object()) {
        for (const char* k : {"message", "error", "text"})
            if (v.contains(k) && v[k].is_string()) return v[k].get<std::string>();
        if (v.contains("data") && v["data"].is_object() && v["data"].contains("message") &&
            v["data"]["message"].is_string())
            return v["data"]["message"].get<std::string>();
    }
    return v.dump();
}

} // namespace

void Despachador::procesarLinea(Corrida& c, const std::string& linea) {
    // TODO LO DE ADENTRO ES ENTRADA DE AFUERA. Lo escribe otro programa y su
    // formato cambia entre versiones: una excepcion de JSON aca se lleva la
    // aplicacion entera, con el aire encendido. Se vio: opencode informo un
    // 401, `error` venia como objeto y la app aborto (SIGABRT).
    try {
        procesarLineaInterna(c, linea);
    } catch (const std::exception& e) {
        paso(c, json{{"tipo", "crudo"},
                     {"texto",
                      recortar(std::string("no pude leer una linea del motor (") + e.what() + "): " + linea,
                               300)}});
    }
}

void Despachador::procesarLineaInterna(Corrida& c, const std::string& linea) {
    const json j = json::parse(linea, nullptr, false);
    if (!j.is_object()) {
        // Texto suelto (un aviso del motor): se guarda como tal.
        paso(c, json{{"tipo", "crudo"}, {"texto", recortar(linea)}});
        return;
    }
    const std::string tipo = j.value("type", "");
    // --- Claude Code ---
    if (tipo == "system") {
        if (j.value("subtype", "") == "init")
            paso(c, json{{"tipo", "inicio"},
                         {"texto", "modelo " + j.value("model", "?") + ", " +
                                       std::to_string(j.value("tools", json::array()).size()) +
                                       " herramientas"}});
        return;
    }
    if (tipo == "assistant" || tipo == "user") {
        const json msg = j.value("message", json::object());
        for (const auto& b : msg.value("content", json::array())) {
            if (!b.is_object()) continue;
            const std::string bt = b.value("type", "");
            if (bt == "text" && tipo == "assistant") {
                const std::string t = b.value("text", "");
                if (!t.empty()) {
                    paso(c, json{{"tipo", "texto"}, {"texto", t}});
                    c.resultado = t;
                }
            } else if (bt == "tool_use") {
                ++c.herramientas;
                paso(c, json{{"tipo", "herramienta"},
                             {"herramienta", sinPrefijo(b.value("name", ""))},
                             {"args", b.value("input", json::object())}});
            } else if (bt == "tool_result") {
                paso(c, json{{"tipo", "resultado"},
                             {"error", b.value("is_error", false)},
                             {"texto", recortar(textoDe(b.value("content", json())))}});
            }
        }
        return;
    }
    if (tipo == "result") {
        const std::string r = j.value("result", "");
        if (!r.empty()) c.resultado = r;
        if (j.value("is_error", false)) c.error = recortar(r.empty() ? "el agente informo un error" : r, 300);
        json fin{
            {"tipo", "fin"}, {"turnos", j.value("num_turns", 0)}, {"duracionMs", j.value("duration_ms", 0)}};
        if (j.contains("total_cost_usd")) fin["costoUsd"] = j["total_cost_usd"];
        paso(c, fin);
        return;
    }
    // --- opencode (y cualquier otro): a la defensiva ---
    const json part = j.value("part", json::object());
    if (tipo == "text" || part.value("type", "") == "text") {
        const std::string t = part.value("text", j.value("text", ""));
        if (!t.empty()) {
            paso(c, json{{"tipo", "texto"}, {"texto", t}});
            c.resultado = t;
        }
        return;
    }
    if (tipo == "tool_use" || tipo == "tool" || part.value("type", "") == "tool") {
        ++c.herramientas;
        json p{{"tipo", "herramienta"},
               {"herramienta", sinPrefijo(part.value("tool", j.value("name", j.value("tool", ""))))}};
        const json st = part.value("state", json::object());
        if (st.contains("input"))
            p["args"] = st["input"];
        else if (j.contains("input"))
            p["args"] = j["input"];
        if (st.contains("output")) p["resultado"] = recortar(textoDe(st["output"]));
        paso(c, p);
        return;
    }
    if (tipo == "error") {
        std::string motivo = textoClave(j, "message");
        if (motivo.empty()) motivo = textoClave(j, "error");
        if (motivo.empty()) motivo = "error del motor";
        c.error = recortar(motivo, 300);
        paso(c, json{{"tipo", "crudo"}, {"texto", c.error}});
        return;
    }
    if (tipo == "step_start" || tipo == "step_finish" || tipo == "step-start" || tipo == "step-finish")
        return;
    paso(c, json{{"tipo", "crudo"}, {"texto", recortar(tipo.empty() ? linea : tipo + ": " + linea, 300)}});
}

void Despachador::paso(Corrida& c, json p) {
    p["t"] = hora(ahoraMs());
    if (p.value("tipo", "") == "herramienta")
        registrar(diag::Nivel::Depuracion, c.rol,
                  c.id + " llama " + p.value("herramienta", "") + " " +
                      recortar(p.value("args", json::object()).dump(), 200));
    c.pasos.push_back(std::move(p));
    emit cambio();
}

void Despachador::terminar(Corrida& c, const std::string& estado, const std::string& error) {
    if (c.tope != nullptr) {
        c.tope->stop();
        c.tope->deleteLater();
        c.tope = nullptr;
    }
    if (c.proc != nullptr) {
        c.proc->deleteLater();
        c.proc = nullptr;
    }
    c.estado = estado;
    c.error = error;
    c.finMs = ahoraMs();
    // El resumen: lo que sigue a RESUMEN: si el agente lo puso; si no, el
    // final de lo que dijo.
    const auto r = c.resultado.rfind("RESUMEN:");
    c.resumen = r != std::string::npos ? c.resultado.substr(r + 8) : c.resultado;
    while (!c.resumen.empty() && (c.resumen.front() == ' ' || c.resumen.front() == '\n'))
        c.resumen.erase(0, 1);
    c.resumen = recortar(c.resumen, 600);
    const std::string dur =
        c.inicioMs > 0 ? std::to_string((c.finMs - c.inicioMs) / 1000) + " s" : "sin arrancar";
    if (estado == "ok")
        registrar(diag::Nivel::Info, c.rol,
                  "corrida " + c.id + " termino bien (" + dur + ", " + std::to_string(c.herramientas) +
                      " herramientas): " + recortar(c.resumen, 200));
    else
        registrar(diag::Nivel::Aviso, c.rol, "corrida " + c.id + " " + estado + " (" + dur + "): " + error);
    if (estado == "error")
        emit notify(QString::fromStdString("Agente " + c.rol + ": " + recortar(error, 120)), "error");
    const std::string rol = c.rol;
    const std::string id = c.id;
    const bool ok = estado == "ok";
    emit corridaTerminada(QString::fromStdString(id), ok);
    // La siguiente del mismo rol, si hay.
    auto& q = cola_[rol];
    while (!q.empty()) {
        const std::string sig = q.front();
        q.pop_front();
        if (Corrida* n = porId(sig); n != nullptr && n->estado == "esperando") {
            lanzarAhora(*n);
            break;
        }
    }
    emit cambio();
}

// --- Consulta -------------------------------------------------------------------

Despachador::Corrida* Despachador::porId(const std::string& id) {
    for (auto& c : corridas_)
        if (c->id == id) return c.get();
    return nullptr;
}
const Despachador::Corrida* Despachador::porId(const std::string& id) const {
    for (const auto& c : corridas_)
        if (c->id == id) return c.get();
    return nullptr;
}
Despachador::Corrida* Despachador::enCursoDe(const std::string& rol) {
    for (auto& c : corridas_)
        if (c->rol == rol && c->estado == "corriendo") return c.get();
    return nullptr;
}
const Despachador::Corrida* Despachador::enCursoDe(const std::string& rol) const {
    for (const auto& c : corridas_)
        if (c->rol == rol && c->estado == "corriendo") return c.get();
    return nullptr;
}

int Despachador::profundidadDe(const std::string& rol) const {
    const Corrida* c = enCursoDe(rol);
    return c == nullptr ? -1 : c->profundidad;
}

int Despachador::enCurso() const {
    int n = 0;
    for (const auto& c : corridas_)
        if (c->estado == "corriendo") ++n;
    return n;
}

json Despachador::resumenCorrida(const Corrida& c) {
    json j{{"id", c.id},
           {"rol", c.rol},
           {"origen", c.origen},
           {"estado", c.estado},
           {"profundidad", c.profundidad},
           {"creada", hora(c.creadaMs)},
           {"inicio", c.inicioMs > 0 ? hora(c.inicioMs) : ""},
           {"fin", c.finMs > 0 ? hora(c.finMs) : ""},
           {"duracionS", c.inicioMs > 0 ? ((c.finMs > 0 ? c.finMs : ahoraMs()) - c.inicioMs) / 1000 : 0},
           {"herramientas", c.herramientas},
           {"pasos", c.pasos.size()},
           {"encargo", recortar(c.encargo, 300)},
           {"resumen", c.resumen},
           {"error", c.error}};
    return j;
}

json Despachador::corridas(int limit, const std::string& rol) const {
    json out = json::array();
    for (const auto& c : corridas_) {
        if (!rol.empty() && c->rol != rol) continue;
        out.push_back(resumenCorrida(*c));
        if (static_cast<int>(out.size()) >= limit) break;
    }
    return out;
}

json Despachador::corrida(const std::string& id) const {
    const Corrida* c = porId(id);
    if (c == nullptr) return nullptr;
    json j = resumenCorrida(*c);
    j["encargo"] = c->encargo;
    j["pasos"] = c->pasos;
    j["resultado"] = c->resultado;
    return j;
}

void Despachador::setContexto(const std::string& rol, std::function<std::string()> fn) {
    contexto_[rol] = std::move(fn);
}

// --- Interfaz -------------------------------------------------------------------

QVariantList Despachador::rolesQml() const {
    json r = roles_.aJson();
    for (auto& x : r) {
        const std::string id = x.value("id", "");
        x["corriendo"] = enCursoDe(id) != nullptr;
        const auto q = cola_.find(id);
        x["enCola"] = q == cola_.end() ? 0 : static_cast<int>(q->second.size());
        x["rolTitulo"] = x["titulo"];
    }
    return aLista(r);
}

QVariantList Despachador::corridasQml() const {
    json r = corridas(kCorridasGuardadas);
    for (auto& x : r) {
        const Rol* rol = roles_.porId(x.value("rol", ""));
        x["rolTitulo"] = rol == nullptr ? x["rol"] : json(rol->titulo);
    }
    return aLista(r);
}

QVariantList Despachador::proveedores() const {
    return motores_.proveedores ? motores_.proveedores() : QVariantList();
}

void Despachador::setActivo(const QString& rol, bool activo) {
    if (roles_.setActivo(rol.toStdString(), activo)) {
        registrar(diag::Nivel::Info, rol.toStdString(), activo ? "rol encendido" : "rol apagado");
        emit cambio();
    }
}
void Despachador::setConsola(const QString& rol, bool consola) {
    if (roles_.setConsola(rol.toStdString(), consola)) emit cambio();
}

void Despachador::setProveedor(const QString& rol, const QString& proveedor) {
    if (roles_.setProveedor(rol.toStdString(), proveedor.toStdString())) emit cambio();
}
void Despachador::setModelo(const QString& rol, const QString& modelo) {
    if (roles_.setModelo(rol.toStdString(), modelo.toStdString())) emit cambio();
}
QStringList Despachador::modelosDe(const QString& proveedor) const {
    return motores_.modelosDe ? motores_.modelosDe(proveedor) : QStringList();
}

QString Despachador::encargarDesdeUi(const QString& rol, const QString& texto) {
    std::string err;
    encargar(rol.toStdString(), texto.trimmed().toStdString(), "operador", 0, &err);
    return QString::fromStdString(err);
}

void Despachador::detener(const QString& id) {
    Corrida* c = porId(id.toStdString());
    if (c == nullptr) return;
    if (c->estado == "esperando") {
        terminar(*c, "detenida", "cancelada antes de arrancar");
        return;
    }
    if (c->proc == nullptr) return;
    c->estado = "detenida";
    c->proc->kill();
}

QVariantMap Despachador::corridaQml(const QString& id) const {
    const json j = corrida(id.toStdString());
    return j.is_null() ? QVariantMap() : aMapa(j);
}

} // namespace ba::app::agent
