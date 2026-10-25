// ============================================================================
// src/app/graficos/VideoEnGrafico.cpp — ver VideoEnGrafico.hpp
// ============================================================================
#include "graficos/VideoEnGrafico.hpp"

#include "Claves.hpp"

#include <algorithm>
#include <cmath>

#include <QJsonArray>
#include <QJsonValue>
#include <QRegularExpression>

namespace ba::app::graficos::video {
namespace {

using json = nlohmann::json;

/// La declaración: la de la descripción (lo que está en el editor, sin guardar) o la de la ficha.
QJsonArray declaracion(const QJsonObject& ficha, const QJsonObject& pedido) {
    const QJsonObject d = pedido.value("declarativo").toObject();
    if (d.value("escena").isArray()) return d.value("escena").toArray();
    const QJsonObject fd = ficha.value("declarativo").toObject();
    if (pedido.isEmpty() && fd.value("escena").isArray()) return fd.value("escena").toArray();
    return ficha.value("escena").toArray();
}

/// El tamaño al que está pensado el gráfico (el de su descripción, o el de la ficha).
void tamano(const QJsonObject& ficha, const QJsonObject& pedido, double& w, double& h) {
    QJsonObject d = pedido.value("declarativo").toObject();
    if (d.isEmpty()) d = ficha.value("declarativo").toObject();
    w = d.value("ancho").toDouble(ficha.value("ancho").toDouble(900));
    h = d.value("alto").toDouble(ficha.value("alto").toDouble(220));
    if (w <= 0) w = 900;
    if (h <= 0) h = 220;
}

/// «{camara}» → el valor de ese parámetro (como texto).
std::string resolver(const QString& fuente, const QJsonObject& parametros) {
    QString out = fuente;
    static const QRegularExpression re("\\{([A-Za-z0-9_.-]+)\\}");
    for (auto it = re.globalMatch(fuente); it.hasNext();) {
        const auto m = it.next();
        const QJsonValue v = parametros.value(m.captured(1));
        out.replace(m.captured(0), v.isString() ? v.toString() : v.isDouble() ? QString::number(v.toDouble()) : QString());
    }
    return out.trimmed().toStdString();
}

/// Las claves de una lista, cada una con lo que no dice heredado de la anterior (la primera,
/// de `previa`). En fracciones del gráfico.
std::vector<Clave> claves(const QJsonArray& a, Clave previa, double gw, double gh) {
    std::vector<Clave> out;
    for (const QJsonValue& v : a) {
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();
        Clave k = previa;
        k.ms = std::max(0, o.value("ms").toInt(out.empty() ? 0 : previa.ms));
        if (o.contains("x")) k.x = o.value("x").toDouble() / gw;
        if (o.contains("y")) k.y = o.value("y").toDouble() / gh;
        if (o.contains("ancho")) k.ancho = std::max(0.0, o.value("ancho").toDouble()) / gw;
        if (o.contains("alto")) k.alto = std::max(0.0, o.value("alto").toDouble()) / gh;
        if (o.contains("alfa")) k.alfa = std::clamp(o.value("alfa").toDouble(1.0), 0.0, 1.0);
        if (o.value("recorte").isObject()) {
            const QJsonObject r = o.value("recorte").toObject();
            const char* lados[4] = {"izq", "arr", "der", "aba"};
            for (int i = 0; i < 4; ++i)
                if (r.contains(lados[i])) k.recorte[i] = std::clamp(r.value(lados[i]).toDouble(), 0.0, 0.95);
        }
        k.suave = o.value("suave").toBool(true);
        out.push_back(k);
        previa = k;
    }
    std::stable_sort(out.begin(), out.end(), [](const Clave& x, const Clave& y) { return x.ms < y.ms; });
    return out;
}

} // namespace

std::vector<Capa> leer(const QJsonObject& ficha, const QJsonObject& pedido, QString* error, bool sinFuente) {
    std::vector<Capa> out;
    const QJsonArray decl = declaracion(ficha, pedido);
    if (decl.isEmpty()) return out;
    double gw = 900, gh = 220;
    tamano(ficha, pedido, gw, gh);
    // Los valores con los que se dibuja: los del pedido (ya con los por defecto), o los de la ficha.
    QJsonObject par = pedido.value("parametros").toObject();
    if (par.isEmpty()) {
        for (const QJsonValue& v : ficha.value("parametros").toArray()) {
            const QJsonObject p = v.toObject();
            if (p.contains("porDefecto")) par[p.value("clave").toString()] = p.value("porDefecto");
        }
    }
    for (const QJsonValue& v : decl) {
        if (!v.isObject()) {
            if (error != nullptr) *error = "cada capa de «escena» tiene que ser un objeto";
            continue;
        }
        const QJsonObject o = v.toObject();
        Capa c;
        c.fuente = resolver(o.value("fuente").toString(), par);
        if (c.fuente.empty() && !sinFuente) continue; // el parámetro vacío: el gráfico sale sin video
        c.arriba = o.value("donde").toString("arriba") != "debajo";
        c.propia = o.value("propia").toBool(false);
        const QString aj = o.value("ajuste").toString("cubrir");
        c.ajuste = aj == "contener" || aj == "estirar" ? aj.toStdString() : std::string("cubrir");
        // La primera clave arranca en todo el gráfico, visible.
        Clave base;
        base.ancho = 1;
        base.alto = 1;
        c.claves = claves(o.value("claves").toArray(), base, gw, gh);
        if (c.claves.empty()) c.claves.push_back(base);
        Clave ultima = c.claves.back();
        ultima.ms = 0;
        c.salida = claves(o.value("salida").toArray(), ultima, gw, gh);
        out.push_back(std::move(c));
    }
    return out;
}

bool tieneVideo(const QJsonObject& ficha, const QJsonObject& pedido) { return !declaracion(ficha, pedido).isEmpty(); }

QStringList parametrosDeFuente(const QJsonObject& ficha) {
    QStringList out;
    for (const QJsonValue& v : ficha.value("parametros").toArray())
        if (v.toObject().value("tipo").toString() == "fuente") out << v.toObject().value("clave").toString();
    return out;
}

std::vector<Clave> lineaDeTiempo(const Capa& c, int salidaEn, int salidaDur) {
    std::vector<Clave> out = c.claves;
    if (out.empty() || salidaEn <= 0) return out;
    // Quieta hasta que se va (si la entrada termina antes).
    Clave quieta = out.back();
    quieta.ms = std::max(salidaEn, quieta.ms);
    if (quieta.ms > out.back().ms) out.push_back(quieta);
    if (c.salida.empty()) {
        // No dice cómo se va: se funde con la salida del gráfico.
        Clave fin = quieta;
        fin.ms = quieta.ms + std::max(1, salidaDur);
        fin.alfa = 0;
        out.push_back(fin);
        return out;
    }
    for (Clave k : c.salida) {
        k.ms += quieta.ms;
        out.push_back(k);
    }
    return out;
}

Clave en(const std::vector<Clave>& k, double ms) {
    if (k.empty()) return {};
    const motor::Tramo tr = motor::tramoEn(k, ms);
    const Clave& a = k[tr.desde];
    const Clave& b = k[tr.hasta];
    Clave out = b;
    out.ms = static_cast<int>(std::lround(ms));
    out.x = tr.en(a.x, b.x);
    out.y = tr.en(a.y, b.y);
    out.ancho = tr.en(a.ancho, b.ancho);
    out.alto = tr.en(a.alto, b.alto);
    out.alfa = tr.en(a.alfa, b.alfa);
    for (int i = 0; i < 4; ++i) out.recorte[i] = tr.en(a.recorte[i], b.recorte[i]);
    return out;
}

std::vector<Clave> desde(const std::vector<Clave>& k, int ms) {
    if (ms <= 0 || k.empty()) return k;
    std::vector<Clave> out;
    Clave primera = en(k, ms);
    primera.ms = 0;
    out.push_back(primera);
    for (Clave c : k)
        if (c.ms > ms) {
            c.ms -= ms;
            out.push_back(c);
        }
    return out;
}

nlohmann::json aJson(const std::vector<Capa>& capas, int salidaEn, int salidaDur, int inicioMs) {
    json out = json::array();
    for (const Capa& c : capas) {
        json cl = json::array();
        for (const Clave& k : desde(lineaDeTiempo(c, salidaEn, salidaDur), inicioMs))
            cl.push_back({{"ms", k.ms},
                          {"x", k.x},
                          {"y", k.y},
                          {"ancho", k.ancho},
                          {"alto", k.alto},
                          {"alfa", k.alfa},
                          {"recorte", json::array({k.recorte[0], k.recorte[1], k.recorte[2], k.recorte[3]})},
                          {"suave", k.suave}});
        out.push_back({{"fuente", c.fuente}, {"arriba", c.arriba}, {"propia", c.propia}, {"ajuste", c.ajuste}, {"claves", cl}});
    }
    return out;
}

Caja dondeSeVe(const Caja& item, int imgAncho, int imgAlto) {
    if (imgAncho <= 0 || imgAlto <= 0 || item.ancho <= 0 || item.alto <= 0) return item;
    const double s = std::min(item.ancho / imgAncho, item.alto / imgAlto);
    const double w = imgAncho * s, h = imgAlto * s;
    return {item.x + (item.ancho - w) / 2, item.y + (item.alto - h) / 2, w, h};
}

nlohmann::json clavesEnLienzo(const nlohmann::json& capa, const Caja& g, int fuenteAncho, int fuenteAlto, bool* contener) {
    const std::string ajuste = capa.value("ajuste", std::string("cubrir"));
    const bool conTamano = fuenteAncho > 0 && fuenteAlto > 0;
    // Cubrir sin saber el tamaño de la fuente: contenida (no se deforma; la próxima vez que se
    // publique la escena ya va a tener tamaño).
    const bool cubrir = ajuste == "cubrir" && conTamano;
    if (contener != nullptr) *contener = ajuste == "contener" || (ajuste == "cubrir" && !conTamano);
    json out = json::array();
    for (const auto& k : capa.value("claves", json::array())) {
        const double w = std::max(2.0, k.value("ancho", 1.0) * g.ancho), h = std::max(2.0, k.value("alto", 1.0) * g.alto);
        double r[4] = {0, 0, 0, 0};
        if (k.contains("recorte") && k["recorte"].is_array() && k["recorte"].size() == 4)
            for (int i = 0; i < 4; ++i) r[i] = std::clamp(k["recorte"][static_cast<std::size_t>(i)].get<double>(), 0.0, 0.95);
        double izq = 0, arr = 0, der = 0, aba = 0;
        if (conTamano) {
            izq = r[0] * fuenteAncho;
            arr = r[1] * fuenteAlto;
            der = r[2] * fuenteAncho;
            aba = r[3] * fuenteAlto;
            if (cubrir) {
                // Lo que queda de la fuente, con la forma del recuadro: lo que sobra se recorta
                // parejo de los dos lados.
                const double sw = std::max(1.0, fuenteAncho - izq - der), sh = std::max(1.0, fuenteAlto - arr - aba);
                const double caja = w / h;
                if (sw / sh > caja) {
                    const double sobra = sw - sh * caja;
                    izq += sobra / 2;
                    der += sobra / 2;
                } else {
                    const double sobra = sh - sw / caja;
                    arr += sobra / 2;
                    aba += sobra / 2;
                }
            }
        }
        out.push_back({{"ms", k.value("ms", 0)},
                       {"x", g.x + k.value("x", 0.0) * g.ancho},
                       {"y", g.y + k.value("y", 0.0) * g.alto},
                       {"ancho", w},
                       {"alto", h},
                       {"alfa", k.value("alfa", 1.0)},
                       {"recorte", {{"izq", izq}, {"arr", arr}, {"der", der}, {"aba", aba}}},
                       {"suave", k.value("suave", true)}});
    }
    return out;
}

} // namespace ba::app::graficos::video
