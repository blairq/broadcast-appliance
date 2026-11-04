#include "ui/FiltroCatalogo.hpp"

#include <QTimer>

#include <algorithm>
#include <map>

namespace ba::app {

FiltroCatalogo::FiltroCatalogo(QObject* parent) : ListaObjetos(parent) {
    // Varias propiedades cambian juntas (abrir el catálogo pone la fuente, lo escrito y la
    // categoría): se filtra una vez, al final de la vuelta.
    auto* rebote = new QTimer(this);
    rebote->setSingleShot(true);
    rebote->setInterval(0);
    connect(rebote, &QTimer::timeout, this, &FiltroCatalogo::filtrar);
    connect(this, &FiltroCatalogo::entradaCambio, rebote, qOverload<>(&QTimer::start));
}

void FiltroCatalogo::setFuente(const QVariantList& f) {
    if (f == fuente_) return;
    fuente_ = f;
    emit entradaCambio();
}

QString FiltroCatalogo::plano(const QString& s) {
    QString out;
    const QString d = s.toLower().normalized(QString::NormalizationForm_D);
    out.reserve(d.size());
    for (const QChar c : d) {
        if (c.category() == QChar::Mark_NonSpacing) continue;
        out += c == '_' ? QChar(' ') : c;
    }
    return out;
}

QString FiltroCatalogo::textoDe(const QVariantMap& m, const QStringList& campos) const {
    QStringList partes;
    for (const QString& c : campos) {
        const QVariant v = m.value(c);
        if (v.typeId() == QMetaType::QVariantList || v.typeId() == QMetaType::QStringList)
            for (const QVariant& x : v.toList()) partes << (x.typeId() == QMetaType::QVariantMap ? x.toMap().value("id").toString() : x.toString());
        else
            partes << v.toString();
    }
    return plano(partes.join(' '));
}

void FiltroCatalogo::filtrar() {
    const QString buscado = texto_.trimmed();
    const bool buscando = !buscado.isEmpty();
    const bool conPuntajes = buscando && !puntajes_.isEmpty();
    const QStringList palabras = plano(buscado).split(' ', Qt::SkipEmptyParts);

    QStringList categorias;
    QVariantMap cuentas, colores;
    int total = 0;
    struct Fila {
        QVariantMap m;
        double puntos;
        int orden;
    };
    std::vector<Fila> filas;
    for (int i = 0; i < fuente_.size(); ++i) {
        QVariantMap m = fuente_[i].toMap();
        bool cumple = true;
        for (const QString& e : exigir_) cumple = cumple && m.value(e).toBool();
        if (!cumple) continue;
        ++total;
        const QString cat = m.value(campoCategoria_).toString();
        if (!categorias.contains(cat)) categorias << cat;
        if (!colores.contains(cat) && m.contains("color")) colores[cat] = m.value("color");

        double puntos = 0;
        QString porque;
        if (conPuntajes) {
            const QVariantMap p = puntajes_.value(m.value(clave()).toString()).toMap();
            if (p.isEmpty()) continue;
            puntos = p.value("puntaje").toDouble();
            QStringList l;
            for (const QVariant& x : p.value("porque").toList()) l << x.toString();
            porque = l.join(", ");
        } else if (buscando) {
            const QString cabeza = textoDe(m, camposCabeza_);
            const QString todo = cabeza + ' ' + textoDe(m, campos_);
            bool todas = true;
            for (const QString& p : palabras) {
                if (!todo.contains(p)) {
                    todas = false;
                    break;
                }
                puntos += cabeza.contains(p) ? 3 : 1;
            }
            if (!todas) continue;
        }
        if (buscando) cuentas[cat] = cuentas.value(cat).toInt() + 1;
        // La categoría elegida acota siempre (también buscando).
        if (!categoria_.isEmpty() && cat != categoria_) continue;
        m["puntos"] = puntos;
        m["porque"] = porque;
        filas.push_back({std::move(m), puntos, static_cast<int>(filas.size())});
    }
    if (buscando && agrupar_) {
        std::map<QString, double> mejor;
        for (const Fila& f : filas) {
            double& b = mejor[f.m.value(campoCategoria_).toString()];
            b = std::max(b, f.puntos);
        }
        std::stable_sort(filas.begin(), filas.end(), [&](const Fila& a, const Fila& b) {
            const QString ca = a.m.value(campoCategoria_).toString(), cb = b.m.value(campoCategoria_).toString();
            if (mejor[ca] != mejor[cb]) return mejor[ca] > mejor[cb];
            if (ca != cb) return ca.localeAwareCompare(cb) < 0;
            return a.puntos > b.puntos;
        });
    }
    if (ordenarCategorias_) std::sort(categorias.begin(), categorias.end(), [](const QString& a, const QString& b) { return a.localeAwareCompare(b) < 0; });

    QVariantList lista;
    lista.reserve(static_cast<qsizetype>(filas.size()));
    for (Fila& f : filas) lista << std::move(f.m);
    poner(lista);
    const bool cambio = categorias != categorias_ || total != total_ || cuentas != cuentas_ || colores != colores_;
    categorias_ = categorias;
    total_ = total;
    cuentas_ = cuentas;
    colores_ = colores;
    if (cambio) emit resultadoCambio();
}

} // namespace ba::app

#include "moc_FiltroCatalogo.cpp"
