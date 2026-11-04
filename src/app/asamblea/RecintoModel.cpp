#include "asamblea/RecintoModel.hpp"

#include <QCollator>

#include <algorithm>

namespace ba::app::asamblea {

RecintoModel::RecintoModel(Fuentes f, QObject* parent) : QObject(parent), f_(std::move(f)) {
    bancas_.setParent(this);
    tarde_.setSingleShot(true);
    tarde_.setInterval(0);
    connect(&tarde_, &QTimer::timeout, this, &RecintoModel::recalcular);
    recalcular();
}

QVariantMap RecintoModel::fila(const QVariantMap& lugar, const QVariantMap& ctx) const {
    QVariantMap l = lugar;
    const QString banca = lugar.value("banca").toString();
    const QVariantMap i = lugar.value("integrante").toMap();
    const QString id = i.value("id").toString();
    const bool hay = !id.isEmpty();
    // La fila de oradores.
    l["turno"] = hay ? ctx.value("cola").toMap().value(id, 0) : 0;
    l["alAire"] = hay && ctx.value("alAire").toString() == id;
    // La central (solo si está conectada).
    const bool central = ctx.value("conCentral").toBool();
    const QVariant uni = central ? ctx.value("unidades").toMap().value(banca) : QVariant();
    const QVariantMap u = uni.toMap();
    l["unidad"] = uni.isValid() ? uni : QVariant();
    l["micAbierto"] = central && ctx.value("abiertos").toStringList().contains(banca);
    l["turnoCentral"] = central ? ctx.value("pedidos").toMap().value(banca, 0) : 0;
    l["leToca"] = central && !banca.isEmpty() && ctx.value("siguiente").toString() == banca;
    l["presidente"] = uni.isValid() && u.value("tipo").toString() == "presidente";
    l["sinUnidad"] = central && (!uni.isValid() || !u.value("conectada").toBool());
    // La sesión remota.
    const QVariant rem = hay ? ctx.value("remotos").toMap().value(id) : QVariant();
    const QVariantMap r = rem.toMap();
    l["remoto"] = rem.isValid() ? rem : QVariant();
    l["conectado"] = rem.isValid() && r.value("conectado").toBool();
    l["esperando"] = rem.isValid() && r.value("estado").toString() == "esperando";
    l["habilitado"] = rem.isValid() && r.value("estado").toString() == "habilitado";
    // Las fotos: la de su posición PTZ y la cara (en vivo, la del Cuerpo o la de la PTZ).
    const QVariantMap pp = i.value("preset_ptz").toMap();
    const QString ptz = pp.value("camara").toString().isEmpty() || pp.value("posicion").toString().isEmpty()
                            ? QString()
                            : ctx.value("fotosPtz").toMap().value(pp.value("camara").toString() + "/" + pp.value("posicion").toString()).toString();
    l["fotoPtz"] = ptz;
    QString cara;
    if (l.value("conectado").toBool() && f_.fotoRemota) cara = f_.fotoRemota(id);
    if (cara.isEmpty()) cara = i.value("foto_url").toString();
    if (cara.isEmpty()) cara = ptz;
    l["fotoCara"] = hay ? cara : QString();
    return l;
}

void RecintoModel::recalcular() {
    const QVariantMap r = f_.recinto ? f_.recinto() : QVariantMap{};
    const QVariantMap o = f_.oradores ? f_.oradores() : QVariantMap{};
    const QVariantMap d = f_.central ? f_.central() : QVariantMap{};
    // Todo lo que se cruza, una vez, por clave.
    QVariantMap ctx;
    {
        QVariantMap cola;
        const QVariantList c = o.value("cola").toList();
        for (int k = 0; k < c.size(); ++k) cola.insert(c[k].toMap().value("id").toString(), k + 1);
        ctx["cola"] = cola;
        ctx["alAire"] = o.value("al_aire").toMap().value("id");
    }
    ctx["conCentral"] = f_.centralConectada && f_.centralConectada();
    {
        QVariantMap unidades, pedidos;
        for (const QVariant& u : d.value("unidades").toList()) {
            const QString b = u.toMap().value("banca").toString();
            if (!b.isEmpty() && !unidades.contains(b)) unidades.insert(b, u);
        }
        for (const QVariant& p : d.value("pedidos").toList()) {
            const QString b = p.toMap().value("banca").toString();
            if (!b.isEmpty() && !pedidos.contains(b)) pedidos.insert(b, p.toMap().value("turno"));
        }
        ctx["unidades"] = unidades;
        ctx["pedidos"] = pedidos;
        QStringList abiertos;
        for (const QVariant& a : d.value("abiertos").toList()) abiertos << a.toString();
        ctx["abiertos"] = abiertos;
        ctx["siguiente"] = d.value("siguiente");
    }
    bool esperando = false;
    {
        QVariantMap remotos;
        for (const QVariant& p : f_.participantes ? f_.participantes() : QVariantList{}) {
            const QVariantMap m = p.toMap();
            remotos.insert(m.value("id").toString(), m);
            esperando = esperando || m.value("estado").toString() == "esperando";
        }
        ctx["remotos"] = remotos;
    }
    {
        // La foto de cada posición PTZ ("cámara/posición", también por su nombre).
        QVariantMap fotos;
        for (const QVariant& c : f_.camaras ? f_.camaras() : QVariantList{}) {
            const QVariantMap cm = c.toMap();
            for (const QVariant& p : cm.value("posiciones").toList()) {
                const QVariantMap pm = p.toMap();
                const QString url = pm.value("miniatura_url").toString();
                if (url.isEmpty()) continue;
                fotos.insert(cm.value("entrada").toString() + "/" + pm.value("id").toString(), url);
                if (!pm.value("nombre").toString().isEmpty()) fotos.insert(cm.value("entrada").toString() + "/" + pm.value("nombre").toString(), url);
            }
        }
        ctx["fotosPtz"] = fotos;
    }

    QVariantList filas;
    QVariantMap porLugar;
    for (const QVariant& lv : r.value("bancas").toList()) {
        const QVariantMap l = fila(lv.toMap(), ctx);
        filas << l;
        porLugar.insert(QString::number(l.value("fila").toInt()) + "," + QString::number(l.value("columna").toInt()), l);
    }
    // Por número de banca (la lista; en la grilla y el hemiciclo cada una va a su lugar).
    QCollator nat;
    nat.setNumericMode(true);
    std::stable_sort(filas.begin(), filas.end(), [&nat](const QVariant& a, const QVariant& b) {
        return nat.compare(a.toMap().value("banca").toString(), b.toMap().value("banca").toString()) < 0;
    });
    bancas_.poner(filas);
    const QVariant pres = r.value("presidencia");
    const QVariantMap presidencia = pres.isValid() && !pres.toMap().isEmpty() ? fila(pres.toMap(), ctx) : QVariantMap{};
    if (porLugar != porLugar_ || presidencia != presidencia_ || esperando != hayEsperando_) {
        porLugar_ = porLugar;
        presidencia_ = presidencia;
        hayEsperando_ = esperando;
        emit cambio();
    }
}

} // namespace ba::app::asamblea

#include "moc_RecintoModel.cpp"
