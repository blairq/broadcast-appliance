// ============================================================================
// src/app/ui/DashboardModel.cpp — ver DashboardModel.hpp
// ============================================================================

#include "ui/DashboardModel.hpp"
#include "pantallas/Tarjetas.hpp"
#include <QCoreApplication>
#include <QSizeF>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>

namespace ba::app {

namespace {

// Grilla de encaje: lo bastante gruesa como para que las tarjetas queden
// alineadas entre si (y para dibujarla mientras se acomoda el tablero).
constexpr double kGridX = 1.0 / 24.0; // 24 x 16
constexpr double kGridY = 1.0 / 16.0;
constexpr double kGapPx = 12.0;    // separacion entre tarjetas vecinas
constexpr double kMagnetPx = 18.0; // distancia de "iman"
constexpr double kMinW = 0.10;
constexpr double kMinH = 0.10;

// Los tipos de tarjeta salen de pantallas/Tarjetas.hpp: la misma tabla que usa
// el catálogo de cosas que se pueden mostrar (ventanas y monitores).
const auto& kKinds = tarjetas::kTipos;

} // namespace

DashboardModel::DashboardModel(std::filesystem::path file, QObject* parent)
    : QAbstractListModel(parent), file_(std::move(file)) {
    load();
    // Qué tipos están en el tablero (la barra de tarjetas los ilumina).
    connect(this, &QAbstractItemModel::rowsInserted, this, &DashboardModel::tarjetasCambio);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &DashboardModel::tarjetasCambio);
    connect(this, &QAbstractItemModel::modelReset, this, &DashboardModel::tarjetasCambio);
}

QVariantList DashboardModel::tiposDeTarjeta() const {
    QVariantList out;
    for (const auto& k : kKinds) {
        const QString id = QString::fromUtf8(k.id);
        const bool abierta = std::any_of(cards_.begin(), cards_.end(), [&](const Card& c) { return c.kind == id; });
        out << QVariantMap{{"kind", id}, {"titulo", QCoreApplication::translate("tarjetas", k.titulo)}, {"icono", QString::fromUtf8(k.icono)},
                           {"abierta", abierta}};
    }
    return out;
}

bool DashboardModel::alternarTarjeta(const QString& kind) {
    // A la vista: se oculta (todas las de ese tipo), y se guarda cómo estaba.
    bool habia = false;
    for (int row = rowCount() - 1; row >= 0; --row) {
        if (cards_[static_cast<std::size_t>(row)].kind != kind) continue;
        habia = true;
        std::erase_if(ocultas_, [&](const Card& o) { return o.kind == kind; });
        ocultas_.push_back(cards_[static_cast<std::size_t>(row)]);
        beginRemoveRows({}, row, row);
        cards_.erase(cards_.begin() + row);
        endRemoveRows();
    }
    if (habia) {
        save();
        return false;
    }
    // Oculta: vuelve donde estaba, con lo que mostraba; nunca vista, una nueva.
    const auto it = std::find_if(ocultas_.begin(), ocultas_.end(), [&](const Card& o) { return o.kind == kind; });
    if (it == ocultas_.end()) {
        addCard(kind, kind == "monitor" ? QStringLiteral("program") : QString());
        return true;
    }
    Card c = *it;
    ocultas_.erase(it);
    c.id = nextId_++;
    const int row = rowCount();
    beginInsertRows({}, row, row);
    cards_.push_back(c);
    endInsertRows();
    save();
    return true;
}

void DashboardModel::setBackgroundLayout(const QString& l) {
    if (l == layout_) {
        return;
    }
    layout_ = l;
    emit backgroundChanged();
    save();
}

int DashboardModel::rowCount(const QModelIndex&) const { return static_cast<int>(cards_.size()); }

QVariant DashboardModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rowCount()) {
        return {};
    }
    const Card& c = cards_[static_cast<std::size_t>(index.row())];
    switch (role) {
        case IdRole: return c.id;
        case KindRole: return c.kind;
        case XRole: return c.rect.x();
        case YRole: return c.rect.y();
        case WRole: return c.rect.width();
        case HRole: return c.rect.height();
        case SourceRole: return c.source;
        case TitleRole: return kindTitle(c.kind);
        default: return {};
    }
}

QHash<int, QByteArray> DashboardModel::roleNames() const {
    return {{IdRole, "cardId"}, {KindRole, "kind"}, {XRole, "fx"},          {YRole, "fy"},
            {WRole, "fw"},      {HRole, "fh"},      {SourceRole, "source"}, {TitleRole, "title"}};
}

QStringList DashboardModel::cardKinds() const {
    QStringList out;
    for (const auto& k : kKinds) {
        out << k.id;
    }
    return out;
}

QString DashboardModel::kindTitle(const QString& kind) const {
    for (const auto& k : kKinds) {
        if (kind == k.id) {
            return QCoreApplication::translate("tarjetas", k.titulo);
        }
    }
    // Las de Extensiones/ las nombra su ficha; aca no se las conoce. Devolver
    // el kind pelado («ext:reloj») ya es mejor que nada: se ve de donde viene.
    return kind;
}

void DashboardModel::setBackground(const QStringList& keys) {
    if (keys == background_) {
        return;
    }
    background_ = keys;
    emit backgroundChanged();
    save();
}

void DashboardModel::setBackgroundDim(double d) {
    d = std::clamp(d, 0.0, 0.9);
    if (std::abs(d - dim_) < 1e-6) {
        return;
    }
    dim_ = d;
    emit backgroundChanged();
    save();
}

int DashboardModel::rowOf(int id) const {
    for (std::size_t i = 0; i < cards_.size(); ++i) {
        if (cards_[i].id == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

/// El tamaño con el que nace cada tarjeta, en fraccion del tablero.
///
/// No todas piden lo mismo: «Escenas» tiene los dos buses arriba (al aire /
/// armada / TAKE) y despues la grilla, asi que con el tamaño generico nace
/// apretada y lo primero que hay que hacer es agrandarla. Una tarjeta que hay
/// que acomodar antes de que sirva es una tarjeta mal nacida.
QSizeF tamanoInicial(const QString& kind) {
    if (kind == "direccion") return {0.34, 0.34};
    // Aire: al aire/armada/TAKE, FTB y placa, transmisión y grabación.
    if (kind == "aire") return {0.30, 0.34};
    if (kind == "scenes") return {0.34, 0.42};
    if (kind == "mixer" || kind == "guests" || kind == "speakers") return {0.30, 0.38};
    if (kind == "monitor") return {0.28, 0.34};
    // Ancha y baja: la tira en perspectiva necesita lugar a los costados para
    // que se vean las de al lado, que es de donde sale la orientacion.
    if (kind == "carrusel") return {0.40, 0.30};
    // Alta: joystick, zoom y las posiciones guardadas debajo.
    if (kind == "ptz") return {0.24, 0.42};
    // La página con sus flechas, el zoom y el joystick, y abajo las carátulas de los PDF.
    if (kind == "pdf") return {0.34, 0.50};
    return {0.28, 0.30};
}

void DashboardModel::addCard(const QString& kind, const QString& source) {
    // Aparece centrada; el operador la lleva a su lugar.
    const int row = rowCount();
    const QSizeF t = tamanoInicial(kind);
    beginInsertRows({}, row, row);
    cards_.push_back(Card{nextId_++, kind,
                          QRectF(0.5 - t.width() / 2, 0.5 - t.height() / 2, t.width(), t.height()), source});
    endInsertRows();
    save();
}

void DashboardModel::removeCard(int id) {
    const int row = rowOf(id);
    if (row < 0) {
        return;
    }
    beginRemoveRows({}, row, row);
    cards_.erase(cards_.begin() + row);
    endRemoveRows();
    save();
}

void DashboardModel::setCardSource(int id, const QString& source) {
    const int row = rowOf(id);
    if (row < 0) {
        return;
    }
    cards_[static_cast<std::size_t>(row)].source = source;
    emit dataChanged(index(row), index(row), {SourceRole});
    save();
}

int DashboardModel::gridCols() const { return static_cast<int>(std::lround(1.0 / kGridX)); }
int DashboardModel::gridRows() const { return static_cast<int>(std::lround(1.0 / kGridY)); }

QRectF DashboardModel::snap(int id, double x, double y, double w, double h, double aspect) const {
    // Separaciones en fracciones: el mismo numero de pixeles en X y en Y.
    const double px = 1.0 / 1000.0; // el area se piensa de 1000 px de alto
    const double gx = kGapPx * px / aspect;
    const double gy = kGapPx * px;
    const double mx = kMagnetPx * px / aspect;
    const double my = kMagnetPx * px;

    // 1) Grilla. El minimo va DESPUES del redondeo (y redondeado hacia
    //    arriba): redondear primero podia dejar la tarjeta por debajo.
    x = std::round(x / kGridX) * kGridX;
    y = std::round(y / kGridY) * kGridY;
    w = std::max(std::round(w / kGridX) * kGridX, std::ceil(kMinW / kGridX) * kGridX);
    h = std::max(std::round(h / kGridY) * kGridY, std::ceil(kMinH / kGridY) * kGridY);

    // 2) Iman con bordes de otras tarjetas (con separacion) y del area.
    std::vector<double> xs = {gx, 1.0 - gx};
    std::vector<double> ys = {gy, 1.0 - gy};
    for (const auto& c : cards_) {
        if (c.id == id) {
            continue;
        }
        const QRectF& r = c.rect;
        xs.insert(xs.end(), {r.left(), r.right(), r.left() - gx, r.right() + gx});
        ys.insert(ys.end(), {r.top(), r.bottom(), r.top() - gy, r.bottom() + gy});
    }
    const auto magnet = [](double v, const std::vector<double>& targets, double reach) {
        double best = v;
        double dist = reach;
        for (double t : targets) {
            if (std::abs(v - t) < dist) {
                dist = std::abs(v - t);
                best = t;
            }
        }
        return best;
    };
    // Borde izquierdo o derecho, el que quede mas cerca de un iman.
    const double left = magnet(x, xs, mx);
    const double right = magnet(x + w, xs, mx);
    if (left != x) {
        x = left;
    } else if (right != x + w) {
        x = right - w;
    }
    const double top = magnet(y, ys, my);
    const double bottom = magnet(y + h, ys, my);
    if (top != y) {
        y = top;
    } else if (bottom != y + h) {
        y = bottom - h;
    }

    // 3) Siempre adentro del area.
    w = std::min(w, 1.0);
    h = std::min(h, 1.0);
    x = std::clamp(x, 0.0, 1.0 - w);
    y = std::clamp(y, 0.0, 1.0 - h);
    return QRectF(x, y, w, h);
}

void DashboardModel::setCardRect(int id, double x, double y, double w, double h) {
    const int row = rowOf(id);
    if (row < 0) {
        return;
    }
    cards_[static_cast<std::size_t>(row)].rect = QRectF(x, y, w, h);
    emit dataChanged(index(row), index(row), {XRole, YRole, WRole, HRole});
    save();
}

void DashboardModel::raise(int id) {
    const int row = rowOf(id);
    if (row < 0 || row == rowCount() - 1) {
        return;
    }
    beginMoveRows({}, row, row, {}, rowCount());
    std::rotate(cards_.begin() + row, cards_.begin() + row + 1, cards_.end());
    endMoveRows();
    save();
}

void DashboardModel::defaults() {
    cards_.clear();
    nextId_ = 1;
    const auto add = [&](const char* kind, QRectF r, const char* src = "") {
        cards_.push_back(Card{nextId_++, kind, r, src});
    };
    // Parte superior libre para ver el fondo (como un monitor grande); tarjetas
    // en dos filas abajo y un monitor alto a la derecha.
    add("outputs", QRectF(0.00, 0.40, 0.24, 0.29));
    add("scenes", QRectF(0.25, 0.40, 0.24, 0.29));
    add("health", QRectF(0.50, 0.40, 0.24, 0.29));
    add("mixer", QRectF(0.00, 0.70, 0.49, 0.30));
    add("guests", QRectF(0.50, 0.70, 0.24, 0.30));
    add("monitor", QRectF(0.75, 0.40, 0.25, 0.60), "guest:1");
    background_ = QStringList{"program"};
    layout_ = "grid";
    dim_ = 0.35;
}

void DashboardModel::resetLayout() {
    beginResetModel();
    ocultas_.clear();
    defaults();
    endResetModel();
    emit backgroundChanged();
    save();
}

void DashboardModel::load() {
    QFile f(QString::fromStdString(file_.string()));
    if (!f.open(QIODevice::ReadOnly)) {
        defaults();
        return;
    }
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonArray cards = o.value("cards").toArray();
    if (cards.isEmpty()) {
        defaults();
        return;
    }
    for (const auto& v : cards) {
        const QJsonObject c = v.toObject();
        Card card{nextId_++, c.value("kind").toString(),
                  QRectF(c.value("x").toDouble(), c.value("y").toDouble(), c.value("w").toDouble(0.25),
                         c.value("h").toDouble(0.25)),
                  c.value("source").toString()};
        cards_.push_back(card);
    }
    for (const auto& v : o.value("ocultas").toArray()) {
        const QJsonObject c = v.toObject();
        ocultas_.push_back(Card{0, c.value("kind").toString(),
                                QRectF(c.value("x").toDouble(), c.value("y").toDouble(), c.value("w").toDouble(0.25),
                                       c.value("h").toDouble(0.25)),
                                c.value("source").toString()});
    }
    background_.clear();
    for (const auto& v : o.value("background").toArray()) {
        background_ << v.toString();
    }
    dim_ = o.value("backgroundDim").toDouble(0.35);
    layout_ = o.value("backgroundLayout").toString("grid");
}

void DashboardModel::save() const {
    // Ruta vacia = esta disposicion no se persiste. Es lo que pide el arranque
    // cuando ui.json se llevo puesto el arranque anterior: se trabaja con la
    // disposicion por defecto y NO se pisa el archivo, para poder mirarlo.
    if (file_.empty()) return;
    QJsonArray cards;
    for (const auto& c : cards_) {
        cards.append(QJsonObject{{"kind", c.kind},
                                 {"x", c.rect.x()},
                                 {"y", c.rect.y()},
                                 {"w", c.rect.width()},
                                 {"h", c.rect.height()},
                                 {"source", c.source}});
    }
    QJsonArray ocultas;
    for (const auto& c : ocultas_)
        ocultas.append(QJsonObject{{"kind", c.kind}, {"x", c.rect.x()}, {"y", c.rect.y()}, {"w", c.rect.width()},
                                   {"h", c.rect.height()}, {"source", c.source}});
    const QJsonObject o{{"backgroundLayout", layout_},
                        {"cards", cards},
                        {"ocultas", ocultas},
                        {"background", QJsonArray::fromStringList(background_)},
                        {"backgroundDim", dim_}};
    std::error_code ec;
    std::filesystem::create_directories(file_.parent_path(), ec);
    QFile f(QString::fromStdString(file_.string()));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(o).toJson());
    }
}

} // namespace ba::app
