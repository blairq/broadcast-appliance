// ============================================================================
// src/app/ui/HistoricoViewModel.cpp — ver HistoricoViewModel.hpp
// ============================================================================
#include "ui/HistoricoViewModel.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QStorageInfo>
#include <QThreadPool>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>

#include "evento/Bitacora.hpp"
#include "evento/CierreEvento.hpp"
#include "evento/FotosConfig.hpp"
#include "evento/Historico.hpp"
#include "platform/Paths.hpp"
#include "ui/EventoViewModel.hpp"

namespace ba::app {

namespace fs = std::filesystem;

namespace {

QVariant aQv(const nlohmann::json& j) {
    if (j.is_null()) return {};
    if (j.is_object() || j.is_array()) {
        const QJsonDocument d = QJsonDocument::fromJson(QByteArray::fromStdString(j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)));
        return d.isArray() ? QVariant(d.array().toVariantList()) : QVariant(d.object().toVariantMap());
    }
    return QJsonDocument::fromJson(QByteArray::fromStdString("[" + j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) + "]")).array().at(0).toVariant();
}

nlohmann::json deQv(const QVariant& v) {
    const QJsonDocument d = v.typeId() == QMetaType::QVariantList ? QJsonDocument(QJsonArray::fromVariantList(v.toList()))
                                                                   : QJsonDocument(QJsonObject::fromVariantMap(v.toMap()));
    return nlohmann::json::parse(d.toJson().toStdString(), nullptr, false);
}

QString hora(std::int64_t ms, const char* formato = "dd/MM/yyyy HH:mm:ss") {
    return ms == 0 ? QString() : QDateTime::fromMSecsSinceEpoch(ms).toString(formato);
}

} // namespace

HistoricoViewModel::HistoricoViewModel(EventoViewModel& ev, QObject* parent) : QObject(parent), ev_(ev) {
    connect(&ev_, &EventoViewModel::listaChanged, this, &HistoricoViewModel::cambio);
    connect(&ev_, &EventoViewModel::enCursoChanged, this, &HistoricoViewModel::cambio);
}

HistoricoViewModel::~HistoricoViewModel() {
    if (cancelarBusqueda_) cancelarBusqueda_->store(true);
}

namespace {
fs::path deU8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string aU8(const fs::path& p) {
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}
fs::path raizEventos() { return ba::platform::dataRoot() / "Configuraciones"; }
} // namespace

HistoricoViewModel::Lugar HistoricoViewModel::lugar(const std::string& carpeta) const {
    if (carpeta.rfind('@', 0) == 0) {
        const auto barra = carpeta.find('/', 1);
        const std::string ev = carpeta.substr(1, barra == std::string::npos ? std::string::npos : barra - 1);
        const fs::path base = raizEventos() / deU8(ev);
        return {base / "Eventos", base / "Config", barra == std::string::npos ? std::string() : carpeta.substr(barra + 1), ev,
                false};
    }
    return {ev_.raizEventos(), ev_.carpetaConfig(), carpeta, ba::platform::configActiva(ba::platform::dataRoot()), true};
}

evento::DatosDelEvento HistoricoViewModel::datosDe(const std::string& carpeta) const {
    const Lugar l = lugar(carpeta);
    if (l.abierto) return ev_.datosEvento(carpeta);
    return evento::EventoStore(l.raiz).datos(l.rel);
}

fs::path HistoricoViewModel::carpetaEvento(const std::string& carpeta) const {
    if (carpeta.empty()) return {};
    const Lugar l = lugar(carpeta);
    if (l.abierto) return ev_.carpetaEvento(carpeta);
    if (l.rel.empty() || !evento::EventoStore::rutaValida(l.rel)) return {};
    std::error_code ec;
    const fs::path d = l.raiz / deU8(l.rel);
    return fs::is_directory(d, ec) ? d : fs::path();
}

QStringList HistoricoViewModel::carpetas() const { return ev_.carpetas(); }

QString HistoricoViewModel::enCurso() const { return ev_.enCurso().value("carpeta").toString(); }

QVariantList HistoricoViewModel::eventos(const QString& grupo, const QString& texto) const {
    // `grupo`: "" = todos los eventos de todas las carpetas; "@<evento>" = los de esa
    // carpeta; "@<evento>/<sub>" o "<sub>" (la abierta) = los de esa subcarpeta.
    QVariantList out;
    const QString t = texto.trimmed().toLower();
    const std::string abierto = ba::platform::configActiva(ba::platform::dataRoot());
    const auto agregar = [&](QVariantMap m, const QString& clave, const std::string& ev, bool esAbierto) {
        const QString g = m["grupo"].toString();
        const QString gEv = "@" + QString::fromStdString(ev);
        if (!grupo.isEmpty()) {
            if (grupo.startsWith('@')) {
                if (grupo != gEv && !grupo.startsWith(gEv + "/")) return;
                const QString sub = grupo.mid(gEv.size() + 1);
                if (!sub.isEmpty() && g != sub && !g.startsWith(sub + "/")) return;
            } else if (!esAbierto || (g != grupo && !g.startsWith(grupo + "/")))
                return;
        }
        if (!t.isEmpty() && !m["nombre"].toString().toLower().contains(t) && !clave.toLower().contains(t) &&
            !QString::fromStdString(ev).toLower().contains(t))
            return;
        m["carpeta"] = clave;
        m["evento"] = QString::fromStdString(ev);
        m["abierto"] = esAbierto;
        const fs::path dir = carpetaEvento(clave.toStdString());
        if (!dir.empty()) m["resumen"] = aQv(evento::Historico::resumen(dir));
        const auto d = datosDe(clave.toStdString());
        m["estado"] = QString::fromStdString(d.estado);
        m["desde"] = hora(d.iniciadoMs, "dd/MM/yyyy HH:mm");
        m["hasta"] = hora(d.terminadoMs, "dd/MM/yyyy HH:mm");
        m["desdeMs"] = static_cast<qint64>(d.iniciadoMs);
        m["duracionMin"] = d.iniciadoMs != 0
                               ? static_cast<int>(((d.terminadoMs != 0 ? d.terminadoMs : QDateTime::currentMSecsSinceEpoch()) -
                                                   d.iniciadoMs) / 60000)
                               : 0;
        out << m;
    };
    for (const QVariant& v : ev_.eventos()) {
        const QVariantMap m = v.toMap();
        agregar(m, m["carpeta"].toString(), abierto, true);
    }
    // Los de los otros eventos (cada uno en su carpeta).
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(raizEventos(), ec)) {
        const std::string n = aU8(e.path().filename());
        if (!e.is_directory() || n.empty() || n[0] == '.' || n == abierto) continue;
        if (!grupo.isEmpty() && !grupo.startsWith('@')) continue; // una subcarpeta de la abierta
        evento::EventoStore st(e.path() / "Eventos");
        const std::string enCurso = st.enCurso();
        for (const evento::Ficha& f : st.listar())
            agregar(QVariantMap{{"grupo", QString::fromStdString(f.grupo)},
                                {"enCurso", f.carpeta == enCurso},
                                {"nombre", QString::fromStdString(f.nombre)},
                                {"fecha", QString::fromStdString(f.fecha)}},
                    QString::fromStdString("@" + n + "/" + f.carpeta), n, false);
    }
    // Lo más nuevo primero: por inicio, y los que nunca empezaron, por fecha.
    std::sort(out.begin(), out.end(), [](const QVariant& a, const QVariant& b) {
        const auto ma = a.toMap(), mb = b.toMap();
        const qint64 ia = ma["desdeMs"].toLongLong(), ib = mb["desdeMs"].toLongLong();
        if (ia != ib) return ia > ib;
        return ma["fecha"].toString() > mb["fecha"].toString();
    });
    return out;
}

QVariantMap HistoricoViewModel::resumen(const QString& carpeta) const {
    const fs::path dir = carpetaEvento(carpeta.toStdString());
    if (dir.empty()) return {};
    QVariantMap m = aQv(evento::Historico::resumen(dir)).toMap();
    const auto d = datosDe(carpeta.toStdString());
    const Lugar lu = lugar(carpeta.toStdString());
    m["evento"] = QString::fromStdString(lu.evento);
    m["abierto"] = lu.abierto;
    m["carpeta"] = carpeta;
    m["ruta"] = QString::fromStdString(dir.string());
    m["estado"] = QString::fromStdString(d.estado);
    m["desde"] = hora(d.iniciadoMs);
    m["hasta"] = hora(d.terminadoMs);
    m["lugar"] = QString::fromStdString(d.lugar);
    m["notas"] = QString::fromStdString(d.notas);
    m["automatico"] = d.automatico;
    QStringList et;
    for (const auto& e : d.etiquetas) et << QString::fromStdString(e);
    m["etiquetas"] = et;
    m["enCurso"] = carpeta == enCurso();
    // El acta, el informe y los capítulos que genera la página Evento al cerrar.
    QVariantList docs;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        const std::string n = e.path().filename().string();
        const bool esDoc = (n.rfind("acta-", 0) == 0 && e.path().extension() == ".md") || n.rfind("informe-", 0) == 0 ||
                           n == "capitulos.txt";
        if (e.is_regular_file() && esDoc)
            docs << QVariantMap{{"archivo", QString::fromStdString(n)}, {"ruta", QString::fromStdString(e.path().string())}};
    }
    std::sort(docs.begin(), docs.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap()["archivo"].toString() > b.toMap()["archivo"].toString();
    });
    m["documentos"] = docs;
    // Por tipo, cuántas líneas tiene la bitácora (para los filtros de la línea de tiempo).
    QVariantMap porTipo;
    for (const auto& l : evento::Bitacora::leer(dir / "bitacora.jsonl", 0, 0, {}, {}, 0)) {
        const QString t = QString::fromStdString(l.value("tipo", std::string()));
        porTipo[t] = porTipo.value(t).toInt() + 1;
    }
    m["porTipo"] = porTipo;
    return m;
}

QVariantList HistoricoViewModel::bitacora(const QString& carpeta, const QStringList& tipos, const QString& texto,
                                          int limite) const {
    const fs::path dir = carpetaEvento(carpeta.toStdString());
    if (dir.empty()) return {};
    std::set<std::string> t;
    for (const QString& x : tipos) t.insert(x.toStdString());
    QVariantList out;
    for (const auto& l : evento::Bitacora::leer(dir / "bitacora.jsonl", 0, 0, t, texto.toStdString(),
                                                static_cast<std::size_t>(std::max(1, limite)))) {
        QVariantMap m;
        m["hora"] = hora(l.value("t", std::int64_t{0}), "HH:mm:ss");
        m["dia"] = hora(l.value("t", std::int64_t{0}), "dd/MM");
        m["tipo"] = QString::fromStdString(l.value("tipo", std::string()));
        m["origen"] = QString::fromStdString(l.value("origen", std::string()));
        std::string texto2 = l.value("texto", std::string());
        nlohmann::json resto = l;
        for (const char* k : {"t", "tipo", "origen", "texto"}) resto.erase(k);
        if (texto2.empty()) texto2 = resto.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        else if (!resto.empty() && m["tipo"] != "config") texto2 += "  " + resto.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        if (texto2.size() > 600) texto2 = texto2.substr(0, 600) + "…";
        m["texto"] = QString::fromStdString(texto2);
        m["nivel"] = QString::fromStdString(l.value("nivel", std::string()));
        out << m;
    }
    std::reverse(out.begin(), out.end()); // lo último arriba
    return out;
}

QVariantMap HistoricoViewModel::grabaciones(const QString& carpeta) const {
    const fs::path dir = carpetaEvento(carpeta.toStdString());
    if (dir.empty()) return {};
    nlohmann::json g = evento::Historico::grabaciones(dir);
    for (auto& x : g["grabaciones"]) {
        x["desde"] = hora(x.value("desde_ms", std::int64_t{0})).toStdString();
        x["hasta"] = hora(x.value("hasta_ms", std::int64_t{0})).toStdString();
    }
    for (auto& x : g["transmisiones"]) {
        x["desde"] = hora(x.value("desde_ms", std::int64_t{0})).toStdString();
        x["hasta"] = hora(x.value("hasta_ms", std::int64_t{0})).toStdString();
    }
    return aQv(g).toMap();
}

QVariantList HistoricoViewModel::archivos(const QString& carpeta) const {
    const fs::path dir = carpetaEvento(carpeta.toStdString());
    if (dir.empty()) return {};
    return aQv(evento::Historico::archivos(dir)).toList();
}

nlohmann::json HistoricoViewModel::traerDelDiaJson(const std::string& carpeta, bool mover, std::int64_t desdeMs,
                                                  std::int64_t hastaMs) {
    const fs::path dir = carpetaEvento(carpeta);
    if (dir.empty()) return {{"error", "no hay un evento «" + carpeta + "»"}};
    if (!lugar(carpeta).abierto) return {{"error", "es de otro evento: abrilo para traerle lo del día"}};
    if (desdeMs == 0) {
        // El día del evento: el de su inicio, su fecha o el de la jornada automática.
        const auto d = ev_.datosEvento(carpeta);
        QDate dia;
        if (d.iniciadoMs != 0) dia = QDateTime::fromMSecsSinceEpoch(d.iniciadoMs).date();
        if (!dia.isValid()) {
            const QString f = ev_.eventos().isEmpty() ? QString() : [&] {
                for (const QVariant& v : ev_.eventos())
                    if (v.toMap()["carpeta"].toString().toStdString() == carpeta) return v.toMap()["fecha"].toString();
                return QString();
            }();
            dia = QDate::fromString(f, "yyyy-MM-dd");
        }
        if (!dia.isValid()) dia = QDate::fromString(QString::fromStdString(d.dia), "yyyy-MM-dd");
        if (!dia.isValid()) return {{"error", "el evento no tiene día (ni inicio, ni fecha): pasá desde y hasta"}};
        desdeMs = QDateTime(dia, QTime(0, 0)).toMSecsSinceEpoch();
        QDate ultimo = d.terminadoMs != 0 ? QDateTime::fromMSecsSinceEpoch(d.terminadoMs).date() : dia;
        if (ultimo < dia) ultimo = dia;
        hastaMs = QDateTime(ultimo.addDays(1), QTime(0, 0)).toMSecsSinceEpoch() - 1;
    }
    std::vector<fs::path> carpetas = carpetasGrabacion_ ? carpetasGrabacion_() : std::vector<fs::path>{};
    const fs::path transcripciones = ev_.carpetaConfig().parent_path() / "Recordings" / "Transcripciones";
    json r = evento::Historico::traerDelDia(dir, carpetas, transcripciones, desdeMs, hastaMs, mover);
    r["desde"] = hora(desdeMs).toStdString();
    r["hasta"] = hora(hastaMs).toStdString();
    emit cambio();
    return r;
}

QVariantMap HistoricoViewModel::traerDelDia(const QString& carpeta, bool mover) {
    return aQv(traerDelDiaJson(carpeta.toStdString(), mover)).toMap();
}

QVariantList HistoricoViewModel::config(const QString& carpeta, const QString& foto) const {
    const fs::path dir = carpetaEvento(carpeta.toStdString());
    if (dir.empty()) return {};
    const auto f = evento::FotosConfig::cargar(dir / "config" / ((foto.isEmpty() ? QString("ultima") : foto) + ".json").toStdString());
    QVariantList out;
    if (f.is_null()) return out;
    const nlohmann::json archivos = f.value("archivos", nlohmann::json::object()); // no .items() de un temporal
    for (const auto& [k, v] : archivos.items())
        out << QVariantMap{{"archivo", QString::fromStdString(k)}, {"bytes", static_cast<qint64>(v.value("bytes", 0))}};
    return out;
}

QString HistoricoViewModel::contenidoConfig(const QString& carpeta, const QString& foto, const QString& archivo) const {
    const fs::path dir = carpetaEvento(carpeta.toStdString());
    if (dir.empty()) return {};
    const auto f = evento::FotosConfig::cargar(dir / "config" / ((foto.isEmpty() ? QString("ultima") : foto) + ".json").toStdString());
    if (f.is_null()) return {};
    const auto a = f.value("archivos", nlohmann::json::object());
    if (!a.contains(archivo.toStdString())) return {};
    const Lugar lu = lugar(carpeta.toStdString());
    evento::FotosConfig fotos(lu.config, lu.raiz / ".objetos");
    return QString::fromStdString(fotos.leer(a[archivo.toStdString()].value("sha", std::string())).dump(2, ' ', false, nlohmann::json::error_handler_t::replace));
}

QVariantList HistoricoViewModel::comparar(const QString& carpeta, const QString& foto, const QString& otro,
                                          const QString& otraFoto) const {
    // El contenido de cada foto se lee por su sha del Eventos/.objetos de SU evento:
    // se comparan fotos del mismo evento («actual» es la configuración del abierto).
    const Lugar la = lugar(carpeta.toStdString());
    const Lugar lb = otro.isEmpty() ? lugar(std::string()) : lugar(otro.toStdString());
    if (la.raiz != lb.raiz) return {};
    evento::FotosConfig fotos(la.config, la.raiz / ".objetos");
    const auto cargar = [&](const QString& c, const QString& f) -> nlohmann::json {
        if (c.isEmpty() && f == "actual") return la.abierto ? fotos.foto(QDateTime::currentMSecsSinceEpoch()) : nullptr;
        const fs::path dir = carpetaEvento(c.toStdString());
        if (dir.empty()) return nullptr;
        return evento::FotosConfig::cargar(dir / "config" / ((f.isEmpty() ? QString("ultima") : f) + ".json").toStdString());
    };
    const auto a = cargar(carpeta, foto), b = cargar(otro, otraFoto);
    if (a.is_null() || b.is_null()) return {};
    return aQv(fotos.comparar(a, b)).toList();
}

QString HistoricoViewModel::prepararRestauracion(const QString& carpeta, const QString& foto, const QStringList& archivos) {
    const fs::path dir = carpetaEvento(carpeta.toStdString());
    if (dir.empty()) return "No está ese evento.";
    if (!lugar(carpeta.toStdString()).abierto)
        return "Es de otro evento: abrilo para volver a esa configuración, o creá un evento nuevo desde esta foto.";
    const auto f = evento::FotosConfig::cargar(dir / "config" / ((foto.isEmpty() ? QString("inicio") : foto) + ".json").toStdString());
    if (f.is_null()) return "Ese evento no tiene esa foto de la configuración.";
    std::vector<std::string> l;
    for (const QString& a : archivos) l.push_back(a.toStdString());
    evento::FotosConfig fotos(ev_.carpetaConfig(), ev_.raizEventos() / ".objetos");
    const auto r = fotos.prepararRestauracion(f, l, carpeta.toStdString() + " (" + (foto.isEmpty() ? "inicio" : foto.toStdString()) + ")");
    emit restauracionCambio();
    if (r.contains("error")) return QString::fromStdString(r.value("error", std::string()));
    return {};
}

void HistoricoViewModel::cancelarRestauracion() {
    evento::FotosConfig::cancelarRestauracion(ev_.carpetaConfig());
    emit restauracionCambio();
}

QVariantMap HistoricoViewModel::restauracion() const {
    return aQv(evento::FotosConfig::restauracionPendiente(ev_.carpetaConfig())).toMap();
}

void HistoricoViewModel::buscar(const QString& texto) {
    if (cancelarBusqueda_) cancelarBusqueda_->store(true);
    cancelarBusqueda_ = std::make_shared<std::atomic<bool>>(false);
    auto cancelar = cancelarBusqueda_;
    buscando_ = true;
    emit buscandoCambio();
    const fs::path raiz = ev_.raizEventos();
    QPointer<HistoricoViewModel> self(this);
    QThreadPool::globalInstance()->start([self, raiz, texto, cancelar]() {
        const auto r = evento::Historico::buscar(raiz, texto.toStdString(), 300, cancelar.get());
        if (cancelar->load()) return;
        QMetaObject::invokeMethod(qApp, [self, texto, r]() {
            if (!self) return;
            QVariantList l = aQv(r).toList();
            for (QVariant& v : l) {
                QVariantMap m = v.toMap();
                if (m.contains("t")) m["hora"] = hora(m["t"].toLongLong(), "dd/MM HH:mm:ss");
                v = m;
            }
            self->buscando_ = false;
            emit self->buscandoCambio();
            emit self->resultados(texto, l);
        }, Qt::QueuedConnection);
    });
}

void HistoricoViewModel::verificar(const QString& carpeta) {
    const fs::path dir = carpetaEvento(carpeta.toStdString());
    if (dir.empty()) return;
    const bool enCursoAhora = carpeta == enCurso();
    QPointer<HistoricoViewModel> self(this);
    QThreadPool::globalInstance()->start([self, dir, carpeta, enCursoAhora]() {
        auto r = evento::CierreEvento::verificar(dir);
        if (enCursoAhora) r["aviso"] = "Está en curso: se sigue escribiendo, los sellos valen cuando termine.";
        QMetaObject::invokeMethod(qApp, [self, carpeta, r]() {
            if (self) emit self->verificado(carpeta, aQv(r).toMap());
        }, Qt::QueuedConnection);
    });
}

QString HistoricoViewModel::carpetaDeExportar() const {
    return QString::fromStdString((ev_.raizEventos().parent_path() / "Exportados").string());
}

void HistoricoViewModel::exportar(const QString& carpeta, const QString& destino, bool conGrabaciones) {
    const fs::path dir = carpetaEvento(carpeta.toStdString());
    if (dir.empty()) {
        emit exportado(carpeta, "No está ese evento.", {});
        return;
    }
    const fs::path dst = (destino.isEmpty() ? carpetaDeExportar() : destino).toStdString();
    const fs::path objetos = lugar(carpeta.toStdString()).raiz / ".objetos";
    QPointer<HistoricoViewModel> self(this);
    QThreadPool::globalInstance()->start([self, dir, dst, objetos, carpeta, conGrabaciones]() {
        fs::path copia;
        const std::string e = evento::Historico::exportar(dir, objetos, dst, conGrabaciones, &copia);
        QMetaObject::invokeMethod(qApp, [self, carpeta, e, copia]() {
            if (self) emit self->exportado(carpeta, QString::fromStdString(e), QString::fromStdString(copia.string()));
        }, Qt::QueuedConnection);
    });
}

void HistoricoViewModel::abrirCarpeta(const QString& carpeta, const QString& sub) {
    fs::path dir = carpetaEvento(carpeta.toStdString());
    if (dir.empty()) dir = ev_.raizEventos();
    if (!sub.isEmpty()) dir /= sub.toStdString();
    QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(dir.string())));
}

void HistoricoViewModel::abrirArchivo(const QString& ruta) {
    QDesktopServices::openUrl(QUrl::fromLocalFile(ruta));
}

QVariantMap HistoricoViewModel::aQvPublico(const json& j) { return aQv(j).toMap(); }

nlohmann::json HistoricoViewModel::retencion(bool simular) {
    const int dias = ajustesJson()["retencion"].value("grabaciones_dias", 0);
    json eventos = json::array();
    std::uintmax_t total = 0;
    if (dias <= 0) return {{"grabaciones_dias", 0}, {"eventos", eventos}, {"bytes", 0}, {"nota", "la retención está apagada"}};
    const qint64 ahora = QDateTime::currentMSecsSinceEpoch();
    const qint64 limite = ahora - static_cast<qint64>(dias) * 24 * 3600 * 1000;
    const std::string enCursoAhora = enCurso().toStdString();
    for (const QVariant& v : ev_.eventos()) {
        const std::string c = v.toMap()["carpeta"].toString().toStdString();
        if (c.empty() || c == enCursoAhora) continue;
        const auto d = ev_.datosEvento(c);
        if (d.estado != "terminado" || d.terminadoMs == 0 || d.terminadoMs > limite) continue;
        const fs::path dir = carpetaEvento(c);
        if (dir.empty()) continue;
        const json r = evento::Historico::limpiarGrabaciones(dir, ahora, simular);
        if (r["archivos"].empty()) continue;
        total += r.value("bytes", std::uintmax_t{0});
        eventos.push_back(json{{"evento", c}, {"terminado", hora(d.terminadoMs).toStdString()}, {"archivos", r["archivos"]},
                               {"bytes", r["bytes"]}});
        if (!simular) {
            evento::Bitacora b;
            b.setArchivo(dir / "bitacora.jsonl");
            b.anotar("evento", "retencion", "grabaciones borradas (retención de " + std::to_string(dias) + " días)", r, ahora);
            b.vaciar();
        }
    }
    if (!simular && !eventos.empty()) emit cambio();
    return {{"grabaciones_dias", dias}, {"simular", simular}, {"eventos", eventos}, {"bytes", total}};
}

nlohmann::json HistoricoViewModel::disco() const {
    const QStorageInfo si(QString::fromStdString(ev_.raizEventos().string()));
    const double total = static_cast<double>(si.bytesTotal()), libre = static_cast<double>(si.bytesAvailable());
    const int aviso = ajustesJson()["retencion"].value("aviso_disco_pct", 90);
    const int usado = total > 0 ? static_cast<int>(std::lround(100.0 * (1.0 - libre / total))) : 0;
    return {{"libre_gb", std::round(libre / 1e8) / 10.0},
            {"total_gb", std::round(total / 1e8) / 10.0},
            {"usado_pct", usado},
            {"aviso_pct", aviso},
            {"lleno", aviso > 0 && usado >= aviso}};
}

nlohmann::json HistoricoViewModel::ajustesJson() const {
    const nlohmann::json aj = ev_.ajustesHistorico();
    nlohmann::json reg = nlohmann::json::object();
    const nlohmann::json r = aj.value("registrar", nlohmann::json::object());
    for (const std::string& c : evento::Bitacora::categorias()) reg[c] = !r.is_object() || r.value(c, true);
    nlohmann::json excl = aj.contains("bus_excluir") && aj["bus_excluir"].is_array()
                              ? aj["bus_excluir"]
                              : nlohmann::json::array({"audio/nivel/#", "audio/loudness", "oradores/tiempo", "salida/salud",
                                                       "aire/tally"});
    const json ret = aj.value("retencion", json::object());
    return {{"registrar", reg},
            {"bus_excluir", excl},
            {"evento_automatico", ev_.eventoAutomatico()},
            {"retencion",
             {{"grabaciones_dias", ret.is_object() ? ret.value("grabaciones_dias", 0) : 0},
              {"aviso_disco_pct", ret.is_object() ? ret.value("aviso_disco_pct", 90) : 90}}}};
}

QVariantMap HistoricoViewModel::ajustes() const { return aQv(ajustesJson()).toMap(); }

std::string HistoricoViewModel::guardarAjustesJson(const nlohmann::json& c) {
    if (c.contains("evento_automatico") && c["evento_automatico"].is_boolean())
        ev_.setEventoAutomatico(c["evento_automatico"].get<bool>());
    const fs::path ruta = ev_.carpetaConfig() / "historico.json";
    nlohmann::json j = ev_.ajustesHistorico();
    if (c.contains("registrar") && c["registrar"].is_object()) {
        nlohmann::json reg = j.value("registrar", nlohmann::json::object());
        if (!reg.is_object()) reg = nlohmann::json::object();
        for (const auto& [k, v] : c["registrar"].items()) {
            const auto& cats = evento::Bitacora::categorias();
            if (std::find(cats.begin(), cats.end(), k) == cats.end()) return "no hay una categoría «" + k + "»";
            if (!v.is_boolean()) return "«" + k + "»: true o false";
            reg[k] = v;
        }
        j["registrar"] = reg;
    }
    if (c.contains("retencion")) {
        if (!c["retencion"].is_object()) return "`retencion`: {grabaciones_dias, aviso_disco_pct}";
        json ret = j.value("retencion", json::object());
        if (!ret.is_object()) ret = json::object();
        for (const auto& [k, v] : c["retencion"].items()) {
            if (k != "grabaciones_dias" && k != "aviso_disco_pct") return "en `retencion` no hay «" + k + "»";
            if (!v.is_number_integer() || v.get<int>() < 0 || (k == "aviso_disco_pct" && v.get<int>() > 100))
                return "«" + k + "»: un entero (días, o % de 0 a 100)";
            ret[k] = v;
        }
        j["retencion"] = ret;
    }
    if (c.contains("bus_excluir")) {
        if (!c["bus_excluir"].is_array()) return "`bus_excluir`: una lista de patrones del bus";
        j["bus_excluir"] = c["bus_excluir"];
    }
    std::error_code ec;
    fs::create_directories(ruta.parent_path(), ec);
    {
        std::ofstream o(ruta.string() + ".tmp");
        o << j.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
    }
    fs::rename(ruta.string() + ".tmp", ruta, ec);
    if (aplicarAjustes_) aplicarAjustes_(ajustesJson());
    emit ajustesCambio();
    return {};
}

QString HistoricoViewModel::guardarAjustes(const QVariantMap& cambios) {
    return QString::fromStdString(guardarAjustesJson(deQv(cambios)));
}

} // namespace ba::app
