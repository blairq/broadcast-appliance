// src/app/ui/DocumentosViewModel.cpp — ver DocumentosViewModel.hpp

#include "ui/DocumentosViewModel.hpp"

#include "evento/Documentos.hpp"

#include <QDateTime>
#include <QDesktopServices>
#include <QImage>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPdfDocument>
#include <QPdfSelection>
#include <QUrl>

#include <algorithm>

namespace ba::app {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {
QVariant aQv(const json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(json{{"x", j}}.dump(-1, ' ', false, json::error_handler_t::replace)))
        .object()
        .value("x")
        .toVariant();
}
json deQv(const QVariant& v) {
    return json::parse(QJsonDocument::fromVariant(QVariantMap{{"x", v}}).toJson().toStdString(), nullptr, false).value("x", json());
}
QString qp(const fs::path& p) { return QString::fromStdU16String(p.u16string()); }

constexpr int kAnchoMiniatura = 360;

struct Leido {
    int paginas = 0;
    std::string titulo, texto;
    bool miniatura = false;
};

/// Guarda la página `pagina` (desde 0) de `pdf` como carátula en `mini` (JPG).
bool tapa(QPdfDocument& pdf, int pagina, const fs::path& mini) {
    const QSizeF pt = pdf.pagePointSize(pagina);
    const int alto = pt.width() > 0 ? static_cast<int>(kAnchoMiniatura * pt.height() / pt.width()) : kAnchoMiniatura * 1414 / 1000;
    const QImage img = pdf.render(pagina, QSize(kAnchoMiniatura, alto));
    if (img.isNull()) return false;
    // Fondo blanco (las páginas sin fondo salen transparentes y en JPG, negras).
    QImage blanco(img.size(), QImage::Format_RGB32);
    blanco.fill(Qt::white);
    {
        QPainter pi(&blanco);
        pi.drawImage(0, 0, img);
    }
    return blanco.save(qp(mini), "JPG", 82);
}

/// Lee el PDF (o la imagen) y guarda su carátula (la página `paginaTapa`, desde 1) en `mini` (JPG).
Leido leer(const fs::path& archivo, const fs::path& mini, int paginaTapa) {
    Leido r;
    std::string ext = archivo.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::error_code ec;
    fs::create_directories(mini.parent_path(), ec);
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".webp" || ext == ".gif") {
        QImageReader ir(qp(archivo));
        ir.setAutoTransform(true);
        const QImage img = ir.read();
        r.paginas = 1;
        if (!img.isNull())
            r.miniatura = img.scaledToWidth(kAnchoMiniatura, Qt::SmoothTransformation).save(qp(mini), "JPG", 82);
        return r;
    }
    if (ext != ".pdf") return r;
    QPdfDocument pdf;
    if (pdf.load(qp(archivo)) != QPdfDocument::Error::None) return r;
    r.paginas = pdf.pageCount();
    r.titulo = pdf.metaData(QPdfDocument::MetaDataField::Title).toString().trimmed().toStdString();
    // El texto de las primeras páginas: para sugerir la descripción (y para el asistente).
    QString t;
    for (int p = 0; p < std::min(2, r.paginas) && t.size() < 1500; ++p) t += pdf.getAllText(p).text() + "\n";
    r.texto = t.simplified().left(1500).toStdString();
    if (r.paginas > 0) r.miniatura = tapa(pdf, std::clamp(paginaTapa, 1, r.paginas) - 1, mini);
    return r;
}
} // namespace

DocumentosViewModel::DocumentosViewModel(std::function<fs::path()> evento, QObject* parent)
    : QObject(parent), evento_(std::move(evento)) {}

fs::path DocumentosViewModel::carpeta() const { return evento::Documentos(evento_()).carpeta(); }

json DocumentosViewModel::lista() const { return evento::Documentos(evento_()).lista(); }

json DocumentosViewModel::documento(const std::string& id) const { return evento::Documentos(evento_()).documento(id); }

QVariantList DocumentosViewModel::listaUi() const {
    const evento::Documentos d(evento_());
    json l = d.lista();
    for (auto& x : l)
        if (x.value("miniatura", false))
            x["url_miniatura"] = QUrl::fromLocalFile(qp(d.miniatura(x.value("id", std::string())))).toString().toStdString() + "?" +
                                 std::to_string(x.value("agregado_ms", 0LL)) + "-" + std::to_string(x.value("pagina_tapa", 1));
    return aQv(l).toList();
}

json DocumentosViewModel::publicos() const { return evento::Documentos(evento_()).publicos(); }

json DocumentosViewModel::agregar(const fs::path& archivo, const json& datos) {
    evento::Documentos d(evento_());
    std::string error;
    const std::string id = d.agregar(archivo, datos, QDateTime::currentMSecsSinceEpoch(), &error);
    if (id.empty()) return {{"error", error}};
    const json pt = datos.value("pagina_tapa", json(1));
    const Leido l = leer(d.ruta(id), d.miniatura(id), pt.is_number_integer() ? pt.get<int>() : 1);
    json cambios{{"paginas", l.paginas}};
    if (pt.is_number_integer() && l.paginas > 0) cambios["pagina_tapa"] = std::clamp(pt.get<int>(), 1, l.paginas);
    // El título del PDF, si tiene uno de verdad y no lo pidieron otro.
    if (!datos.contains("titulo") && l.titulo.size() > 3 && l.titulo.rfind("Microsoft Word", 0) != 0 && l.titulo != "untitled")
        cambios["titulo"] = l.titulo;
    d.editar(id, cambios);
    emit cambio();
    json out{{"id", id}, {"documento", d.documento(id)}};
    if (!l.texto.empty()) out["comienzo_del_texto"] = l.texto; // para escribir la descripción
    if (!l.miniatura) out["nota"] = "sin miniatura (no es un PDF ni una imagen, o no se pudo leer)";
    return out;
}

std::string DocumentosViewModel::editar(const std::string& id, const json& cambios) {
    evento::Documentos d(evento_());
    // Otra página de carátula: se rehace la miniatura de esa página (y queda guardada).
    if (cambios.contains("pagina_tapa")) {
        const fs::path r = d.ruta(id);
        if (r.empty()) return "no hay un documento «" + id + "»";
        QPdfDocument pdf;
        if (pdf.load(qp(r)) != QPdfDocument::Error::None) return "solo un PDF tiene páginas para elegir la carátula";
        const int p = cambios["pagina_tapa"].is_number_integer() ? cambios["pagina_tapa"].get<int>() : 0;
        if (p < 1 || p > pdf.pageCount()) return "la página tiene que ir de 1 a " + std::to_string(pdf.pageCount());
        if (!tapa(pdf, p - 1, d.miniatura(id))) return "no se pudo hacer la carátula de la página " + std::to_string(p);
    }
    const std::string e = d.editar(id, cambios);
    if (e.empty()) emit cambio();
    return e;
}

std::string DocumentosViewModel::quitar(const std::string& id) {
    const std::string e = evento::Documentos(evento_()).quitar(id);
    if (e.empty()) emit cambio();
    return e;
}

json DocumentosViewModel::texto(const std::string& id, int desde, int hasta) const {
    const evento::Documentos d(evento_());
    const fs::path r = d.ruta(id);
    if (r.empty()) return {{"error", "no hay un documento «" + id + "»"}};
    QPdfDocument pdf;
    if (pdf.load(qp(r)) != QPdfDocument::Error::None) return {{"error", "no es un PDF que se pueda leer"}};
    const int n = pdf.pageCount();
    const int a = std::clamp(desde < 1 ? 1 : desde, 1, std::max(1, n));
    const int b = std::clamp(hasta < 1 ? n : hasta, a, std::max(1, n));
    QString t;
    for (int p = a - 1; p < b && t.size() < 200000; ++p) t += "--- página " + QString::number(p + 1) + " ---\n" + pdf.getAllText(p).text() + "\n";
    return {{"id", id}, {"paginas", n}, {"desde", a}, {"hasta", b}, {"texto", t.toStdString()}};
}

QString DocumentosViewModel::agregarUi(const QStringList& archivos) {
    QStringList errores;
    for (const QString& a : archivos) {
        const QString local = a.startsWith("file:") ? QUrl(a).toLocalFile() : a;
        const json r = agregar(fs::path(local.toStdU16String()), json::object());
        if (r.contains("error")) errores << QString::fromStdString(r.value("error", std::string()));
    }
    return errores.join("; ");
}

QString DocumentosViewModel::editarUi(const QString& id, const QVariantMap& cambios) {
    return QString::fromStdString(editar(id.toStdString(), deQv(cambios)));
}

QString DocumentosViewModel::quitarUi(const QString& id) { return QString::fromStdString(quitar(id.toStdString())); }

void DocumentosViewModel::abrirUi(const QString& id) {
    const fs::path r = evento::Documentos(evento_()).ruta(id.toStdString());
    if (!r.empty()) QDesktopServices::openUrl(QUrl::fromLocalFile(qp(r)));
}

void DocumentosViewModel::abrirCarpetaUi() {
    std::error_code ec;
    fs::create_directories(carpeta(), ec);
    QDesktopServices::openUrl(QUrl::fromLocalFile(qp(carpeta())));
}

} // namespace ba::app
