// src/app/agent/Subtitulos.cpp — ver Subtitulos.hpp

#include "agent/Subtitulos.hpp"

#include "emision/FuentesApp.hpp"
#include "agent/FlujosTexto.hpp"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace ba::app::agent {

namespace {

using json = nlohmann::json;
constexpr const char* kTipo = "ba_subtitulos";
constexpr int kMaxLineas = 4;
constexpr int kTicMs = 100;

Subtitulos* gActivo = nullptr;

/// Largo en caracteres (UTF-8: cuenta los que no son de continuación).
size_t largo(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s)
        n += (c & 0xC0) != 0x80;
    return n;
}

/// Palabras separadas por espacios; el chino (sin espacios) se corta por caracteres.
std::vector<std::string> palabras(const std::string& s) {
    std::vector<std::string> out;
    std::string w;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
        const std::string ch = s.substr(i, n);
        i += n;
        if (ch == " " || ch == "\n" || ch == "\t") {
            if (!w.empty()) out.push_back(w), w.clear();
        } else if (n == 3 && c >= 0xE3 && c <= 0xE9) {
            // CJK: cada ideograma es una «palabra» (se puede cortar entre ellos).
            if (!w.empty()) out.push_back(w), w.clear();
            out.push_back(ch);
        } else {
            w += ch;
        }
    }
    if (!w.empty()) out.push_back(w);
    return out;
}

std::vector<std::vector<std::string>> bloques(const std::string& texto, size_t caracteres, size_t nLineas) {
    std::vector<std::string> lineas;
    std::string l;
    for (const auto& p : palabras(texto)) {
        const bool cjk = largo(p) == 1 && p.size() == 3;
        const std::string sep = l.empty() || cjk ? "" : " ";
        if (!l.empty() && largo(l) + largo(sep) + largo(p) > caracteres) {
            lineas.push_back(l);
            l.clear();
        }
        l += (l.empty() ? "" : sep) + p;
    }
    if (!l.empty()) lineas.push_back(l);
    std::vector<std::vector<std::string>> out;
    for (size_t i = 0; i < lineas.size(); i += nLineas)
        out.emplace_back(lineas.begin() + static_cast<long>(i),
                         lineas.begin() + static_cast<long>(std::min(lineas.size(), i + nLineas)));
    return out;
}

double duracion(const std::vector<std::string>& b, double cps, double minimo) {
    size_t n = 0;
    for (const auto& l : b)
        n += largo(l);
    return std::max(minimo, static_cast<double>(n) / std::max(1.0, cps));
}

int nLineas(const json& a) { return std::clamp(a.value("lineas", 2), 1, kMaxLineas); }
int caracteres(const json& a) { return std::clamp(a.value("caracteres", 42), 10, 120); }
int tamano(const json& a) {
    const json f = a.value("font", json::object());
    return std::max(8, f.is_object() ? f.value("size", 56) : 56);
}

/// Color de los ajustes (0xAABBGGRR, como OBS).
QColor color(const json& a, const char* k, std::uint32_t def) {
    const auto it = a.find(k);
    const auto c = it != a.end() && it->is_number() ? static_cast<std::uint32_t>(it->get<std::int64_t>()) : def;
    return QColor(static_cast<int>(c & 0xFF), static_cast<int>((c >> 8) & 0xFF), static_cast<int>((c >> 16) & 0xFF),
                  static_cast<int>((c >> 24) & 0xFF));
}

QImage dibujar(const json& a, const std::vector<std::string>& actual) {
    const int tam = tamano(a);
    const bool fondo = a.value("fondo", true);
    const int altoLinea = static_cast<int>(tam * 1.35) + (fondo ? tam / 4 : 0);
    const int W = std::clamp(a.value("ancho", 1600), 200, 7680) & ~1;
    const int n = nLineas(a);
    const int H = (n * altoLinea + 1) & ~1;
    QImage img(W, H, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    if (!actual.empty()) {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);
        const json f = a.value("font", json::object());
        QFont letra(QString::fromStdString(f.is_object() ? f.value("face", std::string("Helvetica")) : "Helvetica"));
        letra.setPixelSize(tam);
        const int flags = f.is_object() ? f.value("flags", 0) : 0;
        letra.setBold((flags & 1) != 0);
        letra.setItalic((flags & 2) != 0);
        p.setFont(letra);
        const QFontMetrics fm(letra);
        const QColor texto = color(a, "color", 0xFFFFFFFF);
        const QColor caja = color(a, "color_fondo", 0xB4000000);
        const bool contorno = a.value("contorno", false), sombra = a.value("sombra", true) && !fondo;
        const double padX = tam * 0.35, padY = tam * 0.08;
        const int m = static_cast<int>(actual.size());
        for (int i = 0; i < m; ++i) {
            const QString l = QString::fromStdString(actual[static_cast<size_t>(i)]);
            const double w = fm.horizontalAdvance(l), h = fm.height();
            // Abajo de todo la última línea: el bloque se apoya en el borde inferior.
            const double y = (n - m + i) * altoLinea + (altoLinea - h) / 2.0;
            const double x = (W - w) / 2.0;
            if (fondo) p.fillRect(QRectF(x - padX, y - padY, w + 2 * padX, h + 2 * padY), caja);
            const QPointF base(x, y + fm.ascent());
            if (sombra) {
                p.setPen(QColor(0, 0, 0, 200));
                p.drawText(base + QPointF(tam / 18.0 + 1, tam / 18.0 + 1), l);
            }
            if (contorno) {
                QPainterPath path;
                path.addText(base, letra, l);
                p.strokePath(path, QPen(QColor(0, 0, 0, 255), std::max(2.0, tam / 14.0)));
            }
            p.setPen(texto);
            p.drawText(base, l);
        }
    }
    return img.convertToFormat(QImage::Format_ARGB32); // al motor: alfa recto
}

} // namespace

Subtitulos::Subtitulos(FlujosTexto& flujos, QObject* parent) : QObject(parent), flujos_(flujos) {
    gActivo = this;
    fuentes::Tipo t;
    t.id = kTipo;
    t.nombre = "Subtítulos";
    t.deApp = true;
    t.defectos = {{"lineas", 2},      {"caracteres", 42},   {"ancho", 1600},           {"minimo", 1.5},
                  {"cps", 17},        {"borrar", 4},        {"fondo", true},           {"contorno", false},
                  {"sombra", true},   {"color", 0xFFFFFFFF}, {"color_fondo", 0xB4000000},
                  {"font", {{"face", "Helvetica"}, {"size", 56}}}, {"flujo", ""}};
    t.props = [this](const json&) {
        using P = fuentes::Prop;
        P fl{"flujo", "Qué muestra (salida de texto)", "list"};
        fl.ayuda = "La transcripción de una «Voz a texto» o una traducción (Audio → Traducción).";
        for (const QVariant& v : flujos_.flujos()) {
            const std::string n = v.toMap().value("nombre").toString().toStdString();
            if (!n.empty()) fl.opciones.push_back({{"value", n}, {"label", n}});
        }
        return std::vector<P>{fl,
                              {"font", "Letra", "font"},
                              {"color", "Color del texto", "color"},
                              {"fondo", "Fondo detrás de cada línea", "bool"},
                              {"color_fondo", "Color del fondo", "color"},
                              {"contorno", "Contorno", "bool"},
                              {"sombra", "Sombra", "bool"},
                              {"lineas", "Líneas a la vez", "int", 1, kMaxLineas, 1},
                              {"caracteres", "Caracteres por línea (máximo)", "int", 10, 120, 1},
                              {"ancho", "Ancho del cuadro (px)", "int", 200, 7680, 10},
                              {"cps", "Velocidad de lectura (caracteres por segundo)", "float", 5, 40, 0.5},
                              {"minimo", "Tiempo mínimo en pantalla (s)", "float", 0.5, 10, 0.1},
                              {"borrar", "Se borra después de (s)", "float", 1, 60, 0.5},
                              {"limpiar", "Borrar ahora", "button"}};
    };
    t.botones["limpiar"] = [this](const std::string& fuente) { limpiar(fuente, {}); };
    fuentes::registrar(std::move(t));
    connect(&flujos_, &FlujosTexto::frase, this, &Subtitulos::alFrase);
    connect(&timer_, &QTimer::timeout, this, &Subtitulos::tic);
    timer_.start(kTicMs);
}

Subtitulos::~Subtitulos() {
    if (gActivo == this) gActivo = nullptr;
}

Subtitulos* Subtitulos::activo() { return gActivo; }

void Subtitulos::alFrase(const QString& flujo, const QString& texto) {
    const std::string fl = flujo.toStdString(), t = texto.toStdString();
    if (t.empty()) return;
    for (const auto& f : fuentes::de(kTipo)) {
        if (f.ajustes.value("flujo", std::string()) != fl) continue;
        Estado& e = estados_[f.id];
        for (auto& b : bloques(t, static_cast<size_t>(caracteres(f.ajustes)), static_cast<size_t>(nLineas(f.ajustes))))
            e.cola.push_back(std::move(b));
        // Si se atrasó mucho (alguien habla sin parar), se descarta lo más viejo.
        while (e.cola.size() > 12)
            e.cola.pop_front();
    }
}

int Subtitulos::cuadrosDe(const QString& flujo) const {
    int n = 0;
    for (const auto& f : fuentes::de(kTipo))
        n += f.ajustes.value("flujo", std::string()) == flujo.toStdString();
    return n;
}

int Subtitulos::limpiar(const std::string& nombre, const std::string& flujo) {
    int n = 0;
    for (const auto& f : fuentes::de(kTipo)) {
        if (!nombre.empty() && f.nombre != nombre) continue;
        if (!flujo.empty() && f.ajustes.value("flujo", std::string()) != flujo) continue;
        Estado& e = estados_[f.id];
        e.cola.clear();
        e.actual.clear();
        e.restante = 0;
        e.cambio = true;
        ++n;
    }
    return n;
}

void Subtitulos::tic() {
    const double seg = kTicMs / 1000.0;
    std::map<std::uint64_t, Estado> vivos;
    for (const auto& f : fuentes::de(kTipo)) {
        Estado e = std::move(estados_[f.id]);
        const json& a = f.ajustes;
        if (e.version != f.version) { // ajustes nuevos (o recién creada): redibujar
            e.version = f.version;
            e.cambio = true;
        }
        const double cps = a.value("cps", 17.0), minimo = a.value("minimo", 1.5), borrar = a.value("borrar", 4.0);
        if (!e.actual.empty()) {
            // Con bloques esperando, el que se ve se apura (hasta el doble).
            e.restante -= seg * (e.cola.empty() ? 1.0 : std::min(2.0, 1.0 + 0.35 * static_cast<double>(e.cola.size())));
            if (e.restante <= 0 && !e.cola.empty()) {
                e.actual = std::move(e.cola.front());
                e.cola.pop_front();
                e.restante = duracion(e.actual, cps, minimo);
                e.cambio = true;
            } else if (e.restante <= 0) {
                e.vacio += seg;
                if (e.vacio >= borrar) {
                    e.actual.clear();
                    e.cambio = true;
                }
            }
        } else if (!e.cola.empty()) {
            e.actual = std::move(e.cola.front());
            e.cola.pop_front();
            e.restante = duracion(e.actual, cps, minimo);
            e.vacio = 0;
            e.cambio = true;
        }
        if (e.restante > 0) e.vacio = 0;
        if (e.cambio) {
            e.cambio = false;
            const QImage img = dibujar(a, e.actual);
            fuentes::empujar(f.id, img.constBits(), img.width(), img.height(), static_cast<int>(img.bytesPerLine()));
        }
        vivos[f.id] = std::move(e);
    }
    estados_ = std::move(vivos);
}

} // namespace ba::app::agent
