// ============================================================================
// src/app/arranque/ServicioEmision.cpp — ver ServicioEmision.hpp
// ============================================================================

#include "arranque/ServicioEmision.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <vector>

#include "arranque/AireDurable.hpp"
#include "arranque/ArchivoConfig.hpp"
#include "diag/Registro.hpp"
#include "emision/SalidasConfig.hpp"
#include "i18n/I18nTexto.hpp"
#include "persist/ArchivoConfig.hpp"
#include "platform/Volumen.hpp"
#include "security/CredentialStore.hpp"
#include "vivos/Vivos.hpp"

namespace ba::app {

namespace fs = std::filesystem;
using Cambio = AppRuntime::Cambio;

namespace {
// Ultima salida al aire (servidor + clave), cifrada en Credenciales/: evita
// tipear la clave cada vez y permite retomar la emision tras una caida.
constexpr const char* kStreamCredential = "stream-last";

/// La credencial de la transmisión principal de una salida: la de la por omisión es
/// la de siempre; las demás, aparte (las de destinos.json son «stream-<destino>»).
std::string credencialDeSalida(const std::string& salida) {
    return salida.empty() || salida == salidas::kPorOmision ? std::string(kStreamCredential) : "salida-" + salida + "-stream";
}
} // namespace

bool ServicioEmision::leerUltimaTransmision(std::string& server, std::string& key, const std::string& salida) {
    std::string raw;
    if (!ba::security::CredentialStore::bundle().read(credencialDeSalida(salida), raw)) {
        return false;
    }
    const nlohmann::json j = nlohmann::json::parse(raw, nullptr, false);
    if (!j.is_object()) {
        return false;
    }
    server = j.value("server", "");
    key = j.value("key", "");
    return !key.empty();
}

nlohmann::json ServicioEmision::leerDestinos() {
    const nlohmann::json j = config::leerObjeto(ba::persist::archivoConfig("destinos.json")).value("destinos", nlohmann::json::array());
    return j.is_array() ? j : nlohmann::json::array();
}

void ServicioEmision::escribirDestinos(const nlohmann::json& d) {
    config::guardar(ba::persist::archivoConfig("destinos.json"), {{"destinos", d}});
}

void ServicioEmision::ponerCarpetaDelEvento(fs::path carpeta) {
    carpetaDelEvento_ = std::move(carpeta);
    carpetaSabida_ = true;
    ajustesAire(); // la copia del plano, ya con la carpeta
    rt_.fotoSucia(FotoMotor::Sala); // la carpeta de grabación efectiva
}

ServicioEmision::AjustesAire ServicioEmision::ajustesAire() const {
    if (!rt_.esDueno()) {
        std::lock_guard<std::mutex> l(ajustesAireMu_);
        return ajustesAire_;
    }
    AjustesAire a;
    a.rec = rt_.cfg_.rec;
    a.encoder = rt_.cfg_.encoder;
    a.stream = rt_.cfg_.streamCfg;
    a.claveEnv = rt_.cfg_.streamKeyEnv;
    a.transicionMs = rt_.cfg_.transition.durationMs;
    a.carpetaEvento = carpetaDelEvento_;
    std::lock_guard<std::mutex> l(ajustesAireMu_);
    ajustesAire_ = a;
    return a;
}

std::string ServicioEmision::carpetaGrabacion() const {
    const AjustesAire a = ajustesAire();
    return a.carpetaEvento.empty() ? a.rec.directory : (a.carpetaEvento / "Grabaciones").string();
}

ServicioEmision::AlResultado ServicioEmision::alResultado(std::uint64_t orden, AlResultado listo) {
    return [this, orden, listo = std::move(listo)](const ResultadoOrden& r) {
        // Desde el hilo de salidas del motor. La falla ya está en el registro (UNA línea: la
        // de la orden en el motor, BroadcastEngine::avisarFallo). El documento de aire lo
        // pone la próxima vuelta del motor (en el dueño); la foto y los que escuchan, ya
        // (avisar se lo encarga). Quien pidió recibe lo que pasó en el dueño, después de
        // esa foto.
        if (pedidos_.resultado(r.salida, r.que == "grabar", orden, r.ok)) rt_.avisar(Cambio::Salidas);
        if (listo) rt_.despuesEnDueno([listo, r]() { listo(r); });
    };
}

bool ServicioEmision::startRecording(const std::string& salida, AlResultado listo) {
    AppRuntime::errorDelHilo().clear(); // el de una llamada anterior no es de esta (el resultado real llega en `listo`)
    const std::string id = rt_.engine_->idDeSalida(salida);
    if (id.empty()) {
        AppRuntime::errorDelHilo() = ba::i18n::llenar(ba::i18n::traducir("salidas", "no hay una salida «%1»"), {salida});
        return false;
    }
    const bool porOmision = id == salidas::kPorOmision;
    std::error_code ec;
    const AjustesAire a = ajustesAire();
    RecordConfig rec = a.rec;
    rec.directory = carpetaGrabacion();
    // La copia en el otro disco, también por evento: <copia>/Eventos/<evento>/Grabaciones.
    if (!rec.copiaDir.empty()) {
        const fs::path& ev = a.carpetaEvento;
        std::error_code e2;
        const fs::path rel = ev.empty() ? fs::path() : fs::relative(ev, rt_.root_ / "Eventos", e2);
        if (!ev.empty() && !e2 && !rel.empty() && rel.string().rfind("..", 0) != 0)
            rec.copiaDir = (fs::path(rec.copiaDir) / "Eventos" / rel / "Grabaciones").string();
        fs::create_directories(rec.copiaDir, e2);
    }
    fs::create_directories(rec.directory, ec);
    const EngineStatus antes = rt_.engine_->status();
    const PedidoSalida antesPedido = pedidos_.de(id);
    const std::uint64_t orden = pedidos_.pedir(id, true);
    if (!rt_.engine_->startRecording(rec, rt_.codificadorDe(id), id, alResultado(orden, std::move(listo)))) {
        pedidos_.devolver(id, true, orden, antesPedido);
        AppRuntime::errorDelHilo() = rt_.engine_->lastError();
        std::printf("ba-app: %s\n",
                    ba::i18n::llenar(ba::i18n::traducir("grabacion", "la grabación no arrancó: %1"), {AppRuntime::errorDelHilo()}).c_str());
        return false;
    }
    std::printf("ba-app: %s\n", ba::i18n::llenar(ba::i18n::traducir("grabacion", "grabando en %1"), {rec.directory}).c_str());
    // El integrado del loudness arranca con la sesión (si no había nada saliendo de esa salida).
    if (porOmision) {
        if (!antes.streaming && !antes.recording) rt_.engine_->loudness().reiniciar();
    } else if (!rt_.engine_->haySalidaEn(id)) {
        rt_.engine_->loudness(id).reiniciar();
    }
    rt_.aire_->writeOnAirState();
    rt_.avisar(Cambio::Salidas);
    return true;
}

bool ServicioEmision::startSourceRecording(const std::string& source) {
    std::error_code ec;
    // Las individuales van a su propia carpeta: en Recordings/ ya estan las del
    // programa, y mezclar veinte archivos de entradas con las del evento hace
    // que despues nadie encuentre nada.
    const std::string dir = carpetaGrabacion() + "/entradas";
    fs::create_directories(dir, ec);
    if (!rt_.engine_->startSourceRecording(source, dir, rt_.cfg_.encoder)) {
        AppRuntime::errorDelHilo() = rt_.engine_->lastError();
        return false;
    }
    ba::diag::registro().linea(ba::diag::Nivel::Info, "grabacion", 0,
                               ba::i18n::llenar(ba::i18n::traducir("grabacion", "grabando la entrada \"%1\" aparte"), {source}));
    rt_.avisar(Cambio::Salidas);
    return true;
}

bool ServicioEmision::stopSourceRecording(const std::string& source) {
    if (!rt_.engine_->stopSourceRecording(source)) {
        AppRuntime::errorDelHilo() = rt_.engine_->lastError();
        return false;
    }
    ba::diag::registro().linea(ba::diag::Nivel::Info, "grabacion", 0,
                               ba::i18n::llenar(ba::i18n::traducir("grabacion", "para la grabación aparte de \"%1\""), {source}));
    rt_.avisar(Cambio::Salidas);
    return true;
}

void ServicioEmision::stopAllSourceRecordings() {
    rt_.engine_->stopAllSourceRecordings();
    rt_.avisar(Cambio::Salidas);
}

void ServicioEmision::stopRecording(const std::string& salida) {
    const std::string id = rt_.engine_->idDeSalida(salida);
    if (id.empty()) return;
    pedidos_.dejarDePedir(id, true);
    rt_.aire_->writeOnAirState();
    rt_.engine_->stopRecording(id);
    rt_.avisar(Cambio::Salidas);
}

bool ServicioEmision::streamKeySaved() const {
    if (keySaved_ < 0) { // descifrar cuesta: se consulta una vez
        std::string server;
        std::string key;
        keySaved_ = leerUltimaTransmision(server, key) ? 1 : 0;
    }
    return keySaved_ == 1;
}

std::string ServicioEmision::grabarAparte(const std::string& fuente) {
    const nlohmann::json d = rt_.discoGrabacion(); // con el turno, como siempre
    const double horas = d.value("horas", -1.0);
    if (horas >= 0 && horas < AppRuntime::kDiscoMinimoAparteH)
        return ba::i18n::traducir("grabacion", "Queda disco para menos de media hora: liberá espacio antes de sumar grabaciones aparte.");
    if (static_cast<int>(rt_.engine_->sourceRecordings().size()) >= BroadcastEngine::kMaxIndividuales)
        return ba::i18n::llenar(ba::i18n::traducir("grabacion", "Ya se graban %1 aparte (el tope): pará alguna."),
                                {std::to_string(BroadcastEngine::kMaxIndividuales)});
    if (!startSourceRecording(fuente)) return AppRuntime::errorDelHilo();
    return {};
}

bool ServicioEmision::salirAlAireManual(const std::string& server, const std::string& key, bool cambiarEnCaliente,
                                        const std::string& salida, AlResultado listo) {
    // El vivo lo anotan el bus y el histórico (son del dueño) cuando de verdad arrancó (o
    // cambió de destino): no con la orden encolada, que puede fallar; y no si ya salía ahí.
    // (La respuesta llega en el dueño después de esto: `igual` ya está puesto.)
    const auto igual = std::make_shared<std::atomic<bool>>(false);
    AlResultado alTerminar = std::move(listo);
    if (alVivoManual_) {
        const std::string srv = server.empty() ? ajustesAire().stream.server : server;
        alTerminar = [this, srv, igual, listo = std::move(alTerminar)](const ResultadoOrden& r) {
            if (r.ok && !*igual && !srv.empty() && alVivoManual_) alVivoManual_(srv);
            if (listo) listo(r);
        };
    }
    if (!startStreaming(server, key, cambiarEnCaliente, salida, std::move(alTerminar))) return false;
    *igual = rt_.engine_->ultimoInicio() == BroadcastEngine::Inicio::YaIgual;
    return true;
}

bool ServicioEmision::startStreaming(const std::string& server, const std::string& key, bool cambiarEnCaliente,
                                     const std::string& salida, AlResultado listo) {
    AppRuntime::errorDelHilo().clear(); // el de una llamada anterior no es de esta (el resultado real llega en `listo`)
    const AjustesAire a = ajustesAire();
    const std::string id = rt_.engine_->idDeSalida(salida);
    if (id.empty()) {
        AppRuntime::errorDelHilo() = ba::i18n::llenar(ba::i18n::traducir("salidas", "no hay una salida «%1»"), {salida});
        return false;
    }
    // Otra salida: su transmisión principal va a lo que se pida (la clave de la
    // por omisión y la última guardada son de la por omisión: no se reusan, que
    // la misma clave dos veces pisa la transmisión). La clave se guarda cifrada
    // aparte, solo para retomarla tras un corte.
    if (id != salidas::kPorOmision) {
        if (server.empty() || key.empty()) {
            AppRuntime::errorDelHilo() = ba::i18n::traducir("salidas", "para otra salida, pasá el servidor y la clave (o usá un destino "
                                                       "guardado con esa salida)");
            return false;
        }
        StreamConfig sc = a.stream;
        sc.server = server;
        sc.key = key;
        const bool salia = rt_.engine_->haySalidaEn(id);
        const PedidoSalida antesPedido = pedidos_.de(id);
        const std::uint64_t orden = pedidos_.pedir(id, false);
        if (!rt_.engine_->startStreaming(sc, rt_.codificadorDe(id), cambiarEnCaliente, id, alResultado(orden, std::move(listo)))) {
            pedidos_.devolver(id, false, orden, antesPedido);
            AppRuntime::errorDelHilo() = rt_.engine_->lastError();
            return false;
        }
        if (!salia) rt_.engine_->loudness(id).reiniciar();
        // Para volver tras un corte: adónde (la clave, cifrada).
        ba::security::CredentialStore::bundle().write(
            credencialDeSalida(id), nlohmann::json{{"server", server}, {"key", key}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
        rt_.aire_->writeOnAirState();
        rt_.avisar(Cambio::Salidas);
        return true;
    }
    StreamConfig sc = a.stream;
    if (!server.empty()) {
        sc.server = server;
    }
    if (!key.empty()) {
        sc.key = key;
    } else {
        const char* envKey = std::getenv(a.claveEnv.c_str());
        sc.key = envKey != nullptr ? envKey : "";
        if (sc.key.empty()) {
            // Sin clave tipeada ni en el entorno: la de la ultima salida al aire.
            std::string lastServer;
            if (leerUltimaTransmision(lastServer, sc.key) && server.empty() && !lastServer.empty()) {
                sc.server = lastServer;
            }
        }
    }
    if (sc.server.empty()) {
        AppRuntime::errorDelHilo() = ba::i18n::traducir("salidas", "no hay servidor de transmisión configurado");
        return false;
    }
    const EngineStatus antes = rt_.engine_->status();
    const PedidoSalida antesPedido = pedidos_.de(id);
    const std::uint64_t orden = pedidos_.pedir(id, false);
    if (!rt_.engine_->startStreaming(sc, a.encoder, cambiarEnCaliente, {}, alResultado(orden, std::move(listo)))) {
        pedidos_.devolver(id, false, orden, antesPedido);
        AppRuntime::errorDelHilo() = rt_.engine_->lastError();
        std::printf("ba-app: %s\n",
                    ba::i18n::llenar(ba::i18n::traducir("salidas", "la transmisión no arrancó: %1"), {AppRuntime::errorDelHilo()}).c_str());
        return false;
    }
    std::printf("ba-app: %s\n", ba::i18n::llenar(ba::i18n::traducir("salidas", "transmitiendo a %1"), {sc.server}).c_str());
    if (!antes.streaming && !antes.recording) rt_.engine_->loudness().reiniciar();
    if (!sc.key.empty()) {
        ba::security::CredentialStore::bundle().write(
            kStreamCredential, nlohmann::json{{"server", sc.server}, {"key", sc.key}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
        keySaved_ = 1;
    }
    rt_.aire_->writeOnAirState();
    rt_.avisar(Cambio::Salidas);
    return true;
}

void ServicioEmision::stopStreaming(const std::string& salida) {
    const std::string id = rt_.engine_->idDeSalida(salida);
    if (id.empty()) return;
    pedidos_.dejarDePedir(id, false);
    rt_.aire_->writeOnAirState();
    rt_.engine_->stopStreaming(id);
    rt_.avisar(Cambio::Salidas);
}

nlohmann::json ServicioEmision::destinos() const { return destinosConEstado(destinosGuardados()); }

nlohmann::json ServicioEmision::destinosGuardados() const {
    nlohmann::json out = nlohmann::json::array();
    for (nlohmann::json d : leerDestinos()) {
        d["salida"] = salidas::salidaDeDestino(d);
        d["tiene_clave"] = ba::security::CredentialStore::bundle().exists("stream-" + d.value("id", std::string()));
        out.push_back(std::move(d));
    }
    return out;
}

nlohmann::json ServicioEmision::destinosConEstado(const nlohmann::json& guardados) const {
    const nlohmann::json est = rt_.engine_->extrasEstado();
    nlohmann::json out = nlohmann::json::array();
    for (nlohmann::json d : guardados) {
        const std::string id = d.value("id", std::string());
        const nlohmann::json e = est.value(id, nlohmann::json::object());
        d["activo"] = e.value("activo", false);
        d["reconectando"] = e.value("reconectando", false);
        d["perdidos"] = e.value("perdidos", 0);
        d["cuadros"] = e.value("cuadros", 0);
        d["bytes"] = e.value("bytes", 0);
        if (!d.value("url_respaldo", std::string()).empty())
            d["respaldo_conectado"] = e.value("ingest_respaldo", nlohmann::json::object()).value("conectada", false);
        out.push_back(d);
    }
    return out;
}

std::string ServicioEmision::guardarDestino(const nlohmann::json& d, std::string* idOut) {
    const std::string url = d.value("url", std::string());
    // El punto de ingest de respaldo (la misma clave): RTMP o SRT, como el principal; "" lo saca.
    const std::string urlRespaldo = d.value("url_respaldo", std::string());
    if (!urlRespaldo.empty() && urlRespaldo.rfind("rtmp://", 0) != 0 && urlRespaldo.rfind("rtmps://", 0) != 0 && urlRespaldo.rfind("srt://", 0) != 0)
        return ba::i18n::traducir("salidas", "`url_respaldo`: rtmp://, rtmps:// o srt://");
    nlohmann::json lista = leerDestinos();
    std::string id = d.value("id", std::string());
    // La salida de la que toma el programa (sin campo = la por omisión): tiene que existir.
    if (d.contains("salida") && !d.value("salida", std::string()).empty() && !rt_.engine_->existeSalida(d.value("salida", std::string())))
        return ba::i18n::llenar(ba::i18n::traducir("salidas", "no hay una salida «%1»"), {d.value("salida", std::string())});
    nlohmann::json* existente = nullptr;
    for (auto& x : lista)
        if (!id.empty() && x.value("id", std::string()) == id) existente = &x;
    if (existente == nullptr) {
        if (url.rfind("rtmp://", 0) != 0 && url.rfind("rtmps://", 0) != 0 && url.rfind("srt://", 0) != 0 &&
            url.rfind("rist://", 0) != 0)
            return ba::i18n::traducir("salidas", "`url`: rtmp://, rtmps://, srt:// o rist://");
        if (id.empty()) {
            int n = 1;
            const auto usado = [&](const std::string& c) {
                for (const auto& x : lista)
                    if (x.value("id", std::string()) == c) return true;
                return false;
            };
            while (usado("destino-" + std::to_string(n))) ++n;
            id = "destino-" + std::to_string(n);
        }
        lista.push_back({{"id", id}, {"nombre", d.value("nombre", id)}, {"url", url}, {"respaldo", d.value("respaldo", false)}});
        existente = &lista.back();
        if (d.contains("salida")) (*existente)["salida"] = d["salida"];
        // El vivo de una cuenta (vivos/Vivos.hpp): plataforma, cuenta, vivo y su título.
        for (const char* k : {"plataforma", "cuenta", "vivo", "vivo_titulo"})
            if (d.contains(k) && d[k].is_string()) (*existente)[k] = d[k];
        if (!urlRespaldo.empty()) (*existente)["url_respaldo"] = urlRespaldo;
    } else {
        for (const char* k : {"nombre", "url", "respaldo", "salida", "plataforma", "cuenta", "vivo", "vivo_titulo"})
            if (d.contains(k)) (*existente)[k] = d[k];
        if (d.contains("url_respaldo")) {
            if (urlRespaldo.empty()) existente->erase("url_respaldo");
            else (*existente)["url_respaldo"] = urlRespaldo;
        }
    }
    // SRT: el modo (caller/listener/rendezvous) y la latencia van en la URL, que es
    // como los lee el muxer (ffmpeg: latency en microsegundos). La passphrase es la clave.
    if (d.contains("modo") || d.contains("latencia_ms")) {
        const std::string actual = (*existente).value("url", std::string());
        if (actual.rfind("srt://", 0) != 0) return ba::i18n::traducir("salidas", "`modo` y `latencia_ms` son solo para srt://");
        const auto q = actual.find('?');
        std::string u = actual.substr(0, q);
        std::vector<std::pair<std::string, std::string>> params;
        if (q != std::string::npos) {
            std::stringstream ss(actual.substr(q + 1));
            for (std::string kv; std::getline(ss, kv, '&');)
                if (!kv.empty()) {
                    const auto e = kv.find('=');
                    params.emplace_back(kv.substr(0, e), e == std::string::npos ? "" : kv.substr(e + 1));
                }
        }
        const auto poner = [&params](const std::string& k, const std::string& v) {
            for (auto& [pk, pv] : params)
                if (pk == k) return void(pv = v);
            params.emplace_back(k, v);
        };
        if (d.contains("modo")) {
            const std::string m = d.value("modo", std::string());
            if (m != "caller" && m != "listener" && m != "rendezvous") return ba::i18n::traducir("salidas", "`modo`: caller, listener o rendezvous");
            poner("mode", m);
        }
        if (d.contains("latencia_ms")) {
            const int ms = d.value("latencia_ms", 0);
            if (ms < 20 || ms > 8000) return ba::i18n::traducir("salidas", "`latencia_ms`: entre 20 y 8000");
            poner("latency", std::to_string(ms * 1000));
        }
        for (std::size_t i = 0; i < params.size(); ++i)
            u += (i == 0 ? "?" : "&") + params[i].first + (params[i].second.empty() ? "" : "=" + params[i].second);
        (*existente)["url"] = u;
    }
    // Dos destinos al MISMO vivo (la misma cuenta y vivo, o la misma URL con la misma clave)
    // se pisan en la plataforma: no se guarda y se dice cuál lo usa (vivos/Vivos.hpp).
    {
        const std::string claveNueva = d.contains("clave") && d["clave"].is_string() ? d["clave"].get<std::string>() : std::string();
        const auto huellaDe = [](const std::string& destino) {
            std::string c;
            ba::security::CredentialStore::bundle().read("stream-" + destino, c);
            return vivos::huella(c);
        };
        const nlohmann::json lasSalidas = rt_.salidas().value("salidas", nlohmann::json::array());
        const auto nombreSalida = [&lasSalidas](const std::string& sal) {
            for (const auto& s : lasSalidas)
                if (s.value("id", std::string()) == sal) return s.value("nombre", sal);
            return sal;
        };
        nlohmann::json candidato = *existente;
        candidato["salida"] = salidas::salidaDeDestino(candidato);
        const std::string choque = vivos::chocaCon(lista, candidato, vivos::huella(claveNueva), huellaDe, nombreSalida);
        if (!choque.empty()) return choque;
    }
    if (d.contains("clave") && d["clave"].is_string() && !d["clave"].get<std::string>().empty())
        ba::security::CredentialStore::bundle().write("stream-" + id, d["clave"].get<std::string>());
    escribirDestinos(lista);
    if (idOut != nullptr) *idOut = id;
    rt_.avisar(Cambio::Salidas);
    return {};
}

std::string ServicioEmision::aplicarReplay() {
    const int s = rt_.cfg_.replaySegundos;
    if (!rt_.engine_->setReplay(s, rt_.cfg_.rec.directory + "/repeticiones", rt_.cfg_.encoder))
        return ba::i18n::llenar(ba::i18n::traducir("salidas", "Repetición: %1"), {rt_.engine_->lastError()});
    return s > 0 ? ba::i18n::llenar(ba::i18n::traducir("salidas", "Repetición: guarda los últimos %1 s."), {std::to_string(s)})
                 : ba::i18n::traducir("salidas", "Repetición apagada.");
}

std::string ServicioEmision::ponerReplay(int segundos, const std::string& salida) {
    const std::string id = rt_.engine_->idDeSalida(salida);
    if (id.empty()) return ba::i18n::llenar(ba::i18n::traducir("salidas", "no hay una salida «%1»"), {salida});
    segundos = std::clamp(segundos, 0, 600);
    if (id == salidas::kPorOmision) {
        rt_.cfg_.replaySegundos = segundos;
        rt_.save();
        const std::string nota = aplicarReplay();
        return segundos > 0 && !rt_.engine_->replayEstado().value("activa", false) ? nota : std::string();
    }
    if (!rt_.engine_->setReplay(segundos, rt_.cfg_.rec.directory + "/repeticiones", rt_.codificadorDe(id), id))
        return ba::i18n::llenar(ba::i18n::traducir("salidas", "Repetición: %1"), {rt_.engine_->lastError()});
    rt_.avisar(Cambio::Salidas);
    return {};
}

void ServicioEmision::detenerSenalLimpia(const std::string& salida) {
    rt_.engine_->stopCleanFeed(salida);
    rt_.avisar(Cambio::Salidas);
}

std::string ServicioEmision::iniciarSenalLimpia(const std::string& destino, const std::string& salida) {
    std::string url, clave, dir;
    if (!destino.empty()) {
        for (const auto& d : leerDestinos())
            if (d.value("id", std::string()) == destino) url = d.value("url", std::string());
        if (url.empty())
            return ba::i18n::llenar(ba::i18n::traducir("salidas", "no hay un destino «%1» (stream_destinos los lista)"), {destino});
        ba::security::CredentialStore::bundle().read("stream-" + destino, clave);
    } else {
        std::error_code ec;
        dir = carpetaGrabacion() + "/limpia";
        fs::create_directories(dir, ec);
    }
    if (!rt_.engine_->startCleanFeed(url, clave, dir, rt_.codificadorDe(salida), salida)) return rt_.engine_->lastError();
    ba::diag::registro().linea(ba::diag::Nivel::Info, "salidas", 0,
                               destino.empty() ? ba::i18n::llenar(ba::i18n::traducir("salidas", "señal limpia grabando en %1"), {dir})
                                               : ba::i18n::llenar(ba::i18n::traducir("salidas", "señal limpia a %1"), {destino}));
    rt_.avisar(Cambio::Salidas);
    return {};
}

std::string ServicioEmision::borrarDestino(const std::string& id) {
    nlohmann::json lista = leerDestinos(), nueva = nlohmann::json::array();
    for (const auto& x : lista)
        if (x.value("id", std::string()) != id) nueva.push_back(x);
    if (nueva.size() == lista.size()) return ba::i18n::llenar(ba::i18n::traducir("salidas", "no hay un destino «%1»"), {id});
    rt_.engine_->stopExtra(id);
    pedidos_.sacarDestino(id);
    rt_.aire_->writeOnAirState();
    ba::security::CredentialStore::bundle().remove("stream-" + id);
    escribirDestinos(nueva);
    rt_.avisar(Cambio::Salidas);
    return {};
}

std::string ServicioEmision::iniciarDestino(const std::string& id) {
    std::string errores;
    bool alguno = false;
    for (const auto& d : leerDestinos()) {
        const std::string did = d.value("id", std::string());
        if (id != "todos" && did != id) continue;
        alguno = true;
        std::string clave;
        ba::security::CredentialStore::bundle().read("stream-" + did, clave);
        if (rt_.engine_->startExtra(did, d.value("url", std::string()), clave, rt_.codificadorDe(salidas::salidaDeDestino(d)),
                               salidas::salidaDeDestino(d),
                               d.value("url_respaldo", std::string()))) {
            // Pedido en su salida (vuelve tras un corte), en ninguna otra.
            const std::string sal = rt_.engine_->idDeSalida(salidas::salidaDeDestino(d));
            if (!sal.empty()) pedidos_.ponerDestino(sal, did);
        } else
            errores += (errores.empty() ? "" : "; ") + did + ": " + rt_.engine_->lastError();
    }
    if (!alguno) return ba::i18n::llenar(ba::i18n::traducir("salidas", "no hay un destino «%1» (stream_destinos los lista)"), {id});
    rt_.aire_->writeOnAirState();
    // Aunque alguno haya fallado, otros pudieron arrancar: la lista cambia igual.
    rt_.avisar(Cambio::Salidas);
    return errores;
}

nlohmann::json ServicioEmision::discoGrabacion() const {
    const AjustesAire a = ajustesAire();
    const RecordConfig& rec = a.rec;
    std::error_code ec;
    fs::create_directories(rec.directory, ec);
    // Lo que ya escribe el motor (todas las salidas: grabación, copia, señal limpia, ISO)
    // y, si la de la por omisión no graba, lo que sumaría al arrancarla (con su copia):
    // el disco se reparte entre todo lo que va a él.
    const nlohmann::json estado = rt_.engine_->discoEstado();
    std::vector<ba::platform::EscrituraDisco> e;
    for (const nlohmann::json& v : estado.value("restante", nlohmann::json::object()).value("volumenes", nlohmann::json::array())) {
        std::string que;
        for (const nlohmann::json& q : v.value("que", nlohmann::json::array())) que += (que.empty() ? "" : ", ") + q.get<std::string>();
        e.push_back({que, v.value("carpeta", std::string()), v.value("kbps", 0.0)});
    }
    const int kbps = BroadcastEngine::kbpsGrabacion(rec, a.encoder);
    const bool graba = rt_.engine_->grabacionEstado().value("activa", false);
    if (!graba) {
        e.push_back({ba::i18n::traducir("emision", "grabación"), rec.directory, static_cast<double>(kbps)});
        if (!rec.copiaDir.empty())
            e.push_back({ba::i18n::traducir("emision", "copia de la grabación"), rec.copiaDir, static_cast<double>(kbps)});
    }
    nlohmann::json d = ba::platform::aJson(ba::platform::discoRestante(e));
    d["carpeta"] = rec.directory;
    d["kbps_grabacion"] = kbps;
    // La copia en el mismo disco que la grabación no es una copia: lo que graba ahora
    // (el motor lo mide) o, sin grabar, lo que está configurado.
    nlohmann::json mismo = estado.value("mismo_volumen", nlohmann::json::array());
    if (!graba && !rec.copiaDir.empty() && ba::platform::mismoVolumen(rec.directory, rec.copiaDir))
        mismo.push_back({{"salida", std::string(BroadcastEngine::kSalidaPorOmision)}, {"grabacion", rec.directory}, {"copia", rec.copiaDir}});
    d["copia_mismo_volumen"] = !mismo.empty();
    d["mismo_volumen"] = std::move(mismo);
    return d;
}

void ServicioEmision::detenerDestino(const std::string& id) {
    pedidos_.sacarDestino(id);
    rt_.aire_->writeOnAirState();
    if (id == "todos")
        rt_.engine_->stopExtras();
    else
        rt_.engine_->stopExtra(id);
    rt_.avisar(Cambio::Salidas);
}

} // namespace ba::app
