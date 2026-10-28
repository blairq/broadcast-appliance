// src/app/ui/ManualesViewModel.cpp — ver el .hpp

#include "ui/ManualesViewModel.hpp"

#include "i18n/I18n.hpp"
#include "platform/Paths.hpp"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QUrl>

namespace ba::app {

namespace {
// El piso y el recomendado para el asistente que arma los manuales (tareas largas).
// Van a la sección y al encargo: si cambian, se cambian acá.
constexpr const char* kModeloMinimo = "DeepSeek 4.1 Flash";
constexpr const char* kModeloRecomendado = "Claude Opus 5.5";
} // namespace

ManualesViewModel::ManualesViewModel(Partes partes, QObject* parent) : QObject(parent), p_(std::move(partes)) {
    connect(&vigia_, &QFileSystemWatcher::directoryChanged, this, [this]() { refrescar(); });
    refrescar();
}

QString ManualesViewModel::idioma() const { return QString::fromStdString(ba::i18n::actual().codigo); }
QString ManualesViewModel::nombreIdioma() const { return QString::fromStdString(ba::i18n::actual().nombre); }

QString ManualesViewModel::carpeta() const {
    return QString::fromStdU16String((ba::platform::dataRoot() / "Manuales").u16string()) + "/" + idioma();
}

QString ManualesViewModel::modeloMinimo() const { return kModeloMinimo; }
QString ManualesViewModel::modeloRecomendado() const { return kModeloRecomendado; }

void ManualesViewModel::setAlcance(int a) {
    a = a == 19 ? 19 : 7;
    if (a == alcance_) return;
    alcance_ = a;
    emit opcionesCambio();
}
void ManualesViewModel::setPdf(bool b) {
    if (b == pdf_) return;
    pdf_ = b;
    emit opcionesCambio();
}
void ManualesViewModel::setAnotar(bool b) {
    if (b == anotar_) return;
    anotar_ = b;
    emit opcionesCambio();
}

void ManualesViewModel::refrescar() {
    const QDir dir(carpeta());
    // Cada manual por su nombre, con el .docx y el .pdf que haya.
    QMap<QString, QVariantMap> porNombre;
    for (const QFileInfo& f : dir.entryInfoList({"*.docx", "*.pdf"}, QDir::Files, QDir::Name)) {
        QVariantMap& m = porNombre[f.completeBaseName()];
        m["nombre"] = f.completeBaseName();
        m[f.suffix().toLower()] = f.absoluteFilePath();
        const QString cuando = f.lastModified().toString("yyyy-MM-dd HH:mm");
        if (cuando > m.value("modificado").toString()) m["modificado"] = cuando;
    }
    QVariantList lista;
    for (auto it = porNombre.cbegin(); it != porNombre.cend(); ++it) {
        QVariantMap m = it.value();
        if (!m.contains("docx")) m["docx"] = QString();
        if (!m.contains("pdf")) m["pdf"] = QString();
        lista << m;
    }
    const int capturas = static_cast<int>(QDir(dir.filePath("capturas")).entryList({"*.jpg", "*.jpeg", "*.png"}, QDir::Files).size());
    if (lista != manuales_ || capturas != capturas_) {
        manuales_ = lista;
        capturas_ = capturas;
        emit cambio();
    }
    vigilar();
}

void ManualesViewModel::vigilar() {
    // Mientras el asistente escribe, la lista se va llenando sola. La carpeta puede no
    // existir todavía: se vigila la más cercana que exista (la raíz de los datos solo
    // si todavía no hay Manuales/: ahí cambian cosas todo el tiempo).
    const QString manuales = QString::fromStdU16String((ba::platform::dataRoot() / "Manuales").u16string());
    QStringList quiero;
    if (QDir(carpeta()).exists()) quiero = {carpeta(), carpeta() + "/capturas"};
    else if (QDir(manuales).exists()) quiero = {manuales};
    else quiero = {QString::fromStdU16String(ba::platform::dataRoot().u16string())};
    for (const QString& d : vigia_.directories())
        if (!quiero.contains(d)) vigia_.removePath(d);
    for (const QString& d : quiero)
        if (QDir(d).exists() && !vigia_.directories().contains(d)) vigia_.addPath(d);
}

QString ManualesViewModel::encargo() const {
    const std::string plantilla = ba::i18n::En("en").leer("prompts/manuales.md");
    if (plantilla.empty()) return {};
    const QStringList paginas = p_.paginas ? p_.paginas() : QStringList{};
    QStringList conComillas;
    for (const QString& id : paginas) conComillas << "`" + id + "`";
    QString t = QString::fromStdString(plantilla);
    // El comentario de arriba es para quien mantiene el archivo, no para el asistente.
    if (const int fin = t.indexOf("-->"); t.indexOf("<!--") >= 0 && fin > 0) t.remove(t.indexOf("<!--"), fin + 3 - t.indexOf("<!--"));
    const QMap<QString, QString> campos{
        {"idioma", idioma()},
        {"nombre_idioma", nombreIdioma()},
        {"evento", QString::fromStdString(ba::i18n::traducir("manuales", "Manual del sistema"))},
        {"carpeta", carpeta()},
        {"anotar", anotar_ ? "yes" : "no"},
        {"pdf", pdf_ ? "yes" : "no"},
        {"alcance", QString::number(alcance_)},
        {"modelo_minimo", kModeloMinimo},
        {"modelo_recomendado", kModeloRecomendado},
        {"n_paginas", QString::number(paginas.size())},
        {"paginas", conComillas.join(" · ")}};
    for (auto it = campos.cbegin(); it != campos.cend(); ++it) t.replace("{{" + it.key() + "}}", it.value());
    return t;
}

QString ManualesViewModel::generar() {
    if (p_.enVivo && p_.enVivo())
        return tr("Se está transmitiendo o grabando: los manuales se generan cuando termine (el asistente abre otro evento y "
                  "reinicia el programa).");
    const QString e = encargo();
    if (e.isEmpty()) return tr("Falta el encargo de los manuales (prompts/manuales.md) en la instalación.");
    if (!p_.encargar) return tr("El asistente no está disponible.");
    if (p_.faltaAsistente)
        if (const QString falta = p_.faltaAsistente(); !falta.isEmpty()) return falta;
    QDir().mkpath(carpeta() + "/capturas");
    refrescar();
    p_.encargar(e);
    return {};
}

void ManualesViewModel::abrir(const QString& archivo) const {
    if (!archivo.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(archivo));
}

void ManualesViewModel::abrirCarpeta() const {
    QDir().mkpath(carpeta());
    QDesktopServices::openUrl(QUrl::fromLocalFile(carpeta()));
}

} // namespace ba::app
