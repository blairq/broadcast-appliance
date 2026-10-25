// ============================================================================
// src/app/pantallas/Monitores.cpp — ver Monitores.hpp
// ============================================================================
#include "pantallas/Monitores.hpp"

#include "pantallas/Catalogo.hpp"

#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QScreen>

#include <algorithm>
#include <map>

namespace ba::app {
namespace {

/// Como mucho tantas cosas en un monitor: más que una grilla de 3×3 ya no se lee.
constexpr int kMaxCosas = 9;

/// El orden FÍSICO: por filas de arriba abajo y, en cada fila, de izquierda a
/// derecha (como se lee). Dos monitores están en la misma fila si se pisan en
/// vertical más de la mitad del más bajo: así uno un poco más alto que el de
/// al lado (o apoyado más arriba) no arma una fila propia.
std::vector<PantallaFisica> ordenFisico(std::vector<PantallaFisica> v) {
    std::sort(v.begin(), v.end(), [](const PantallaFisica& a, const PantallaFisica& b) {
        return a.geometria.center().y() < b.geometria.center().y();
    });
    std::vector<std::vector<PantallaFisica>> filas;
    for (PantallaFisica& p : v) {
        bool puesta = false;
        for (auto& f : filas) {
            const QRect& r = f.front().geometria;
            const int pisan = std::min(r.bottom(), p.geometria.bottom()) - std::max(r.top(), p.geometria.top());
            if (pisan * 2 > std::min(r.height(), p.geometria.height())) {
                f.push_back(p);
                puesta = true;
                break;
            }
        }
        if (!puesta) filas.push_back({p});
    }
    std::vector<PantallaFisica> out;
    for (auto& f : filas) {
        std::sort(f.begin(), f.end(), [](const PantallaFisica& a, const PantallaFisica& b) {
            return a.geometria.x() < b.geometria.x();
        });
        out.insert(out.end(), f.begin(), f.end());
    }
    return out;
}

} // namespace

MonitoresModel::MonitoresModel(std::filesystem::path archivo, CatalogoVisible* catalogo, QObject* parent)
    : ListaConClave(parent), archivo_(std::move(archivo)), catalogo_(catalogo) {
    // Primero lo recordado (todo «esperando» hasta que el sistema diga qué hay).
    cargar();
    activos_ = new QSortFilterProxyModel(this);
    activos_->setSourceModel(this);
    activos_->setFilterRole(ActivaRole);
    activos_->setFilterRegularExpression(QRegularExpression(QStringLiteral("^true$")));
    // Los títulos salen del catálogo: si una escena se renombra o se borra, lo
    // que dice cada monitor cambia con ella.
    if (catalogo_ != nullptr)
        connect(catalogo_, &CatalogoVisible::cambio, this, [this]() {
            if (rowCount() > 0) emit dataChanged(index(0), index(rowCount() - 1), {TituloRole});
        });
}

QString MonitoresModel::idDe(const PantallaFisica& p) {
    const QString fab = p.fabricante.trimmed();
    const QString mod = p.modelo.trimmed();
    const QString serie = p.serie.trimmed();
    if (!serie.isEmpty()) return fab + "|" + mod + "|" + serie;
    // Sin serie: el nombre del sistema desempata (ver la cabecera).
    if (fab.isEmpty() && mod.isEmpty()) return "@" + p.nombre.trimmed();
    return fab + "|" + mod + "|@" + p.nombre.trimmed();
}

QString MonitoresModel::idDePantalla(const QString& nombre, const QString& fabricante, const QString& modelo,
                                     const QString& serie) const {
    PantallaFisica p{nombre, fabricante, modelo, serie, {}, false};
    const QString base = idDe(p);
    // Los repetidos llevan «#n» según su orden: hay que buscarlo en las filas.
    for (const Fila& f : filas_)
        if (f.conectado && f.pantalla.nombre == nombre && idDe(f.pantalla) == base) return f.id;
    return base;
}

void MonitoresModel::actualizarPantallas(const std::vector<PantallaFisica>& pantallas) {
    pantallas_ = pantallas;
    const std::vector<PantallaFisica> orden = ordenFisico(pantallas);
    // Lo recordado de cada id (de las filas de ahora: conectadas o esperando).
    std::map<QString, Fila> antes;
    for (const Fila& f : filas_) antes[f.id] = f;

    std::vector<Fila> nuevas;
    std::map<QString, int> vistos;
    const QRect limitesAntes = limites_;
    limites_ = {};
    for (std::size_t i = 0; i < orden.size(); ++i) {
        const PantallaFisica& p = orden[i];
        QString id = idDe(p);
        // Dos monitores iguales sin serie: el segundo (en el orden físico) es «#2».
        const int n = ++vistos[id];
        if (n > 1) id += "#" + QString::number(n);
        Fila f;
        auto it = antes.find(id);
        if (it != antes.end()) f = it->second;
        f.id = id;
        f.pantalla = p;
        f.conectado = true;
        f.orden = static_cast<int>(i) + 1;
        f.descripcion = nombreDe(f);
        nuevas.push_back(f);
        limites_ = limites_.isNull() ? p.geometria : limites_.united(p.geometria);
    }
    // Los recordados que no están: se los espera, abajo, con su contenido
    // intacto (no se le pasa a ningún otro). Uno sin nada asignado no se
    // espera: no hay nada que devolverle.
    for (const Fila& f : filas_) {
        if (f.cosas.isEmpty()) continue;
        if (std::any_of(nuevas.begin(), nuevas.end(), [&](const Fila& n) { return n.id == f.id; })) continue;
        Fila e = f;
        e.conectado = false;
        e.orden = 0;
        e.pantalla = {};
        nuevas.push_back(e);
    }
    bool hubo = aplicar(std::move(nuevas));
    // El dibujo de cada uno es relativo al conjunto: si el conjunto cambió (se
    // fue el de la punta), cambian todos aunque su geometría sea la misma.
    if (limites_ != limitesAntes && rowCount() > 0) {
        emit dataChanged(index(0), index(rowCount() - 1), {DibujoRole});
        hubo = true;
    }
    if (hubo) emit cambio();
}

void MonitoresModel::conectarAlSistema() {
    auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
    if (app == nullptr) return;
    // `fuera`: la que se está yendo (según la versión de Qt, todavía figura en
    // screens() cuando avisa).
    const auto leer = [this](QScreen* fuera) {
        std::vector<PantallaFisica> v;
        const QScreen* principal = QGuiApplication::primaryScreen();
        for (QScreen* s : QGuiApplication::screens()) {
            if (s == nullptr || s == fuera) continue;
            v.push_back({s->name(), s->manufacturer(), s->model(), s->serialNumber(), s->geometry(), s == principal});
        }
        actualizarPantallas(v);
    };
    const auto enganchar = [this, leer](QScreen* s) {
        connect(s, &QScreen::geometryChanged, this, [leer]() { leer(nullptr); });
    };
    for (QScreen* s : QGuiApplication::screens()) enganchar(s);
    connect(app, &QGuiApplication::screenAdded, this, [enganchar, leer](QScreen* s) {
        enganchar(s);
        leer(nullptr);
    });
    connect(app, &QGuiApplication::screenRemoved, this, [leer](QScreen* s) { leer(s); });
    connect(app, &QGuiApplication::primaryScreenChanged, this, [leer](QScreen*) { leer(nullptr); });
    leer(nullptr);
}

QVariant MonitoresModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rowCount()) return {};
    const Fila& f = filas_[static_cast<std::size_t>(index.row())];
    switch (role) {
        case IdRole: return f.id;
        case NombreRole: return f.descripcion.isEmpty() ? f.id : f.descripcion;
        case NombreSistemaRole: return f.pantalla.nombre;
        case ConectadoRole: return f.conectado;
        case PrincipalRole: return f.pantalla.principal;
        case OrdenRole: return f.orden;
        case GeometriaRole: return f.pantalla.geometria;
        case DibujoRole: {
            if (!f.conectado || limites_.width() <= 0 || limites_.height() <= 0) return QRectF();
            const QRect& g = f.pantalla.geometria;
            const double w = limites_.width(), h = limites_.height();
            return QRectF((g.x() - limites_.x()) / w, (g.y() - limites_.y()) / h, g.width() / w, g.height() / h);
        }
        case CosasRole: return f.cosas;
        case TituloRole: return tituloDe(f);
        case PantallaCompletaRole: return f.pantallaCompleta;
        case ActivaRole: return f.conectado && !f.cosas.isEmpty();
        case EsperandoRole: return !f.conectado && !f.cosas.isEmpty();
        default: return {};
    }
}

QHash<int, QByteArray> MonitoresModel::roleNames() const {
    return {{IdRole, "monitorId"},
            {NombreRole, "nombre"},
            {NombreSistemaRole, "nombreSistema"},
            {ConectadoRole, "conectado"},
            {PrincipalRole, "principal"},
            {OrdenRole, "orden"},
            {GeometriaRole, "geometria"},
            {DibujoRole, "dibujo"},
            {CosasRole, "cosas"},
            {TituloRole, "titulo"},
            {PantallaCompletaRole, "pantallaCompleta"},
            {ActivaRole, "activa"},
            {EsperandoRole, "esperando"}};
}

int MonitoresModel::conectados() const {
    return static_cast<int>(std::count_if(filas_.begin(), filas_.end(), [](const Fila& f) { return f.conectado; }));
}

int MonitoresModel::esperando() const {
    return static_cast<int>(std::count_if(filas_.begin(), filas_.end(),
                                          [](const Fila& f) { return !f.conectado && !f.cosas.isEmpty(); }));
}

double MonitoresModel::proporcion() const {
    if (limites_.width() <= 0 || limites_.height() <= 0) return 16.0 / 9.0;
    return static_cast<double>(limites_.width()) / limites_.height();
}

int MonitoresModel::filaDe(const QString& id) const {
    for (std::size_t i = 0; i < filas_.size(); ++i)
        if (filas_[i].id == id) return static_cast<int>(i);
    return -1;
}

QString MonitoresModel::nombreDe(const Fila& f) const {
    const PantallaFisica& p = f.pantalla;
    QString mod = p.modelo.trimmed();
    const QString fab = p.fabricante.trimmed();
    // «DELL U2720Q» y no «Dell Inc. DELL U2720Q»: el modelo suele traer la marca.
    if (!mod.isEmpty() && !fab.isEmpty() && !mod.contains(fab.section(' ', 0, 0), Qt::CaseInsensitive))
        mod = fab.section(' ', 0, 0) + " " + mod;
    if (mod.isEmpty()) mod = p.nombre;
    return mod.isEmpty() ? f.descripcion : mod;
}

QString MonitoresModel::tituloDe(const Fila& f) const {
    QStringList t;
    for (const QString& c : f.cosas) t << (catalogo_ != nullptr ? catalogo_->tituloDe(c) : c);
    return t.join(QStringLiteral(" + "));
}

void MonitoresModel::tocada(int fila) {
    emit dataChanged(index(fila), index(fila));
    emit cambio();
    guardar();
}

QString MonitoresModel::asignar(const QString& id, const QStringList& cosas, bool pantallaCompleta) {
    const int r = filaDe(id);
    if (r < 0) return QStringLiteral("No conozco ese monitor: los que hay salen en la lista de Pantallas conectadas.");
    QStringList limpias;
    for (const QString& c : cosas) {
        const QString t = c.trimmed();
        if (t.isEmpty() || limpias.contains(t)) continue;
        if (catalogo_ != nullptr) {
            const CatalogoVisible::Cosa* cosa = catalogo_->buscar(t);
            if (cosa == nullptr) return "No hay nada que se llame «" + t + "» en lo que se puede mostrar.";
            if (cosa->dibujo.isEmpty())
                return "«" + cosa->titulo + "» no se puede mostrar en un monitor (solo en una TV de la red).";
        }
        limpias << t;
    }
    if (limpias.isEmpty()) return QStringLiteral("Elegí qué mostrar (para dejarlo vacío, «Quitar»).");
    if (limpias.size() > kMaxCosas)
        return "Como mucho " + QString::number(kMaxCosas) + " cosas en un monitor: más ya no se leen.";
    Fila& f = filas_[static_cast<std::size_t>(r)];
    if (f.cosas == limpias && f.pantallaCompleta == pantallaCompleta) return {};
    f.cosas = limpias;
    f.pantallaCompleta = pantallaCompleta;
    tocada(r);
    return {};
}

QString MonitoresModel::setPantallaCompleta(const QString& id, bool si) {
    const int r = filaDe(id);
    if (r < 0) return QStringLiteral("No conozco ese monitor.");
    Fila& f = filas_[static_cast<std::size_t>(r)];
    if (f.pantallaCompleta == si) return {};
    f.pantallaCompleta = si;
    tocada(r);
    return {};
}

QString MonitoresModel::quitar(const QString& id) {
    const int r = filaDe(id);
    if (r < 0) return QStringLiteral("No conozco ese monitor.");
    Fila& f = filas_[static_cast<std::size_t>(r)];
    if (!f.conectado) return olvidar(id); // sin nada y sin estar: no hay qué esperar
    if (f.cosas.isEmpty()) return {};
    f.cosas.clear();
    tocada(r);
    return {};
}

QString MonitoresModel::olvidar(const QString& id) {
    const int r = filaDe(id);
    if (r < 0) return QStringLiteral("No conozco ese monitor.");
    if (filas_[static_cast<std::size_t>(r)].conectado) return quitar(id);
    beginRemoveRows({}, r, r);
    filas_.erase(filas_.begin() + r);
    endRemoveRows();
    emit cambio();
    guardar();
    return {};
}

QVariantMap MonitoresModel::monitor(const QString& id) const {
    const int r = filaDe(id);
    if (r < 0) return {};
    QVariantMap m;
    const auto roles = roleNames();
    for (auto it = roles.begin(); it != roles.end(); ++it)
        m.insert(QString::fromUtf8(it.value()), data(index(r), it.key()));
    return m;
}

QString MonitoresModel::resolver(const QString& quien) const {
    const QString q = quien.trimmed();
    if (filaDe(q) >= 0) return q;
    bool esNumero = false;
    const int n = q.toInt(&esNumero);
    for (const Fila& f : filas_) {
        if (esNumero && f.conectado && f.orden == n) return f.id;
        if (!esNumero && (f.descripcion.compare(q, Qt::CaseInsensitive) == 0 ||
                          (!f.pantalla.nombre.isEmpty() && f.pantalla.nombre.compare(q, Qt::CaseInsensitive) == 0)))
            return f.id;
    }
    return {};
}

void MonitoresModel::cargar() {
    if (archivo_.empty()) return;
    QFile f(QString::fromStdString(archivo_.string()));
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    for (const QJsonValue& v : o.value("monitores").toArray()) {
        const QJsonObject m = v.toObject();
        Fila fila;
        fila.id = m.value("id").toString();
        fila.descripcion = m.value("descripcion").toString();
        for (const QJsonValue& c : m.value("cosas").toArray()) fila.cosas << c.toString();
        fila.pantallaCompleta = m.value("pantallaCompleta").toBool(true);
        if (fila.id.isEmpty() || fila.cosas.isEmpty() || filaDe(fila.id) >= 0) continue;
        filas_.push_back(fila);
    }
}

void MonitoresModel::guardar() const {
    if (archivo_.empty()) return;
    QJsonArray ms;
    for (const Fila& f : filas_) {
        if (f.cosas.isEmpty()) continue;
        ms.append(QJsonObject{{"id", f.id},
                              {"descripcion", f.descripcion},
                              {"cosas", QJsonArray::fromStringList(f.cosas)},
                              {"pantallaCompleta", f.pantallaCompleta}});
    }
    std::error_code ec;
    std::filesystem::create_directories(archivo_.parent_path(), ec);
    QFile f(QString::fromStdString(archivo_.string()));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(QJsonObject{{"monitores", ms}}).toJson());
}

} // namespace ba::app
