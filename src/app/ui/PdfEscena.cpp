// src/app/ui/PdfEscena.cpp — ver PdfEscena.hpp
#include "ui/PdfEscena.hpp"

#include "arranque/AppRuntime.hpp"
#include "emision/FuentePdf.hpp"
#include "emision/FuentesApp.hpp"
#include "evento/Documentos.hpp"

#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QPdfDocument>
#include <QPdfDocumentRenderOptions>
#include <QUrl>

#include <algorithm>
#include <functional>
#include <set>

namespace ba::app {

namespace {
QString qp(const std::filesystem::path& p) { return QString::fromStdU16String(p.u16string()); }
/// Color guardado como 0xAABBGGRR (como los guardaba OBS).
QColor colorDe(std::int64_t v) {
    const auto u = static_cast<std::uint32_t>(v);
    return QColor(static_cast<int>(u & 0xFF), static_cast<int>((u >> 8) & 0xFF), static_cast<int>((u >> 16) & 0xFF),
                  static_cast<int>((u >> 24) & 0xFF));
}
} // namespace

PdfEscena::PdfEscena(AppRuntime& rt, QObject* parent) : QObject(parent), rt_(rt) {
    refrescarDocumentos();
    // La lista «Qué PDF» de Propiedades: la documentación del evento.
    fuentespdf::ponerOpciones([this]() {
        json out = json::array();
        for (const QVariant& v : documentos_) {
            const QVariantMap m = v.toMap();
            out.push_back({{"value", m.value("id").toString().toStdString()}, {"label", m.value("titulo").toString().toStdString()}});
        }
        return out;
    });
    connect(&timer_, &QTimer::timeout, this, &PdfEscena::tic);
    timer_.start(150); // pasar de página tiene que sentirse inmediato
}

PdfEscena::~PdfEscena() { fuentespdf::ponerOpciones({}); }

void PdfEscena::refrescarDocumentos() {
    QVariantList l;
    const evento::Documentos d(rt_.dataRoot());
    for (const auto& x : d.lista()) {
        const std::string archivo = x.value("archivo", std::string());
        if (QFileInfo(QString::fromStdString(archivo)).suffix().toLower() != "pdf") continue;
        const std::string id = x.value("id", std::string());
        l << QVariantMap{{"id", QString::fromStdString(id)},
                         {"titulo", QString::fromStdString(x.value("titulo", archivo))},
                         {"paginas", x.value("paginas", 0)},
                         {"url_miniatura", x.value("miniatura", false) ? QUrl::fromLocalFile(qp(d.miniatura(id))).toString() : QString()}};
    }
    documentos_ = l;
    emit documentosCambio();
}

QVariantList PdfEscena::documentos() const { return documentos_; }

QString PdfEscena::rutaDe(const json& a) const {
    const std::string id = a.value("documento", std::string());
    if (!id.empty()) return qp(evento::Documentos(rt_.dataRoot()).ruta(id));
    QString r = QString::fromStdString(a.value("archivo", std::string()));
    if (r.startsWith("file:")) r = QUrl(r).toLocalFile();
    return r;
}

QPdfDocument* PdfEscena::abrir(const QString& ruta) {
    if (ruta.isEmpty() || !QFileInfo::exists(ruta)) return nullptr;
    auto it = docs_.find(ruta);
    if (it == docs_.end()) {
        auto d = std::make_unique<QPdfDocument>();
        if (d->load(ruta) != QPdfDocument::Error::None) return nullptr;
        it = docs_.emplace(ruta, std::move(d)).first;
    }
    return it->second.get();
}

int PdfEscena::paginasDe(const QString& ruta) const {
    const auto it = docs_.find(ruta);
    return it == docs_.end() ? 0 : it->second->pageCount();
}

QVariantList PdfEscena::fuentes() const {
    QVariantList out;
    // Guardado primero: recorrer `estado()["fuentes"]` directo recorría un temporal
    // ya destruido y la tarjeta veía la lista vacía («no hay ninguna fuente PDF»).
    const json e = estado();
    for (const auto& f : e["fuentes"]) {
        QVariantMap m;
        for (const auto& [k, x] : f.items())
            m[QString::fromStdString(k)] = x.is_number_integer() ? QVariant(x.get<int>())
                                         : x.is_number()         ? QVariant(x.get<double>())
                                         : x.is_string()         ? QVariant(QString::fromStdString(x.get<std::string>()))
                                                                 : QVariant();
        out << m;
    }
    return out;
}

PdfEscena::json PdfEscena::estado() const {
    json fs = json::array();
    for (const auto& f : fuentes::de(fuentespdf::kTipo)) {
        const QString ruta = rutaDe(f.ajustes);
        const std::string id = f.ajustes.value("documento", std::string());
        std::string titulo = ruta.isEmpty() ? std::string() : QFileInfo(ruta).completeBaseName().toStdString();
        for (const QVariant& v : documentos_)
            if (v.toMap().value("id").toString().toStdString() == id && !id.empty())
                titulo = v.toMap().value("titulo").toString().toStdString();
        const int paginas = paginasDe(ruta);
        json o{{"nombre", f.nombre},
               {"documento", id},
               {"archivo", ruta.toStdString()},
               {"titulo", titulo},
               {"pagina", std::max(1, f.ajustes.value("pagina", 1))},
               {"paginas", paginas},
               {"zoom", f.ajustes.value("zoom", 1.0)},
               {"x", f.ajustes.value("x", 0.5)},
               {"y", f.ajustes.value("y", 0.5)}};
        if (ruta.isEmpty()) o["error"] = "no tiene ningún PDF elegido";
        else if (!QFileInfo::exists(ruta)) o["error"] = "no está el archivo";
        fs.push_back(o);
    }
    json docs = json::array();
    for (const QVariant& v : documentos_) {
        const QVariantMap m = v.toMap();
        docs.push_back({{"id", m.value("id").toString().toStdString()},
                        {"titulo", m.value("titulo").toString().toStdString()},
                        {"paginas", m.value("paginas").toInt()}});
    }
    return {{"fuentes", fs}, {"documentos", docs}};
}

std::string PdfEscena::resolver(const QString& fuente, QString* error) const {
    const auto lista = fuentes::de(fuentespdf::kTipo);
    for (const auto& f : lista)
        if (fuente.isEmpty() || QString::fromStdString(f.nombre) == fuente) return f.nombre;
    if (error != nullptr)
        *error = lista.empty() ? "no hay ninguna fuente PDF en las escenas (armá la escena «PDF»)"
                               : "no hay una fuente PDF llamada «" + fuente + "»";
    return {};
}

QString PdfEscena::elegir(const QString& fuente, const QString& id) {
    QString err;
    const std::string n = resolver(fuente, &err);
    if (n.empty()) return err;
    if (evento::Documentos(rt_.dataRoot()).ruta(id.toStdString()).empty()) return "no hay un documento «" + id + "»";
    fuentes::cambiarAjustes(n, json{{"documento", id.toStdString()}, {"archivo", ""}, {"pagina", 1}, {"zoom", 1.0}, {"x", 0.5}, {"y", 0.5}});
    tic();
    return {};
}

QString PdfEscena::elegirArchivo(const QString& fuente, const QString& ruta) {
    QString err;
    const std::string n = resolver(fuente, &err);
    if (n.empty()) return err;
    const QString r = ruta.startsWith("file:") ? QUrl(ruta).toLocalFile() : ruta;
    if (!QFileInfo::exists(r)) return "no existe " + r;
    if (abrir(r) == nullptr) return tr("no es un PDF que se pueda abrir");
    fuentes::cambiarAjustes(n, json{{"documento", ""}, {"archivo", r.toStdString()}, {"pagina", 1}, {"zoom", 1.0}, {"x", 0.5}, {"y", 0.5}});
    tic();
    return {};
}

QString PdfEscena::irA(const QString& fuente, int pagina) {
    QString err;
    const std::string n = resolver(fuente, &err);
    if (n.empty()) return err;
    for (const auto& f : fuentes::de(fuentespdf::kTipo))
        if (f.nombre == n) {
            const QString ruta = rutaDe(f.ajustes);
            if (abrir(ruta) == nullptr) return tr("esa fuente no tiene un PDF que se pueda abrir");
            const int total = std::max(1, paginasDe(ruta));
            fuentes::cambiarAjustes(n, json{{"pagina", std::clamp(pagina, 1, total)}});
            tic();
            return {};
        }
    return tr("no está la fuente");
}

QString PdfEscena::paso(const QString& fuente, int delta) {
    QString err;
    const std::string n = resolver(fuente, &err);
    if (n.empty()) return err;
    for (const auto& f : fuentes::de(fuentespdf::kTipo))
        if (f.nombre == n) return irA(QString::fromStdString(n), std::max(1, f.ajustes.value("pagina", 1)) + delta);
    return tr("no está la fuente");
}

PdfEscena::Geometria PdfEscena::geometria(const json& a) {
    Geometria g;
    const int ancho = std::clamp(a.value("ancho", 1920), 320, 7680), alto = std::clamp(a.value("alto", 1080), 240, 4320);
    QPdfDocument* d = abrir(rutaDe(a));
    if (d == nullptr || d->pageCount() == 0) return g;
    const int pagina = std::clamp(a.value("pagina", 1), 1, d->pageCount());
    const QSizeF pt = d->pagePointSize(pagina - 1);
    const QSize base = pt.isValid() && pt.width() > 0 ? pt.toSize().scaled(ancho, alto, Qt::KeepAspectRatio) : QSize(ancho, alto);
    const double z = std::clamp(a.value("zoom", 1.0), 1.0, 8.0);
    g.W = base.width() * z;
    g.H = base.height() * z;
    g.vw = std::min<double>(ancho, g.W);
    g.vh = std::min<double>(alto, g.H);
    return g;
}

QString PdfEscena::encuadre(const QString& fuente, double zoom, double x, double y) {
    QString err;
    const std::string n = resolver(fuente, &err);
    if (n.empty()) return err;
    json c = json::object();
    if (zoom >= 0) c["zoom"] = std::clamp(zoom, 1.0, 8.0);
    if (x >= 0) c["x"] = std::clamp(x, 0.0, 1.0);
    if (y >= 0) c["y"] = std::clamp(y, 0.0, 1.0);
    if (c.empty()) return {};
    fuentes::cambiarAjustes(n, c);
    tic();
    return {};
}

QString PdfEscena::mover(const QString& fuente, double dx, double dy) {
    QString err;
    const std::string n = resolver(fuente, &err);
    if (n.empty()) return err;
    for (const auto& f : fuentes::de(fuentespdf::kTipo))
        if (f.nombre == n) {
            const Geometria g = geometria(f.ajustes);
            if (g.W <= 0) return tr("esa fuente no tiene un PDF que se pueda abrir");
            // El centro, acotado para que lo visible no se salga de la página.
            const auto acotar = [](double v, double visible, double total) {
                if (visible >= total) return 0.5;
                const double m = visible / 2 / total;
                return std::clamp(v, m, 1 - m);
            };
            const double x = acotar(f.ajustes.value("x", 0.5) + dx * g.vw / g.W, g.vw, g.W);
            const double y = acotar(f.ajustes.value("y", 0.5) + dy * g.vh / g.H, g.vh, g.H);
            fuentes::cambiarAjustes(n, json{{"x", x}, {"y", y}});
            tic();
            return {};
        }
    return tr("no está la fuente");
}

QString PdfEscena::crearEscena(const QString& nombre) {
    const std::string esc = nombre.trimmed().isEmpty() ? std::string("PDF") : nombre.trimmed().toStdString();
    const auto escenas = rt_.engine().sceneNames();
    if (std::find(escenas.begin(), escenas.end(), esc) == escenas.end() && !rt_.addScene(esc))
        return QString::fromStdString(rt_.lastError().empty() ? "no se pudo crear la escena" : rt_.lastError());
    // Ya tiene una fuente PDF: listo.
    const auto ss = rt_.engine().sceneSources(esc);
    for (const auto& f : fuentes::de(fuentespdf::kTipo))
        if (std::find(ss.begin(), ss.end(), f.nombre) != ss.end()) return {};
    SourceSpec s;
    s.id = fuentespdf::kTipo;
    // Un nombre que no esté (las fuentes tienen nombre único).
    std::string fn = "PDF";
    for (int i = 2; std::find(ss.begin(), ss.end(), fn) != ss.end() || !rt_.engine().sourceTypeId(fn).empty(); ++i)
        fn = "PDF " + std::to_string(i);
    s.name = fn;
    s.settingsJson = json{{"documento", documentos_.isEmpty() ? std::string() : documentos_.first().toMap().value("id").toString().toStdString()},
                          {"pagina", 1}}
                         .dump();
    s.rect = CanvasRect{0, 0, 1, 1};
    if (!rt_.addSource(esc, s))
        return QString::fromStdString(rt_.lastError().empty() ? "no se pudo agregar la fuente PDF" : rt_.lastError());
    tic();
    return {};
}

void PdfEscena::tic() {
    std::set<std::uint64_t> vivas;
    std::size_t firmaLista = 0;
    for (const auto& f : fuentes::de(fuentespdf::kTipo)) {
        vivas.insert(f.id);
        const QString ruta = rutaDe(f.ajustes);
        QPdfDocument* d = abrir(ruta);
        const int ancho = std::clamp(f.ajustes.value("ancho", 1920), 320, 7680);
        const int alto = std::clamp(f.ajustes.value("alto", 1080), 240, 4320);
        const std::int64_t fondo = f.ajustes.value("fondo", static_cast<std::int64_t>(0xFF000000));
        const int total = d ? d->pageCount() : 0;
        const int pagina = total > 0 ? std::clamp(f.ajustes.value("pagina", 1), 1, total) : 1;
        const double zoom = std::clamp(f.ajustes.value("zoom", 1.0), 1.0, 8.0);
        const double cx = std::clamp(f.ajustes.value("x", 0.5), 0.0, 1.0), cy = std::clamp(f.ajustes.value("y", 0.5), 0.0, 1.0);
        const std::size_t firma = std::hash<std::string>{}(ruta.toStdString() + "|" + std::to_string(zoom) + "|" +
                                                           std::to_string(cx) + "|" + std::to_string(cy)) ^
                                  (static_cast<std::size_t>(pagina) << 1) ^
                                  (static_cast<std::size_t>(ancho) << 20) ^ (static_cast<std::size_t>(alto) << 34) ^
                                  static_cast<std::size_t>(fondo) ^ (d ? 0x5bd1e995u : 0u) ^ (static_cast<std::size_t>(f.version) << 44);
        firmaLista ^= firma + 0x9e3779b9u + (firmaLista << 6) + std::hash<std::string>{}(f.nombre);
        if (firmas_[f.id] == firma) continue;
        firmas_[f.id] = firma;
        // La página entera, lo más grande que entre sin deformarse, centrada en el fondo.
        QImage lienzo(ancho, alto, QImage::Format_ARGB32);
        lienzo.fill(colorDe(fondo));
        if (d != nullptr && total > 0) {
            // Con zoom: la página escalada (W×H) y, de ella, SOLO lo visible (pdfium dibuja
            // ese recorte a esa escala: nítido, sin dibujar la página entera grande).
            const QSizeF pt = d->pagePointSize(pagina - 1);
            const QSize base = pt.isValid() && pt.width() > 0 ? pt.toSize().scaled(ancho, alto, Qt::KeepAspectRatio) : QSize(ancho, alto);
            const QSize escalada(qRound(base.width() * zoom), qRound(base.height() * zoom));
            const int vw = std::min(ancho, escalada.width()), vh = std::min(alto, escalada.height());
            const int izq = std::clamp(qRound(cx * escalada.width() - vw / 2.0), 0, escalada.width() - vw);
            const int arr = std::clamp(qRound(cy * escalada.height() - vh / 2.0), 0, escalada.height() - vh);
            QPdfDocumentRenderOptions o;
            o.setScaledSize(escalada);
            o.setScaledClipRect(QRect(izq, arr, vw, vh));
            const QImage pag = d->render(pagina - 1, QSize(vw, vh), o);
            QPainter p(&lienzo);
            // La hoja en blanco (las páginas sin fondo vienen transparentes).
            const QRect r((ancho - vw) / 2, (alto - vh) / 2, vw, vh);
            p.fillRect(r, Qt::white);
            p.drawImage(r, pag);
        }
        fuentes::empujar(f.id, lienzo.constBits(), lienzo.width(), lienzo.height(), static_cast<int>(lienzo.bytesPerLine()));
    }
    for (auto it = firmas_.begin(); it != firmas_.end();) it = vivas.count(it->first) ? std::next(it) : firmas_.erase(it);
    if (firmaLista != firmaLista_) {
        firmaLista_ = firmaLista;
        emit cambio();
    }
}

} // namespace ba::app
