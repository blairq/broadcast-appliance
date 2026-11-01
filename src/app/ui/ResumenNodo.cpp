// src/app/ui/ResumenNodo.cpp — ver el .hpp

#include "ui/ResumenNodo.hpp"

#include <QCoreApplication>
#include <QStringList>

namespace ba::app {

using json = nlohmann::json;

namespace {

QString q(const std::string& s) { return QString::fromStdString(s); }

// Los textos son los que estaban en la página (mismo contexto: VideoPage), así siguen
// con sus traducciones.
QString t(const char* s) { return QCoreApplication::translate("VideoPage", s); }
QString tn(const char* s, int n) { return QCoreApplication::translate("VideoPage", s, "", n); }
QString num(double v, int dec = 2) { return QString::number(v, 'f', dec); }
QString texto(const json& j) {
    if (j.is_string()) return q(j.get<std::string>());
    if (j.is_number_integer()) return QString::number(j.get<long long>());
    if (j.is_number()) {
        QString s = QString::number(j.get<double>(), 'f', 2);
        while (s.contains('.') && (s.endsWith('0') || s.endsWith('.'))) s.chop(1);
        return s;
    }
    return j.is_null() ? QString() : q(j.dump());
}
// Lo que se lee de un campo, del tipo que se espera o el de respaldo: el grafo viene de
// disco y de versiones anteriores, y value<T>() con otro tipo tira (a42d4a31).
const json& campo(const json& o, const char* k) {
    static const json nulo;
    if (!o.is_object()) return nulo;
    const auto it = o.find(k);
    return it != o.end() ? *it : nulo;
}
double dbl(const json& p, const char* k, double d = 0) {
    const json& v = campo(p, k);
    return v.is_number() ? v.get<double>() : d;
}
bool si(const json& o, const char* k, bool d) {
    const json& v = campo(o, k);
    return v.is_boolean() ? v.get<bool>() : d;
}
std::string txt(const json& o, const char* k) {
    const json& v = campo(o, k);
    return v.is_string() ? v.get<std::string>() : std::string();
}
QString texto(const json& o, const char* k, const char* d = "") {
    const json& v = campo(o, k);
    return v.is_null() ? QString::fromUtf8(d) : texto(v);
}
/// El objeto (o la lista) de un campo; vacío si es otra cosa.
json obj(const json& o, const char* k) {
    const json& v = campo(o, k);
    return v.is_object() ? v : json::object();
}
json lista(const json& o, const char* k) {
    const json& v = campo(o, k);
    return v.is_array() ? v : json::array();
}

} // namespace

/// Lo que se lee de un nodo en su tarjeta (abajo del nombre): qué hace, con lo principal
/// de sus ajustes. `conectadas`: los cables que le llegan (para una mezcla).
QString resumenDeNodo(const json& n, int conectadas) {
    const json p = obj(n, "params");
    const std::string tipo = txt(n, "tipo");
    if (const QString e = texto(n, "error"); !e.isEmpty()) return "⚠ " + e;
    const QString enEscena = si(n, "publicada", false) ? t(" · en escena") : QString();
    if (tipo == "entrada") return t("La cámara (ya encuadrada por el PTZ)");
    if (tipo == "escena") return t("Lo que ve la escena");
    if (tipo == "obs") return t("Filtro de OBS · %1").arg(texto(p, "filtro"));
    if (tipo == "control") {
        const std::string tarjeta = txt(p, "tarjeta");
        const json pp = obj(p, "params");
        return t("Controlador · %1").arg(tarjeta == "gst" ? texto(pp, "elemento") : tarjeta == "obs" ? texto(pp, "filtro") : q(tarjeta));
    }
    // `controlador` puede ser true o el id del controlador (texto).
    const json& ctl = campo(n, "controlador");
    if ((ctl.is_boolean() && ctl.get<bool>()) || (ctl.is_string() && !ctl.get<std::string>().empty()))
        return obj(p, "local").empty() ? t("↳ del controlador") : t("↳ del controlador · con ajuste propio");
    if (tipo == "gst") {
        const json pr = obj(p, "propiedades");
        QStringList ks;
        for (const auto& [k, v] : pr.items()) {
            if (ks.size() == 2) break;
            ks << q(k) + "=" + texto(v);
        }
        return "GStreamer · " + texto(p, "elemento") + (ks.isEmpty() ? QString() : " · " + ks.join(' '));
    }
    if (tipo == "compositor") {
        const int ent = static_cast<int>(lista(n, "entradas").size()) - 1;
        return tn("%n entrada · %1", ent).arg(dbl(p, "ancho") > 0 ? texto(p, "ancho") + "×" + texto(p, "alto", "0") : t("lienzo del estudio")) + enEscena;
    }
    if (tipo == "mezcla") return tn("GStreamer · %1 · %n conectada", conectadas).arg(texto(p, "elemento")) + enEscena;
    if (tipo == "color")
        return t("brillo %1 · contraste %2 · saturación %3").arg(num(dbl(p, "brillo")), num(dbl(p, "contraste")), num(dbl(p, "saturacion")));
    if (tipo == "croma")
        return t("fondo %1 · similitud %2").arg("#" + QString::number(static_cast<qint64>(dbl(p, "clave")), 16).rightJustified(6, '0').right(6).toUpper(), num(dbl(p, "similitud")));
    if (tipo == "recorte") {
        QStringList partes;
        if (dbl(p, "izquierda") > 0 || dbl(p, "arriba") > 0 || dbl(p, "derecha") > 0 || dbl(p, "abajo") > 0) partes << t("bordes recortados");
        if (si(p, "espejoH", false)) partes << t("espejo ↔");
        if (si(p, "espejoV", false)) partes << t("espejo ↕");
        return partes.isEmpty() ? t("sin cambios") : partes.join(" · ");
    }
    if (tipo == "vista360" || tipo == "panoramica") {
        const std::string ver = txt(p, "ver");
        if (ver == "entrada") return t("calibrando: la entrada con los círculos");
        if (ver == "tira")
            return (dbl(p, "ancho") > 0 ? t("la tira %1×%2").arg(texto(p, "ancho"), texto(p, "alto", "0")) : t("la tira (la suma de las cámaras)")) + enEscena;
        if (ver == "panorama") {
            const std::string guia = txt(p, "guia");
            return !guia.empty() && guia != "ninguna" ? t("el panorama 360 · guía %1").arg(q(guia)) : t("el panorama 360");
        }
        const QString mira = t("mira %1° · %2° · abre %3°").arg(qRound(dbl(p, "yaw"))).arg(qRound(dbl(p, "pitch"))).arg(qRound(dbl(p, "fov", 90)));
        if (tipo != "panoramica") return mira + enEscena;
        int camaras = 0;
        for (const auto& e : lista(n, "entradas"))
            if (!si(e, "pedido", false)) ++camaras;
        return tn("%n cámara · %1", camaras).arg(mira) + enEscena;
    }
    if (tipo == "salida") return t("«%1»").arg(texto(p, "nombre")) + enEscena;
    if (tipo == "retardo") return tn("%n cuadro de atraso", static_cast<int>(dbl(p, "cuadros")));
    if (tipo == "ptz") return !si(p, "activo", true) ? t("Apagado") : t("Pan, tilt y zoom digital (Cámaras PTZ)");
    if (tipo == "seguir") {
        if (!si(p, "activo", true)) return t("Apagado");
        const QString obj = texto(p, "objetivo");
        return !obj.isEmpty() ? t("Sigue: %1").arg(obj) : t("Sin nadie elegido");
    }
    if (tipo == "rostros") {
        const std::string uso = txt(p, "uso");
        QString s = uso == "mirar" ? t("Personas mira") : uso == "autoalta" ? t("Personas anota sola") : uso == "puerta" ? t("Puerta: acredita")
                    : uso == "seguir" ? t("Sigue a una cara") : t("Caras");
        if (si(p, "marcar", false)) s += " · " + (campo(p, "estilo").is_string() ? q(txt(p, "estilo")) : QStringLiteral("recuadro"));
        if (const QString tx = texto(p, "texto"); !tx.isEmpty()) s += " · «" + tx + "»";
        return s;
    }
    return {};
}

/// El grafo (o el estudio) con el `resumen` de cada nodo, para la vista.
json conResumenes(json g) {
    if (!g.is_object() || !g.contains("nodos") || !g["nodos"].is_array()) return g;
    const json cables = lista(g, "cables");
    for (json& n : g["nodos"]) {
        if (!n.is_object()) continue;
        int conectadas = 0;
        for (const auto& c : cables)
            if (c.is_object() && campo(c, "hacia") == campo(n, "id")) ++conectadas;
        n["resumen"] = resumenDeNodo(n, conectadas).toStdString();
    }
    return g;
}

} // namespace ba::app
