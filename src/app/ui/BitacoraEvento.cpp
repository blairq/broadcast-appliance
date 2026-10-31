// src/app/ui/BitacoraEvento.cpp — ver el .hpp (salió de StudioViewModel, F3-03)

#include "persist/Escritor.hpp"
#include "ui/BitacoraEvento.hpp"

#include "ajustes/Ajustes.hpp"
#include "arranque/AppRuntime.hpp"
#include "control/BusEventos.hpp"
#include "emision/VentanillaMotor.hpp"
#include "diag/Registro.hpp"
#include "evento/Bitacora.hpp"
#include "evento/CierreEvento.hpp"
#include "evento/FotosConfig.hpp"
#include "i18n/I18n.hpp"
#include "mcp/McpTraza.hpp"
#include "platform/Hilo.hpp"
#include "platform/Paths.hpp"
#include "ui/EventoViewModel.hpp"
#include "ui/HistoricoViewModel.hpp"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QTime>
#include <QTimer>

#include <fstream>
#include <map>
#include <mutex>

namespace ba::app {

BitacoraEvento::BitacoraEvento(AppRuntime& rt, VentanillaMotor& ventanilla, EventoViewModel& evento, control::BusEventos& bus,
                               QObject* parent)
    : QObject(parent), rt_(rt), ventanilla_(ventanilla), evento_(&evento), bus_(&bus) {
    armar();
}

std::string BitacoraEvento::archivoGrabacion() const {
    const FotoMotor::Salida* s = ventanilla_.foto().salida({});
    return s != nullptr ? s->archivo : std::string();
}

std::string BitacoraEvento::copiaGrabacion() const { return ventanilla_.foto().grabacion.value("copia", std::string()); }

BitacoraEvento::~BitacoraEvento() {
    // Un sello a medias no se escribe: se corta y se vuelve a sellar otro día.
    cancelarSellos_ = true;
    for (auto& s : selladores_)
        if (s.hilo.joinable()) s.hilo.join();
    if (bitacoraOyente_ != 0) ba::diag::registro().quitarOyente(bitacoraOyente_);
    if (bitacoraBus_ != 0) bus_->desuscribir(bitacoraBus_);
    if (bitacora_) {
        revisarConfig({}, "");
        bitacora_->anotar("evento", "app", ba::i18n::traducir("historico", "la app se cierra"));
        bitacora_->vaciar();
    }
}

void BitacoraEvento::revisarConfig(const std::string& carpeta, const std::string& momento) {
    if (!fotos_ || !evento_) return;
    const auto dir = evento_->carpetaEvento(carpeta);
    if (dir.empty()) return;
    const auto ahoraMs = QDateTime::currentMSecsSinceEpoch();
    nlohmann::json f;
    try {
        f = fotos_->foto(ahoraMs);
    } catch (const std::exception&) {
        return;
    }
    const auto cfg = dir / "config";
    nlohmann::json antes = fotoUltima_;
    if (antes.is_null()) antes = evento::FotosConfig::cargar(cfg / "ultima.json");
    if (!antes.is_null() && antes.value("archivos", nlohmann::json::object()) == f.value("archivos", nlohmann::json::object())) {
        fotoUltima_ = f;
        std::error_code ec;
        if (momento == "inicio" && !std::filesystem::exists(cfg / "inicio.json", ec))
            evento::FotosConfig::guardar(cfg / "inicio.json", f);
        if (momento == "cierre") evento::FotosConfig::guardar(cfg / "cierre.json", f);
        return;
    }
    if (!antes.is_null() && bitacora_) {
        // Si es de otro evento (el que se cierra), la línea va a SU bitácora:
        // la bitácora ya puede estar apuntando al nuevo. Se escribe directo.
        const bool otro = !carpeta.empty();
        for (const auto& c : fotos_->comparar(antes, f)) {
            const std::string texto =
                antes.value("t", 0LL) != 0 && momento == "inicio"
                    ? ba::i18n::llenar(ba::i18n::traducir("historico", "%1 %2 (con la app cerrada)"),
                                       {c.value("archivo", std::string()), c.value("que", std::string())})
                    : c.value("archivo", std::string()) + " " + c.value("que", std::string());
            if (otro) {
                evento::Bitacora b;
                b.setArchivo(dir / "bitacora.jsonl");
                b.anotar("config", c.value("archivo", std::string()), texto, c, ahoraMs);
                b.vaciar();
            } else {
                bitacora_->anotar("config", c.value("archivo", std::string()), texto, c, ahoraMs);
            }
        }
    }
    fotoUltima_ = f;
    evento::FotosConfig::guardar(cfg / "ultima.json", f);
    std::error_code ec;
    if (momento == "inicio" && !std::filesystem::exists(cfg / "inicio.json", ec))
        evento::FotosConfig::guardar(cfg / "inicio.json", f);
    if (momento == "cierre") evento::FotosConfig::guardar(cfg / "cierre.json", f);
}

namespace {
nlohmann::json leerLista(const std::filesystem::path& p) {
    std::ifstream f(p);
    nlohmann::json j = f ? nlohmann::json::parse(f, nullptr, false) : nlohmann::json();
    return j.is_array() ? j : nlohmann::json::array();
}
void guardarLista(const std::filesystem::path& p, const nlohmann::json& j) {
    // El escritor único: durable y que también pisa en Windows (un rename a mano no).
    ba::persist::escribir(p, j.dump(1, '\t', false, nlohmann::json::error_handler_t::replace));
}
} // namespace

void BitacoraEvento::indexarSalida(const std::string& tema, const nlohmann::json& valor) {
    if (!evento_) return;
    const auto dir = evento_->carpetaEvento();
    if (dir.empty()) return;
    const qint64 ahora = QDateTime::currentMSecsSinceEpoch();
    if (tema == "salida/grabacion") {
        const auto p = dir / "grabaciones.json";
        nlohmann::json l = leerLista(p);
        if (valor.value("activo", false)) {
            nlohmann::json g{{"archivo", archivoGrabacion()}, {"desde_ms", ahora}};
            if (const std::string c = copiaGrabacion(); !c.empty()) g["copia"] = c;
            l.push_back(g);
        } else {
            for (auto it = l.rbegin(); it != l.rend(); ++it) {
                if (it->contains("hasta_ms")) continue;
                (*it)["hasta_ms"] = ahora;
                // Si se pasó a MP4 al terminar, el archivo es otro.
                std::filesystem::path a = it->value("archivo", std::string());
                const std::string final = archivoGrabacion();
                if (!final.empty()) a = final;
                (*it)["archivo"] = a.string();
                std::error_code ec;
                const auto bytes = std::filesystem::file_size(a, ec);
                if (!ec) (*it)["bytes"] = bytes;
                break;
            }
        }
        guardarLista(p, l);
        return;
    }
    const auto p = dir / "transmisiones.json";
    nlohmann::json l = leerLista(p);
    auto abierta = l.end();
    for (auto it = l.begin(); it != l.end(); ++it)
        if (!it->contains("hasta_ms")) abierta = it;
    if (tema == "salida/vivo") {
        if (abierta == l.end()) {
            l.push_back({{"desde_ms", ahora}, {"caidas", 0}});
            abierta = l.end() - 1;
        }
        for (auto it = valor.begin(); it != valor.end(); ++it) (*abierta)[it.key()] = it.value();
    } else if (valor.value("activo", false)) {
        if (abierta == l.end()) {
            l.push_back({{"desde_ms", ahora}, {"caidas", 0}});
        } else if (valor.value("reconectando", false)) {
            (*abierta)["caidas"] = abierta->value("caidas", 0) + 1;
        }
    } else if (abierta != l.end()) {
        (*abierta)["hasta_ms"] = ahora;
        if (!valor.value("problema", std::string()).empty()) (*abierta)["problema"] = valor["problema"];
    }
    guardarLista(p, l);
}

void BitacoraEvento::cerrarEnHistorico(const std::string& carpeta) {
    if (!evento_) return;
    const auto dir = evento_->carpetaEvento(carpeta);
    if (dir.empty()) return;
    const evento::DatosDelEvento d = evento_->datosEvento(carpeta);
    // Hasta cuándo: si ya terminó (el cierre que quedó pendiente de un día que
    // cambió con la app cerrada), su fin; si no, ahora.
    const qint64 ahora = d.terminadoMs != 0 && d.estado == "terminado" ? d.terminadoMs : QDateTime::currentMSecsSinceEpoch();
    // Sin inicio conocido, el día del evento (lo que haya en los logs de hoy).
    std::int64_t desde = d.iniciadoMs;
    if (desde == 0) desde = QDateTime(QDate::currentDate(), QTime(0, 0)).toMSecsSinceEpoch();
    const int asamblea = evento::CierreEvento::copiarAsamblea(rt_.dataRoot() / "Config", dir);
    const nlohmann::json registro =
        evento::CierreEvento::recortarRegistros(ba::platform::dataRoot() / "Logs", desde, ahora, dir / "registro");
    {
        evento::Bitacora b;
        b.setArchivo(dir / "bitacora.jsonl");
        b.anotar("evento", "historico", "cerrado", {{"asamblea", asamblea}, {"registro", registro}}, ahora);
        b.vaciar();
    }
    // Ya terminados los hilos viejos, se limpian.
    for (auto it = selladores_.begin(); it != selladores_.end();) {
        if (it->listo->load()) {
            it->hilo.join();
            it = selladores_.erase(it);
        } else {
            ++it;
        }
    }
    auto listo = std::make_shared<std::atomic<bool>>(false);
    selladores_.push_back({std::thread([this, dir, ahora, listo]() {
        ba::platform::nombrarHilo("sellador");
        const nlohmann::json s = evento::CierreEvento::sellar(dir, ahora, &cancelarSellos_);
        listo->store(true);
        if (!s.is_null())
            ba::diag::registro().linea(ba::diag::Nivel::Info, "historico", 0,
                                       ba::i18n::llenar(ba::i18n::traducir("historico", "sellado %1 (%2 archivos)"),
                                                        {dir.filename().string(),
                                                         std::to_string(s.value("archivos", nlohmann::json::object()).size())}));
    }), listo});
}

// LA BITÁCORA DEL EVENTO EN CURSO (H2 de docs/planes/PLAN-EVENTOS-HISTORICO.md): se
// cuelga de lo que ya pasa por dos lugares —el registro general (MCP, botonera,
// guion, avisos) y el bus de eventos (aire, salidas, oradores, asamblea)— sin
// tocar cada módulo.
void BitacoraEvento::armar() {
    bitacora_ = std::make_unique<evento::Bitacora>();
    bitacora_->setLimpiador([](const nlohmann::json& j) { return mcp::Traza::limpio(j); });
    const nlohmann::json aj = evento_->ajustesHistorico();
    const nlohmann::json reg = aj.value("registrar", nlohmann::json::object());
    for (const std::string& c : evento::Bitacora::categorias())
        bitacora_->setRegistrar(c, !reg.is_object() || reg.value(c, true));
    // Lo que se repite muchas veces por segundo no es un hecho: es una medición.
    busExcluir_ = std::make_shared<std::vector<std::string>>(control::BusEventos::medicionesDeFabrica());
    if (aj.contains("bus_excluir") && aj["bus_excluir"].is_array()) {
        busExcluir_->clear();
        for (const auto& p : aj["bus_excluir"])
            if (p.is_string()) busExcluir_->push_back(p.get<std::string>());
    }
    auto excluir = busExcluir_;
    // La página Histórico y el MCP cambian qué se registra, en vivo.
    historico_ = std::make_unique<HistoricoViewModel>(*evento_);
    historico_->setCarpetasGrabacion([this]() { return std::vector<std::filesystem::path>{ventanilla_.foto().sala.carpetaGrabacion}; });
    historico_->setAplicarAjustes([this](const nlohmann::json& a) {
        const nlohmann::json reg = a.value("registrar", nlohmann::json::object());
        for (const auto& [k, v] : reg.items())
            bitacora_->setRegistrar(k, v.get<bool>());
        std::vector<std::string> l;
        for (const auto& p : a.value("bus_excluir", nlohmann::json::array()))
            if (p.is_string()) l.push_back(p.get<std::string>());
        *busExcluir_ = l; // solo lo toca el hilo de la interfaz, igual que el oyente del bus
    });
    bitacora_->setArchivo(evento_->archivoBitacora());
    bitacora_->anotar("evento", "app", ba::i18n::traducir("historico", "la app arranca"),
                      {{"evento", evento_->enCurso().value("carpeta").toString().toStdString()}});
    // La configuración: lo que cambió con la app cerrada se ve al arrancar
    // (contra la última foto del evento), y después, cada 10 s.
    fotos_ = std::make_unique<evento::FotosConfig>(rt_.dataRoot() / "Config", evento_->raizEventos() / ".objetos");
    revisarConfig({}, "inicio");
    // LO QUE QUEDÓ SIN CERRAR: si el día cambió con la app cerrada, la jornada de
    // ayer se terminó al arrancar (antes de que hubiera bitácora) y no recibió su
    // cierre. Todo evento terminado en la última semana, con bitácora y sin
    // sellos, lo recibe ahora (su foto de cierre es la última que tuvo).
    {
        const qint64 semana = QDateTime::currentMSecsSinceEpoch() - 7LL * 24 * 3600 * 1000;
        const std::string enCursoAhora = evento_->enCurso().value("carpeta").toString().toStdString();
        for (const QVariant& v : evento_->eventos()) {
            const std::string c = v.toMap().value("carpeta").toString().toStdString();
            if (c.empty() || c == enCursoAhora) continue;
            const auto d = evento_->datosEvento(c);
            const auto dir = evento_->carpetaEvento(c);
            std::error_code ec;
            if (d.estado != "terminado" || d.terminadoMs < semana || dir.empty() ||
                !std::filesystem::exists(dir / "bitacora.jsonl", ec) || std::filesystem::exists(dir / "sellos.json", ec))
                continue;
            if (!std::filesystem::exists(dir / "config" / "cierre.json", ec))
                std::filesystem::copy_file(dir / "config" / "ultima.json", dir / "config" / "cierre.json", ec);
            ba::diag::registro().linea(ba::diag::Nivel::Info, "historico", 0,
                                       ba::i18n::llenar(ba::i18n::traducir("historico", "cierre pendiente de «%1»"), {c}));
            cerrarEnHistorico(c);
        }
    }
    // CADA HORA (y al minuto de arrancar): el disco de Eventos/ y la retención.
    {
        auto* tDisco = new QTimer(this);
        const auto revisar = [this]() {
            if (!historico_) return;
            const nlohmann::json d = historico_->disco();
            if (d.value("lleno", false)) {
                // Con el contexto de siempre (el texto ya está traducido ahí).
                const QString t = QCoreApplication::translate("StudioViewModel", "El disco de los eventos está al %1 % (quedan %2 GB)")
                                      .arg(d.value("usado_pct", 0))
                                      .arg(d.value("libre_gb", 0.0));
                ba::diag::registro().linea(ba::diag::Nivel::Aviso, "historico", 0, t.toStdString());
                emit notify(t, "warn");
            }
            const nlohmann::json r = historico_->retencion(false);
            if (!r.value("eventos", nlohmann::json::array()).empty())
                ba::diag::registro().linea(ba::diag::Nivel::Info, "historico", 0,
                                           ba::i18n::llenar(ba::i18n::traducir("historico",
                                                                               "retención: grabaciones borradas de %1 eventos (%2 MB)"),
                                                            {std::to_string(r["eventos"].size()),
                                                             std::to_string(r.value("bytes", 0ULL) / 1000000)}));
        };
        QTimer::singleShot(ba::ajustes::sAMs("historico.primeraRevisionS", 60), this, revisar);
        connect(tDisco, &QTimer::timeout, this, revisar);
        tDisco->start(std::max(60000, ba::ajustes::entero("historico.revisarCadaMin", 60) * 60 * 1000));
    }
    auto* tFotos = new QTimer(this);
    tFotos->setInterval(10000);
    connect(tFotos, &QTimer::timeout, this, [this]() { revisarConfig({}, ""); });
    tFotos->start();

    auto* b = bitacora_.get();
    // Del registro: cualquier hilo. Lo mismo repetido en 10 s se anota una vez
    // (un aviso que se dispara por cuadro no llena el disco).
    struct Repetidos {
        std::mutex mu;
        std::map<std::string, std::int64_t> visto;
    };
    auto rep = std::make_shared<Repetidos>();
    bitacoraOyente_ = ba::diag::registro().agregarOyente([b, rep](const ba::diag::Registrado& r) {
        std::string tipo;
        if (r.componente == "mcp")
            tipo = "mcp";
        else if (r.componente == "botonera")
            tipo = "botonera";
        else if (r.componente == "guion")
            tipo = "guion";
        else if (r.nivel >= ba::diag::Nivel::Aviso)
            tipo = "aviso";
        else
            return;
        if (r.nivel < ba::diag::Nivel::Info) return;
        // Qt avisa que se perdió un refresco cada vez que la pantalla se
        // bloquea o se duerme: no es un hecho del evento.
        // El aviso agrupado de refrescos perdidos (arranque/Registrar.cpp, código 1 de «qt»).
        if (r.componente == "qt" && r.codigo == 1) return;
        {
            std::lock_guard<std::mutex> g(rep->mu);
            const std::string clave = r.componente + "|" + r.texto;
            auto it = rep->visto.find(clave);
            if (it != rep->visto.end() && r.ms - it->second < 10000) return;
            rep->visto[clave] = r.ms;
            if (rep->visto.size() > 2000) rep->visto.clear();
        }
        nlohmann::json d = nlohmann::json::object();
        if (r.nivel >= ba::diag::Nivel::Aviso) d["nivel"] = ba::diag::nombreDeNivel(r.nivel);
        if (r.codigo != 0) d["codigo"] = r.codigo;
        b->anotar(tipo, r.componente, r.texto, d, r.ms);
    });
    // Las salidas se publican cada segundo (cambia el tiempo y los kbps): de
    // esas solo importa cuando cambian estos campos.
    auto importa = std::make_shared<std::map<std::string, nlohmann::json>>();
    bitacoraBus_ = bus_->suscribir("#", [this, b, excluir, importa](const std::string& tema, const nlohmann::json& valor) {
        for (const std::string& p : *excluir)
            if (control::BusEventos::coincide(p, tema)) return;
        if (tema == "salida/stream" || tema == "salida/grabacion") {
            nlohmann::json clave = nlohmann::json::object();
            for (const char* k : {"activo", "reconectando", "problema"})
                if (valor.is_object() && valor.contains(k)) clave[k] = valor[k];
            auto& antes = (*importa)[tema];
            if (antes == clave) return;
            antes = clave;
            nlohmann::json v = valor;
            if (tema == "salida/grabacion" && v.value("activo", false)) v["archivo"] = archivoGrabacion();
            b->anotar("bus", tema, {}, v);
            indexarSalida(tema, valor);
            return;
        }
        if (tema == "salida/vivo") indexarSalida(tema, valor);
        b->anotar("bus", tema, {}, valor);
    });
    // Cambia el evento en curso: lo que sigue va a su carpeta.
    auto anterior = std::make_shared<std::string>(evento_->enCurso().value("carpeta").toString().toStdString());
    connect(evento_, &EventoViewModel::enCursoChanged, this, [this, anterior]() {
        const std::string ahora = evento_->enCurso().value("carpeta").toString().toStdString();
        if (ahora == *anterior) return;
        if (!anterior->empty()) {
            revisarConfig(*anterior, "cierre");
            bitacora_->anotar("evento", "historico", ba::i18n::traducir("historico", "deja de estar en curso"),
                              {{"siguiente", ahora}});
            bitacora_->vaciar();
            cerrarEnHistorico(*anterior);
        }
        bitacora_->setArchivo(evento_->archivoBitacora());
        if (!ahora.empty()) {
            bitacora_->anotar("evento", "historico", ba::i18n::traducir("historico", "en curso"), {{"anterior", *anterior}});
            fotoUltima_ = nullptr; // el evento nuevo compara contra su propia última foto
            revisarConfig({}, "inicio");
        }
        *anterior = ahora;
    });
}

} // namespace ba::app
