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
#include <QThreadPool>
#include <QUrl>

#include <algorithm>
#include <fstream>
#include <set>

#include "evento/Bitacora.hpp"
#include "evento/CierreEvento.hpp"
#include "evento/FotosConfig.hpp"
#include "evento/Historico.hpp"
#include "ui/EventoViewModel.hpp"

namespace ba::app {

namespace fs = std::filesystem;

namespace {

QVariant aQv(const nlohmann::json& j) {
    if (j.is_null()) return {};
    if (j.is_object() || j.is_array()) {
        const QJsonDocument d = QJsonDocument::fromJson(QByteArray::fromStdString(j.dump()));
        return d.isArray() ? QVariant(d.array().toVariantList()) : QVariant(d.object().toVariantMap());
    }
    return QJsonDocument::fromJson(QByteArray::fromStdString("[" + j.dump() + "]")).array().at(0).toVariant();
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

fs::path HistoricoViewModel::carpetaEvento(const std::string& carpeta) const {
    return carpeta.empty() ? fs::path() : ev_.carpetaEvento(carpeta);
}

QStringList HistoricoViewModel::carpetas() const { return ev_.carpetas(); }

QString HistoricoViewModel::enCurso() const { return ev_.enCurso().value("carpeta").toString(); }

QVariantList HistoricoViewModel::eventos(const QString& grupo, const QString& texto) const {
    QVariantList out;
    const QString t = texto.trimmed().toLower();
    for (const QVariant& v : ev_.eventos()) {
        QVariantMap m = v.toMap();
        const QString g = m["grupo"].toString(), c = m["carpeta"].toString();
        if (!grupo.isEmpty() && g != grupo && !g.startsWith(grupo + "/")) continue;
        if (!t.isEmpty() && !m["nombre"].toString().toLower().contains(t) && !c.toLower().contains(t)) continue;
        const fs::path dir = ev_.carpetaEvento(c.toStdString());
        if (!dir.empty()) {
            const auto r = evento::Historico::resumen(dir);
            m["resumen"] = aQv(r);
        }
        const auto d = ev_.datosEvento(c.toStdString());
        m["estado"] = QString::fromStdString(d.estado);
        m["desde"] = hora(d.iniciadoMs, "dd/MM/yyyy HH:mm");
        m["hasta"] = hora(d.terminadoMs, "dd/MM/yyyy HH:mm");
        m["desdeMs"] = static_cast<qint64>(d.iniciadoMs);
        m["duracionMin"] = d.iniciadoMs != 0
                               ? static_cast<int>(((d.terminadoMs != 0 ? d.terminadoMs : QDateTime::currentMSecsSinceEpoch()) -
                                                   d.iniciadoMs) / 60000)
                               : 0;
        out << m;
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
    const auto d = ev_.datosEvento(carpeta.toStdString());
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
        if (texto2.empty()) texto2 = resto.dump();
        else if (!resto.empty() && m["tipo"] != "config") texto2 += "  " + resto.dump();
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
    evento::FotosConfig fotos(ev_.carpetaConfig(), ev_.raizEventos() / ".objetos");
    return QString::fromStdString(fotos.leer(a[archivo.toStdString()].value("sha", std::string())).dump(2));
}

QVariantList HistoricoViewModel::comparar(const QString& carpeta, const QString& foto, const QString& otro,
                                          const QString& otraFoto) const {
    evento::FotosConfig fotos(ev_.carpetaConfig(), ev_.raizEventos() / ".objetos");
    const auto cargar = [&](const QString& c, const QString& f) -> nlohmann::json {
        if (c.isEmpty() && f == "actual") return fotos.foto(QDateTime::currentMSecsSinceEpoch());
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
    const fs::path objetos = ev_.raizEventos() / ".objetos";
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

nlohmann::json HistoricoViewModel::ajustesJson() const {
    const nlohmann::json aj = ev_.ajustesHistorico();
    nlohmann::json reg = nlohmann::json::object();
    const nlohmann::json r = aj.value("registrar", nlohmann::json::object());
    for (const std::string& c : evento::Bitacora::categorias()) reg[c] = !r.is_object() || r.value(c, true);
    nlohmann::json excl = aj.contains("bus_excluir") && aj["bus_excluir"].is_array()
                              ? aj["bus_excluir"]
                              : nlohmann::json::array({"audio/nivel/#", "audio/loudness", "oradores/tiempo", "salida/salud",
                                                       "aire/tally"});
    return {{"registrar", reg}, {"bus_excluir", excl}, {"evento_automatico", ev_.eventoAutomatico()}};
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
    if (c.contains("bus_excluir")) {
        if (!c["bus_excluir"].is_array()) return "`bus_excluir`: una lista de patrones del bus";
        j["bus_excluir"] = c["bus_excluir"];
    }
    std::error_code ec;
    fs::create_directories(ruta.parent_path(), ec);
    {
        std::ofstream o(ruta.string() + ".tmp");
        o << j.dump(2);
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
