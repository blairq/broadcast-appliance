// ============================================================================
// src/app/pantallas/BuscadorCatalogo.cpp — ver BuscadorCatalogo.hpp
// ============================================================================
#include "pantallas/BuscadorCatalogo.hpp"

#include "ui/FiltroCatalogo.hpp"

#include <QAbstractItemModel>

#include <algorithm>
#include <utility>
#include <vector>

namespace ba::app {

BuscadorCatalogo::BuscadorCatalogo(QObject* parent) : QSortFilterProxyModel(parent) {
    // Escribir rápido no filtra en cada tecla (con miles de cosas se nota).
    rebote_.setSingleShot(true);
    rebote_.setInterval(90);
    connect(&rebote_, &QTimer::timeout, this, &BuscadorCatalogo::filtrarYa);
    // Lo que se ve cambió (filtró, o el catálogo sumó o sacó algo): la cuenta y la fila
    // de la elegida también.
    juntar_.setSingleShot(true);
    juntar_.setInterval(60);
    connect(&juntar_, &QTimer::timeout, this, &BuscadorCatalogo::alDiaYa);
    connect(this, &QAbstractItemModel::rowsInserted, this, &BuscadorCatalogo::pedirAlDia);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &BuscadorCatalogo::pedirAlDia);
    connect(this, &QAbstractItemModel::modelReset, this, &BuscadorCatalogo::pedirAlDia);
    connect(this, &QAbstractItemModel::layoutChanged, this, &BuscadorCatalogo::pedirAlDia);
}

void BuscadorCatalogo::setFuente(QObject* f) {
    auto* m = qobject_cast<QAbstractItemModel*>(f);
    if (m == sourceModel()) return;
    if (QAbstractItemModel* viejo = sourceModel()) disconnect(viejo, nullptr, this, nullptr);
    rolId_ = rolGrupo_ = rolGrupoTitulo_ = rolTitulo_ = rolLocal_ = rolRed_ = -1;
    if (m != nullptr) {
        const QHash<int, QByteArray> roles = m->roleNames();
        rolId_ = roles.key("itemId", -1);
        rolGrupo_ = roles.key("grupo", -1);
        rolGrupoTitulo_ = roles.key("grupoTitulo", -1);
        rolTitulo_ = roles.key("titulo", -1);
        rolLocal_ = roles.key("local", -1);
        rolRed_ = roles.key("red", -1);
    }
    setSourceModel(m);
    if (m != nullptr) {
        // El catálogo cambió (una escena nueva, una distribución renombrada): los grupos
        // y sus cuentas, y la elegida (la vista previa se enlaza a `actual`).
        connect(m, &QAbstractItemModel::rowsInserted, this, &BuscadorCatalogo::pedirAlDia);
        connect(m, &QAbstractItemModel::rowsRemoved, this, &BuscadorCatalogo::pedirAlDia);
        connect(m, &QAbstractItemModel::modelReset, this, &BuscadorCatalogo::pedirAlDia);
        connect(m, &QAbstractItemModel::dataChanged, this, &BuscadorCatalogo::pedirAlDia);
        connect(m, &QAbstractItemModel::rowsMoved, this, &BuscadorCatalogo::pedirAlDia);
    }
    recontar();
    emit fuenteCambio();
    emit resultadoCambio();
    emit elegidoCambio();
}

void BuscadorCatalogo::pedirAlDia() {
    if (!juntar_.isActive()) juntar_.start();
}

void BuscadorCatalogo::alDiaYa() {
    juntar_.stop();
    recontar();
    emit resultadoCambio();
    emit elegidoCambio();
}

void BuscadorCatalogo::setTexto(const QString& t) {
    if (t == texto_) return;
    texto_ = t;
    emit textoCambio();
    rebote_.start();
}

void BuscadorCatalogo::filtrarYa() {
    rebote_.stop();
    if (texto_ == textoAplicado_) return;
    textoAplicado_ = texto_;
    palabras_ = palabras(texto_);
    refiltrar();
}

void BuscadorCatalogo::setGrupo(const QString& g) {
    if (g == grupo_) return;
    grupo_ = g;
    emit grupoCambio();
    refiltrar();
}

void BuscadorCatalogo::setDonde(const QString& d) {
    const QString v = d == "monitor" || d == "red" ? d : QStringLiteral("todo");
    if (v == donde_) return;
    donde_ = v;
    emit dondeCambio();
    refiltrar();
}

void BuscadorCatalogo::setExcluir(const QStringList& e) {
    if (e == excluir_) return;
    excluir_ = e;
    emit excluirCambio();
    refiltrar();
}

void BuscadorCatalogo::refiltrar() {
    invalidateFilter();
    recontar();
    emit resultadoCambio();
    emit elegidoCambio();
}

QStringList BuscadorCatalogo::palabras(const QString& texto) {
    return FiltroCatalogo::plano(texto.trimmed()).split(QChar(' '), Qt::SkipEmptyParts);
}

bool BuscadorCatalogo::coincide(const QStringList& palabras, const QString& plano) {
    for (const QString& p : palabras)
        if (!plano.contains(p)) return false;
    return true;
}

QString BuscadorCatalogo::textoDeBusqueda(const QString& titulo, const QString& grupoTitulo, const QString& id) {
    return FiltroCatalogo::plano(titulo + QChar(' ') + grupoTitulo + QChar(' ') + id);
}

QString BuscadorCatalogo::grupoDe(int fila) const {
    const QAbstractItemModel* m = sourceModel();
    return m == nullptr || rolGrupo_ < 0 ? QString() : m->data(m->index(fila, 0), rolGrupo_).toString();
}

bool BuscadorCatalogo::entraFijo(int fila) const {
    const QAbstractItemModel* m = sourceModel();
    if (m == nullptr) return false;
    const QModelIndex i = m->index(fila, 0);
    if (donde_ == "monitor" && rolLocal_ >= 0 && !m->data(i, rolLocal_).toBool()) return false;
    if (donde_ == "red" && rolRed_ >= 0 && !m->data(i, rolRed_).toBool()) return false;
    return excluir_.isEmpty() || !excluir_.contains(grupoDe(fila));
}

bool BuscadorCatalogo::coincideTexto(int fila) const {
    if (palabras_.isEmpty()) return true;
    const QAbstractItemModel* m = sourceModel();
    const QModelIndex i = m->index(fila, 0);
    const auto rol = [&](int r) { return r >= 0 ? m->data(i, r).toString() : QString(); };
    return coincide(palabras_, textoDeBusqueda(rol(rolTitulo_), rol(rolGrupoTitulo_), rol(rolId_)));
}

bool BuscadorCatalogo::filterAcceptsRow(int fila, const QModelIndex& padre) const {
    if (padre.isValid() || sourceModel() == nullptr) return false;
    if (!entraFijo(fila)) return false;
    if (!grupo_.isEmpty() && grupoDe(fila) != grupo_) return false;
    return coincideTexto(fila);
}

void BuscadorCatalogo::recontar() {
    const QAbstractItemModel* m = sourceModel();
    int total = 0;
    // Los grupos en el orden en que aparecen en el catálogo (el del operador).
    std::vector<std::pair<QString, std::pair<QString, int>>> gs;
    if (m != nullptr)
        for (int r = 0; r < m->rowCount(); ++r) {
            if (!entraFijo(r)) continue;
            ++total;
            const QString g = grupoDe(r);
            auto it = gs.begin();
            while (it != gs.end() && it->first != g) ++it;
            if (it == gs.end()) {
                const QString titulo = rolGrupoTitulo_ >= 0 ? m->data(m->index(r, 0), rolGrupoTitulo_).toString() : g;
                gs.push_back({g, {titulo, 0}});
                it = gs.end() - 1;
            }
            if (coincideTexto(r)) ++it->second.second;
        }
    QVariantList grupos;
    for (const auto& [g, tc] : gs)
        grupos << QVariantMap{{"grupo", g}, {"titulo", tc.first}, {"cuenta", tc.second}};
    // Quien llama avisa (resultadoCambio), una vez.
    total_ = total;
    grupos_ = std::move(grupos);
}

void BuscadorCatalogo::setElegido(const QString& id) {
    if (id == elegido_) return;
    elegido_ = id;
    emit elegidoCambio();
}

int BuscadorCatalogo::filaElegida() const {
    if (elegido_.isEmpty() || rolId_ < 0) return -1;
    for (int r = 0; r < rowCount(); ++r)
        if (data(index(r, 0), rolId_).toString() == elegido_) return r;
    return -1;
}

QVariantMap BuscadorCatalogo::actual() const {
    const QAbstractItemModel* m = sourceModel();
    if (m == nullptr || elegido_.isEmpty() || rolId_ < 0) return {};
    const QHash<int, QByteArray> roles = m->roleNames();
    for (int r = 0; r < m->rowCount(); ++r) {
        const QModelIndex i = m->index(r, 0);
        if (m->data(i, rolId_).toString() != elegido_) continue;
        QVariantMap out;
        for (auto it = roles.begin(); it != roles.end(); ++it) out.insert(QString::fromUtf8(it.value()), m->data(i, it.key()));
        return out;
    }
    return {};
}

void BuscadorCatalogo::elegirFila(int fila) {
    if (fila < 0 || fila >= rowCount() || rolId_ < 0) return;
    setElegido(data(index(fila, 0), rolId_).toString());
}

void BuscadorCatalogo::mover(int delta) {
    const int n = rowCount();
    if (n == 0) return;
    const int ahora = filaElegida();
    const int fila = ahora < 0 ? 0 : std::clamp(ahora + delta, 0, n - 1);
    elegirFila(fila);
}

} // namespace ba::app
