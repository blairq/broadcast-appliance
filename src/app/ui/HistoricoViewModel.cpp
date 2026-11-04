// ============================================================================
// src/app/ui/HistoricoViewModel.cpp — ver HistoricoViewModel.hpp
// ============================================================================
#include "ui/HistoricoViewModel.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QStorageInfo>
#include <QThreadPool>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <set>

#include "evento/Bitacora.hpp"
#include "evento/EventoStore.hpp"
#include "evento/FotosConfig.hpp"
#include "evento/Historico.hpp"
#include "i18n/I18nTexto.hpp"
#include "ui/EventoViewModel.hpp"
#include "ui/EventosListaViewModel.hpp"

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

/// La lista de la página: los eventos de `grupo` (y sus subcarpetas) que tienen `texto` en
/// el nombre, la carpeta o la configuración, con su resumen. En el hilo de trabajo.
nlohmann::json armarLista(const HistoricoConsulta& c, const std::vector<evento::Ficha>& abiertos, const std::string& enCursoAbierto,
                          const QString& grupo, const QString& texto) {
    nlohmann::json out = nlohmann::json::array();
    const QString t = texto.trimmed().toLower();
    const auto agregar = [&](const evento::Ficha& f, bool enCurso, const std::string& clave, const std::string& ev, bool esAbierto) {
        const QString g = QString::fromStdString(f.grupo);
        const QString gEv = "@" + QString::fromStdString(ev);
        if (!grupo.isEmpty()) {
            if (grupo.startsWith('@')) {
                if (grupo != gEv && !grupo.startsWith(gEv + "/")) return;
                const QString sub = grupo.mid(gEv.size() + 1);
                if (!sub.isEmpty() && g != sub && !g.startsWith(sub + "/")) return;
            } else if (!esAbierto || (g != grupo && !g.startsWith(grupo + "/")))
                return;
        }
        if (!t.isEmpty() && !QString::fromStdString(f.nombre).toLower().contains(t) &&
            !QString::fromStdString(clave).toLower().contains(t) && !QString::fromStdString(ev).toLower().contains(t))
            return;
        const evento::DatosDelEvento& d = f.datos;
        nlohmann::json m{{"grupo", f.grupo},       {"enCurso", enCurso},    {"nombre", f.nombre},
                         {"fecha", f.fecha},       {"carpeta", clave},      {"evento", ev},
                         {"abierto", esAbierto},   {"estado", d.estado},    {"desde", hora(d.iniciadoMs, "dd/MM/yyyy HH:mm").toStdString()},
                         {"hasta", hora(d.terminadoMs, "dd/MM/yyyy HH:mm").toStdString()},
                         {"desdeMs", d.iniciadoMs}};
        m["duracionMin"] = d.iniciadoMs != 0 ? static_cast<int>(((d.terminadoMs != 0 ? d.terminadoMs : QDateTime::currentMSecsSinceEpoch()) -
                                                                  d.iniciadoMs) / 60000)
                                             : 0;
        if (const fs::path dir = c.carpeta(clave); !dir.empty()) m["resumen"] = evento::Historico::resumen(dir);
        out.push_back(std::move(m));
    };
    for (const evento::Ficha& f : abiertos) agregar(f, f.carpeta == enCursoAbierto, f.carpeta, c.activa(), true);
    // Los de las otras configuraciones (cada una en su carpeta).
    if (grupo.isEmpty() || grupo.startsWith('@'))
        for (const auto& [nombre, raiz] : c.otras()) {
            if (!grupo.isEmpty() && grupo != "@" + QString::fromStdString(nombre) && !grupo.startsWith("@" + QString::fromStdString(nombre) + "/"))
                continue;
            const evento::EventoStore st(raiz);
            const std::string enCurso = st.enCurso();
            for (const evento::Ficha& f : st.listar()) agregar(f, f.carpeta == enCurso, "@" + nombre + "/" + f.carpeta, nombre, false);
        }
    // Lo más nuevo primero: por inicio, y los que nunca empezaron, por fecha.
    std::sort(out.begin(), out.end(), [](const nlohmann::json& a, const nlohmann::json& b) {
        const auto ia = a.value("desdeMs", std::int64_t{0}), ib = b.value("desdeMs", std::int64_t{0});
        if (ia != ib) return ia > ib;
        return a.value("fecha", std::string()) > b.value("fecha", std::string());
    });
    return out;
}

/// Las líneas de la bitácora como las muestra la línea de tiempo (lo último arriba).
nlohmann::json lineasDeTiempo(const nlohmann::json& crudas) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& l : crudas) {
        nlohmann::json m;
        m["hora"] = hora(l.value("t", std::int64_t{0}), "HH:mm:ss").toStdString();
        m["dia"] = hora(l.value("t", std::int64_t{0}), "dd/MM").toStdString();
        m["tipo"] = l.value("tipo", std::string());
        m["origen"] = l.value("origen", std::string());
        std::string texto = l.value("texto", std::string());
        nlohmann::json resto = l;
        for (const char* k : {"t", "tipo", "origen", "texto", "hora"}) resto.erase(k);
        if (texto.empty()) texto = resto.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        else if (!resto.empty() && m["tipo"] != "config") texto += "  " + resto.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        if (texto.size() > 600) texto = texto.substr(0, 600) + "…";
        m["texto"] = texto;
        m["nivel"] = l.value("nivel", std::string());
        out.push_back(std::move(m));
    }
    std::reverse(out.begin(), out.end());
    return out;
}

} // namespace

HistoricoViewModel::HistoricoViewModel(EventoViewModel& ev, QObject* parent) : QObject(parent), ev_(ev) {
    fondo_.setMaxThreadCount(1);
    juntarLista_.setSingleShot(true);
    juntarLista_.setInterval(150);
    connect(&juntarLista_, &QTimer::timeout, this, &HistoricoViewModel::leerLista);
    juntarDetalle_.setSingleShot(true);
    juntarDetalle_.setInterval(150);
    connect(&juntarDetalle_, &QTimer::timeout, this, &HistoricoViewModel::leerDetalle);
    // La lista de eventos cambió (creado, movido, en curso, traído lo del día): de nuevo.
    for (auto senal : {&EventosListaViewModel::listaChanged, &EventosListaViewModel::enCursoChanged})
        connect(&ev_.listaVm(), senal, this, [this]() {
            emit cambio();
            juntarLista_.start();
            juntarDetalle_.start();
        });
    connect(&ev_.listaVm(), &EventosListaViewModel::automaticoChanged, this, &HistoricoViewModel::ajustesCambio);
    // La bitácora del elegido creció (se mira su línea de tiempo o su resumen): de nuevo,
    // juntando lo que llega seguido.
    connect(&vigia_, &QFileSystemWatcher::fileChanged, this, [this]() {
        if (!juntarDetalle_.isActive()) juntarDetalle_.start(1000);
    });
    restauracion_ = aQv(evento::FotosConfig::restauracionPendiente(ev_.carpetaConfig())).toMap();
    leerLista();
    medirDisco();
}

HistoricoViewModel::~HistoricoViewModel() {
    if (cancelarBusqueda_) cancelarBusqueda_->store(true);
    fondo_.waitForDone();
}

HistoricoConsulta HistoricoViewModel::consulta() const {
    return HistoricoConsulta::deLaApp(ev_.raizEventos(), ev_.carpetaConfig());
}

fs::path HistoricoViewModel::carpetaEvento(const std::string& carpeta) const {
    if (carpeta.empty()) return {};
    return consulta().carpeta(carpeta);
}

QStringList HistoricoViewModel::carpetas() const { return ev_.listaVm().carpetas(); }

QString HistoricoViewModel::enCurso() const { return QString::fromStdString(ev_.listaVm().carpetaEnCurso()); }

// --- Lo que elige la vista -----------------------------------------------------------------

void HistoricoViewModel::setGrupo(const QString& g) {
    if (g == grupo_) return;
    grupo_ = g;
    emit eleccionCambio();
    leerLista();
}

void HistoricoViewModel::setFiltro(const QString& f) {
    if (f == filtro_) return;
    filtro_ = f;
    emit eleccionCambio();
    juntarLista_.start(); // se tipea: se junta
}

void HistoricoViewModel::setSeleccion(const QString& s) {
    if (s == sel_) return;
    sel_ = s;
    archivoConfig_.clear();
    cambios_ = QVariant();
    emit eleccionCambio();
    leerDetalle();
}

void HistoricoViewModel::setPestana(const QString& p) {
    if (p == pestana_) return;
    pestana_ = p;
    emit eleccionCambio();
    leerDetalle();
}

void HistoricoViewModel::setTiposTiempo(const QStringList& t) {
    if (t == tipos_) return;
    tipos_ = t;
    emit eleccionCambio();
    leerDetalle();
}

void HistoricoViewModel::setTextoTiempo(const QString& t) {
    if (t == textoTiempo_) return;
    textoTiempo_ = t;
    emit eleccionCambio();
    juntarDetalle_.start(150);
}

void HistoricoViewModel::setFotoConfig(const QString& f) {
    if (f == foto_) return;
    foto_ = f;
    archivoConfig_.clear();
    cambios_ = QVariant();
    emit eleccionCambio();
    leerDetalle();
}

void HistoricoViewModel::setArchivoConfig(const QString& a) {
    if (a == archivoConfig_) return;
    archivoConfig_ = a;
    cambios_ = QVariant();
    emit eleccionCambio();
    leerDetalle();
}

// --- Leer en el fondo ------------------------------------------------------------------------

void HistoricoViewModel::leerLista() {
    juntarLista_.stop();
    const int gen = ++genLista_;
    QPointer<HistoricoViewModel> yo(this);
    fondo_.start([yo, gen, c = consulta(), fichas = ev_.listaVm().fichas(), enCurso = ev_.listaVm().carpetaEnCurso(), grupo = grupo_,
                  texto = filtro_]() {
        const nlohmann::json l = armarLista(c, fichas, enCurso, grupo, texto);
        QMetaObject::invokeMethod(
            yo,
            [yo, gen, l]() {
                if (!yo || gen != yo->genLista_) return; // llegó otra más nueva
                yo->eventos_ = aQv(l).toList();
                emit yo->eventosCambio();
            },
            Qt::QueuedConnection);
    });
}

void HistoricoViewModel::leerDetalle() {
    juntarDetalle_.stop();
    vigilarBitacora();
    const int gen = ++genDetalle_;
    if (sel_.isEmpty()) {
        resumen_.clear();
        lineas_.clear();
        grabaciones_.clear();
        archivosConfig_.clear();
        contenidoConfig_.clear();
        archivos_.clear();
        emit detalleCambio();
        return;
    }
    std::set<std::string> tipos;
    for (const QString& x : tipos_) tipos.insert(x.toStdString());
    QPointer<HistoricoViewModel> yo(this);
    fondo_.start([yo, gen, c = consulta(), sel = sel_.toStdString(), pestana = pestana_, tipos, texto = textoTiempo_.toStdString(),
                  foto = foto_.toStdString(), archivo = archivoConfig_.toStdString()]() {
        nlohmann::json d{{"resumen", c.resumen(sel, pestana == "resumen")}};
        if (d["resumen"].contains("error")) d["resumen"] = nlohmann::json::object();
        if (pestana == "tiempo") {
            const nlohmann::json b = c.bitacora(sel, 0, 0, tipos, texto, 1500);
            d["lineas"] = lineasDeTiempo(b.value("lineas", nlohmann::json::array()));
        } else if (pestana == "grabaciones") {
            nlohmann::json g = c.grabaciones(sel);
            if (!g.contains("error")) d["grabaciones"] = std::move(g);
        } else if (pestana == "config") {
            const nlohmann::json f = c.config(sel, foto);
            d["archivosConfig"] = f.value("archivos", nlohmann::json::array());
            if (!archivo.empty()) d["contenido"] = c.contenidoConfig(sel, foto, archivo).value("contenido", nlohmann::json());
        } else if (pestana == "archivos") {
            const nlohmann::json a = c.archivos(sel);
            if (a.is_array()) d["archivos"] = a;
        }
        QMetaObject::invokeMethod(
            yo,
            [yo, gen, d]() {
                if (!yo || gen != yo->genDetalle_) return;
                yo->resumen_ = aQv(d["resumen"]).toMap();
                if (d.contains("resumen") && yo->resumen_.contains("desde_ms")) {
                    // Las fechas como las ve la página.
                    yo->resumen_["enCurso"] = yo->resumen_.value("carpeta").toString() == yo->enCurso();
                }
                yo->lineas_ = d.contains("lineas") ? aQv(d["lineas"]).toList() : QVariantList{};
                yo->grabaciones_ = d.contains("grabaciones") ? aQv(d["grabaciones"]).toMap() : QVariantMap{};
                yo->archivosConfig_ = d.contains("archivosConfig") ? aQv(d["archivosConfig"]).toList() : QVariantList{};
                yo->contenidoConfig_ =
                    d.contains("contenido") && !d["contenido"].is_null()
                        ? QString::fromStdString(d["contenido"].dump(2, ' ', false, nlohmann::json::error_handler_t::replace))
                        : QString();
                yo->archivos_ = d.contains("archivos") ? aQv(d["archivos"]).toList() : QVariantList{};
                emit yo->detalleCambio();
            },
            Qt::QueuedConnection);
    });
}

void HistoricoViewModel::vigilarBitacora() {
    // Solo la del elegido, y solo mientras se ve lo que sale de ella.
    QString quiero;
    if (!sel_.isEmpty() && (pestana_ == "tiempo" || pestana_ == "resumen")) {
        const HistoricoConsulta::Lugar l = consulta().lugar(sel_.toStdString());
        if (!l.raiz.empty() && !l.rel.empty())
            quiero = QString::fromStdString((l.raiz / fs::path(std::u8string(l.rel.begin(), l.rel.end())) / "bitacora.jsonl").string());
    }
    const QStringList hay = vigia_.files();
    if (hay.size() == 1 && hay.first() == quiero) return;
    if (!hay.isEmpty()) vigia_.removePaths(hay);
    if (!quiero.isEmpty()) vigia_.addPath(quiero); // si todavía no existe, no se vigila (no hay qué ver)
}

void HistoricoViewModel::comparar() {
    if (sel_.isEmpty()) return;
    const int gen = genDetalle_;
    QPointer<HistoricoViewModel> yo(this);
    fondo_.start([yo, gen, c = consulta(), sel = sel_.toStdString(), foto = foto_.toStdString()]() {
        const nlohmann::json r = c.comparar(sel, foto, {}, "actual");
        QMetaObject::invokeMethod(
            yo,
            [yo, gen, r]() {
                if (!yo || gen != yo->genDetalle_) return;
                yo->cambios_ = r.contains("cambios") ? aQv(r["cambios"]) : QVariant(QVariantList{});
                emit yo->detalleCambio();
            },
            Qt::QueuedConnection);
    });
}

// --- Acciones ---------------------------------------------------------------------------------

std::string HistoricoViewModel::delDia(const std::string& carpeta, std::int64_t& desdeMs, std::int64_t& hastaMs,
                                      fs::path& dir) const {
    const HistoricoConsulta c = consulta();
    dir = carpeta.empty() ? fs::path() : c.carpeta(carpeta);
    if (dir.empty()) return tr("no hay un evento «%1»").arg(QString::fromStdString(carpeta)).toStdString();
    if (!c.lugar(carpeta).abierto) return tr("es de otro evento: abrilo para traerle lo del día").toStdString();
    if (desdeMs != 0) return {};
    // El día del evento: el de su inicio, su fecha o el de la jornada automática.
    const auto d = ev_.listaVm().datos(carpeta);
    QDate dia;
    if (d.iniciadoMs != 0) dia = QDateTime::fromMSecsSinceEpoch(d.iniciadoMs).date();
    if (!dia.isValid())
        for (const evento::Ficha& f : ev_.listaVm().fichas())
            if (f.carpeta == carpeta) dia = QDate::fromString(QString::fromStdString(f.fecha), "yyyy-MM-dd");
    if (!dia.isValid()) dia = QDate::fromString(QString::fromStdString(d.dia), "yyyy-MM-dd");
    if (!dia.isValid()) return tr("el evento no tiene día (ni inicio, ni fecha): pasá desde y hasta").toStdString();
    desdeMs = QDateTime(dia, QTime(0, 0)).toMSecsSinceEpoch();
    QDate ultimo = d.terminadoMs != 0 ? QDateTime::fromMSecsSinceEpoch(d.terminadoMs).date() : dia;
    if (ultimo < dia) ultimo = dia;
    hastaMs = QDateTime(ultimo.addDays(1), QTime(0, 0)).toMSecsSinceEpoch() - 1;
    return {};
}

nlohmann::json HistoricoViewModel::traerDelDiaJson(const std::string& carpeta, bool mover, std::int64_t desdeMs,
                                                  std::int64_t hastaMs) {
    fs::path dir;
    if (const std::string e = delDia(carpeta, desdeMs, hastaMs, dir); !e.empty()) return {{"error", e}};
    const std::vector<fs::path> carpetas = carpetasGrabacion_ ? carpetasGrabacion_() : std::vector<fs::path>{};
    const fs::path transcripciones = ev_.carpetaConfig().parent_path() / "Recordings" / "Transcripciones";
    json r = evento::Historico::traerDelDia(dir, carpetas, transcripciones, desdeMs, hastaMs, mover);
    r["desde"] = hora(desdeMs).toStdString();
    r["hasta"] = hora(hastaMs).toStdString();
    emit cambio();
    leerLista();
    leerDetalle();
    return r;
}

void HistoricoViewModel::traerDelDia(const QString& carpeta, bool mover) {
    // Mover grabaciones puede ser de gigas: el día y las carpetas se arman acá (de la
    // caché), lo pesado va al fondo.
    std::int64_t desdeMs = 0, hastaMs = 0;
    fs::path dir;
    if (const std::string e = delDia(carpeta.toStdString(), desdeMs, hastaMs, dir); !e.empty()) {
        emit traido(carpeta, QVariantMap{{"error", QString::fromStdString(e)}});
        return;
    }
    QPointer<HistoricoViewModel> yo(this);
    fondo_.start([yo, carpeta, mover, dir, desdeMs, hastaMs,
                  carpetas = carpetasGrabacion_ ? carpetasGrabacion_() : std::vector<fs::path>{},
                  transcripciones = ev_.carpetaConfig().parent_path() / "Recordings" / "Transcripciones"]() {
        json r = evento::Historico::traerDelDia(dir, carpetas, transcripciones, desdeMs, hastaMs, mover);
        r["desde"] = hora(desdeMs).toStdString();
        r["hasta"] = hora(hastaMs).toStdString();
        QMetaObject::invokeMethod(
            yo,
            [yo, carpeta, r]() {
                if (!yo) return;
                emit yo->cambio();
                yo->leerLista();
                yo->leerDetalle();
                emit yo->traido(carpeta, aQv(r).toMap());
            },
            Qt::QueuedConnection);
    });
}

QString HistoricoViewModel::prepararRestauracion(const QString& carpeta, const QString& foto, const QStringList& archivos) {
    const HistoricoConsulta c = consulta();
    const fs::path dir = c.carpeta(carpeta.toStdString());
    if (carpeta.isEmpty() || dir.empty()) return tr("No está ese evento.");
    if (!c.lugar(carpeta.toStdString()).abierto)
        return tr("Es de otro evento: abrilo para volver a esa configuración, o creá un evento nuevo desde esta foto.");
    const auto f = evento::FotosConfig::cargar(dir / "config" / ((foto.isEmpty() ? QString("inicio") : foto) + ".json").toStdString());
    if (f.is_null()) return tr("Ese evento no tiene esa foto de la configuración.");
    std::vector<std::string> l;
    for (const QString& a : archivos) l.push_back(a.toStdString());
    evento::FotosConfig fotos(ev_.carpetaConfig(), ev_.raizEventos() / ".objetos");
    const auto r = fotos.prepararRestauracion(f, l, carpeta.toStdString() + " (" + (foto.isEmpty() ? "inicio" : foto.toStdString()) + ")");
    restauracion_ = aQv(evento::FotosConfig::restauracionPendiente(ev_.carpetaConfig())).toMap();
    emit restauracionCambio();
    if (r.contains("error")) return QString::fromStdString(r.value("error", std::string()));
    return {};
}

void HistoricoViewModel::cancelarRestauracion() {
    evento::FotosConfig::cancelarRestauracion(ev_.carpetaConfig());
    restauracion_.clear();
    emit restauracionCambio();
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
    QPointer<HistoricoViewModel> self(this);
    QThreadPool::globalInstance()->start([self, c = consulta(), carpeta]() {
        const nlohmann::json r = c.sellos(carpeta.toStdString());
        QMetaObject::invokeMethod(qApp, [self, carpeta, r]() {
            if (self) emit self->verificado(carpeta, aQv(r).toMap());
        }, Qt::QueuedConnection);
    });
}

QString HistoricoViewModel::carpetaDeExportar() const { return QString::fromStdString(consulta().carpetaDeExportar().string()); }

void HistoricoViewModel::exportar(const QString& carpeta, const QString& destino, bool conGrabaciones) {
    QPointer<HistoricoViewModel> self(this);
    QThreadPool::globalInstance()->start([self, c = consulta(), carpeta, dst = fs::path(destino.toStdString()), conGrabaciones]() {
        fs::path copia;
        const std::string e = c.exportar(carpeta.toStdString(), dst, conGrabaciones, &copia);
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

void HistoricoViewModel::abrirArchivo(const QString& ruta) { QDesktopServices::openUrl(QUrl::fromLocalFile(ruta)); }

QVariantMap HistoricoViewModel::aQvPublico(const json& j) { return aQv(j).toMap(); }

namespace {
using json = nlohmann::json;
/// La retención sobre lo que se le pasa (copias: corre en el fondo). Ver retencion().
json aplicarRetencion(const HistoricoConsulta& c, const std::vector<evento::Ficha>& fichas, const std::string& enCurso,
                      int dias, bool simular) {
    json eventos = json::array();
    std::uintmax_t total = 0;
    const qint64 ahora = QDateTime::currentMSecsSinceEpoch();
    const qint64 limite = ahora - static_cast<qint64>(dias) * 24 * 3600 * 1000;
    for (const evento::Ficha& f : fichas) {
        const auto& d = f.datos;
        if (f.carpeta.empty() || f.carpeta == enCurso || d.estado != "terminado" || d.terminadoMs == 0 || d.terminadoMs > limite)
            continue;
        const fs::path dir = c.carpeta(f.carpeta);
        if (dir.empty()) continue;
        const json r = evento::Historico::limpiarGrabaciones(dir, ahora, simular);
        if (r["archivos"].empty()) continue;
        total += r.value("bytes", std::uintmax_t{0});
        eventos.push_back(json{{"evento", f.carpeta}, {"terminado", hora(d.terminadoMs).toStdString()}, {"archivos", r["archivos"]},
                               {"bytes", r["bytes"]}});
        if (!simular) {
            evento::Bitacora b;
            b.setArchivo(dir / "bitacora.jsonl");
            b.anotar("evento", "retencion",
                     ba::i18n::llenar(ba::i18n::traducir("historico", "grabaciones borradas (retención de %1 días)"),
                                      {std::to_string(dias)}),
                     r, ahora);
            b.vaciar();
        }
    }
    return {{"grabaciones_dias", dias}, {"simular", simular}, {"eventos", eventos}, {"bytes", total}};
}
} // namespace

void HistoricoViewModel::retencion(bool simular, std::function<void(const json&)> listo) {
    const int dias = ajustesJson()["retencion"].value("grabaciones_dias", 0);
    if (dias <= 0) {
        if (listo)
            listo({{"grabaciones_dias", 0}, {"eventos", json::array()}, {"bytes", 0}, {"nota", tr("la retención está apagada").toStdString()}});
        return;
    }
    // Lo de la caché, copiado: recorrer y borrar grabaciones (gigas) va al fondo.
    QPointer<HistoricoViewModel> yo(this);
    fondo_.start([yo, dias, simular, listo = std::move(listo), c = consulta(), fichas = ev_.listaVm().fichas(),
                  enCurso = enCurso().toStdString()]() {
        const json r = aplicarRetencion(c, fichas, enCurso, dias, simular);
        QMetaObject::invokeMethod(
            yo,
            [yo, r, listo]() {
                if (!yo) return;
                if (!r.value("simular", true) && !r["eventos"].empty()) {
                    emit yo->cambio();
                    yo->leerLista();
                    yo->leerDetalle();
                }
                if (listo) listo(r);
            },
            Qt::QueuedConnection);
    });
}

void HistoricoViewModel::simularRetencion() {
    QPointer<HistoricoViewModel> yo(this);
    retencion(true, [yo](const json& r) {
        if (yo) emit yo->retencionSimulada(aQv(r).toMap());
    });
}

namespace {
nlohmann::json medir(const fs::path& raiz, int aviso) {
    const QStorageInfo si(QString::fromStdString(raiz.string()));
    const double total = static_cast<double>(si.bytesTotal()), libre = static_cast<double>(si.bytesAvailable());
    const int usado = total > 0 ? static_cast<int>(std::lround(100.0 * (1.0 - libre / total))) : 0;
    return {{"libre_gb", std::round(libre / 1e8) / 10.0},
            {"total_gb", std::round(total / 1e8) / 10.0},
            {"usado_pct", usado},
            {"aviso_pct", aviso},
            {"lleno", aviso > 0 && usado >= aviso}};
}
} // namespace

void HistoricoViewModel::medirDiscoY(std::function<void(const json&)> listo) {
    QPointer<HistoricoViewModel> yo(this);
    fondo_.start([yo, listo = std::move(listo), raiz = ev_.raizEventos(), aviso = ajustesJson()["retencion"].value("aviso_disco_pct", 90)]() {
        const nlohmann::json d = medir(raiz, aviso);
        QMetaObject::invokeMethod(
            yo,
            [yo, d, listo]() {
                if (!yo) return;
                yo->disco_ = aQv(d).toMap();
                emit yo->discoCambio();
                if (listo) listo(d);
            },
            Qt::QueuedConnection);
    });
}

nlohmann::json HistoricoViewModel::ajustesJson() const {
    const nlohmann::json& aj = ev_.listaVm().ajustesHistorico();
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
            {"evento_automatico", ev_.listaVm().eventoAutomatico()},
            {"retencion",
             {{"grabaciones_dias", ret.is_object() ? ret.value("grabaciones_dias", 0) : 0},
              {"aviso_disco_pct", ret.is_object() ? ret.value("aviso_disco_pct", 90) : 90}}}};
}

QVariantMap HistoricoViewModel::ajustes() const { return aQv(ajustesJson()).toMap(); }

std::string HistoricoViewModel::guardarAjustesJson(const nlohmann::json& c) {
    nlohmann::json j = ev_.listaVm().ajustesHistorico();
    if (c.contains("registrar") && c["registrar"].is_object()) {
        nlohmann::json reg = j.value("registrar", nlohmann::json::object());
        if (!reg.is_object()) reg = nlohmann::json::object();
        for (const auto& [k, v] : c["registrar"].items()) {
            const auto& cats = evento::Bitacora::categorias();
            if (std::find(cats.begin(), cats.end(), k) == cats.end())
                return tr("no hay una categoría «%1»").arg(QString::fromStdString(k)).toStdString();
            if (!v.is_boolean()) return tr("«%1»: true o false").arg(QString::fromStdString(k)).toStdString();
            reg[k] = v;
        }
        j["registrar"] = reg;
    }
    if (c.contains("retencion")) {
        if (!c["retencion"].is_object()) return tr("`retencion`: {grabaciones_dias, aviso_disco_pct}").toStdString();
        json ret = j.value("retencion", json::object());
        if (!ret.is_object()) ret = json::object();
        for (const auto& [k, v] : c["retencion"].items()) {
            if (k != "grabaciones_dias" && k != "aviso_disco_pct")
                return tr("en `retencion` no hay «%1»").arg(QString::fromStdString(k)).toStdString();
            if (!v.is_number_integer() || v.get<int>() < 0 || (k == "aviso_disco_pct" && v.get<int>() > 100))
                return tr("«%1»: un entero (días, o % de 0 a 100)").arg(QString::fromStdString(k)).toStdString();
            ret[k] = v;
        }
        j["retencion"] = ret;
    }
    if (c.contains("bus_excluir")) {
        if (!c["bus_excluir"].is_array()) return tr("`bus_excluir`: una lista de patrones del bus").toStdString();
        j["bus_excluir"] = c["bus_excluir"];
    }
    if (c.contains("evento_automatico") && c["evento_automatico"].is_boolean()) j["evento_automatico"] = c["evento_automatico"];
    // Todo de una (la caché de la lista y el archivo, por persist; el automático avisa solo).
    ev_.listaVm().guardarAjustesHistorico(std::move(j));
    if (aplicarAjustes_) aplicarAjustes_(ajustesJson());
    emit ajustesCambio();
    if (c.contains("retencion")) medirDisco(); // el aviso del disco depende del %
    return {};
}

QString HistoricoViewModel::guardarAjustes(const QVariantMap& cambios) {
    return QString::fromStdString(guardarAjustesJson(deQv(cambios)));
}

} // namespace ba::app

#include "moc_HistoricoViewModel.cpp"
