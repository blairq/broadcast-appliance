// ============================================================================
// src/app/medios/Medios.cpp — ver Medios.hpp
// ============================================================================
#include "medios/Medios.hpp"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QRegularExpression>

namespace ba::app::medios {
namespace {

const QStringList kImagenes{"png", "jpg", "jpeg", "gif", "bmp", "tga", "webp", "psd", "svg"};
const QStringList kVideos{"mp4", "mkv", "mov", "webm", "avi", "flv", "ts", "m4v", "mpg", "mpeg", "wmv"};
const QStringList kAudios{"mp3", "wav", "ogg", "flac", "m4a", "aac", "opus", "wma"};
/// De estos sale un carrusel. El `.ppt` viejo tambien lo abre LibreOffice.
const QStringList kDocumentos{"pdf", "pptx", "ppt", "odp"};

/// Cuantos archivos se miran como mucho. Una carpeta de medios tiene cientos;
/// diez mil es que alguien apunto a su disco entero, y recorrerlo entero
/// congelaria la interfaz sin decir por que.
constexpr int kTope = 5000;

} // namespace

const char* nombreDeTipo(Tipo t) {
    switch (t) {
        case Tipo::Imagen: return "imagen";
        case Tipo::Video: return "video";
        case Tipo::Audio: return "audio";
        case Tipo::Documento: return "documento";
        case Tipo::Desconocido: return "desconocido";
    }
    return "desconocido";
}

Tipo tipoDe(const QString& archivo) {
    const QString ext = QFileInfo(archivo).suffix().toLower();
    if (kImagenes.contains(ext)) return Tipo::Imagen;
    if (kVideos.contains(ext)) return Tipo::Video;
    if (kAudios.contains(ext)) return Tipo::Audio;
    if (kDocumentos.contains(ext)) return Tipo::Documento;
    return Tipo::Desconocido;
}

bool sePuedeDesarmar(const QString& archivo) {
    return tipoDe(archivo) == Tipo::Documento;
}

const QStringList& carpetas() {
    static const QStringList c{"Media", "Shares", "Recordings"};
    return c;
}

QString aplanar(const QString& s) {
    QString out = s.normalized(QString::NormalizationForm_D).toLower();
    out.remove(QRegularExpression("[\\x{0300}-\\x{036f}]"));
    out.replace(QRegularExpression("[_\\-.]+"), " ");
    return out.simplified();
}

QVector<Archivo> listar(const QString& raiz, const QString& soloCarpeta) {
    QVector<Archivo> out;
    if (raiz.isEmpty()) return out;
    const QDir base(raiz);
    for (const QString& c : carpetas()) {
        if (!soloCarpeta.isEmpty() && c != soloCarpeta) continue;
        const QString dir = base.absoluteFilePath(c);
        if (!QDir(dir).exists()) continue;
        QDirIterator it(dir, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            if (out.size() >= kTope) break;
            const QFileInfo fi(it.next());
            const Tipo t = tipoDe(fi.fileName());
            // Lo que no sabemos mostrar ni reproducir no entra: llenar la
            // grilla de .txt y .json es ruido sobre lo que se esta buscando.
            if (t == Tipo::Desconocido) continue;
            Archivo a;
            a.ruta = fi.absoluteFilePath();
            a.relativa = base.relativeFilePath(a.ruta);
            a.nombre = fi.fileName();
            a.carpeta = c;
            a.tipo = t;
            a.bytes = fi.size();
            a.modificadoMs = fi.lastModified().toMSecsSinceEpoch();
            out.push_back(std::move(a));
        }
    }
    std::sort(out.begin(), out.end(), [](const Archivo& a, const Archivo& b) {
        if (a.carpeta != b.carpeta) return a.carpeta < b.carpeta;
        return a.relativa.compare(b.relativa, Qt::CaseInsensitive) < 0;
    });
    return out;
}

bool coincide(const QString& texto, const QString& consulta) {
    const QString q = aplanar(consulta);
    if (q.isEmpty()) return true;
    // Todas las palabras tienen que estar, en cualquier orden y en cualquier
    // parte: «logo sponsor» encuentra «Media/sponsors/logo-2.png». Con una sola
    // palabra que falte no aparece, que es lo que uno espera al ir agregando
    // palabras para achicar la lista.
    const QString heno = aplanar(texto);
    for (const QString& p : q.split(' ', Qt::SkipEmptyParts))
        if (!heno.contains(p)) return false;
    return true;
}

QVector<Archivo> filtrar(const QVector<Archivo>& todos, const QString& consulta) {
    if (aplanar(consulta).isEmpty()) return todos;
    QVector<Archivo> out;
    for (const Archivo& a : todos)
        if (coincide(a.relativa, consulta)) out.push_back(a);
    return out;
}

QString resolverAdentro(const QString& raiz, const QString& p) {
    const QString abs = QDir::cleanPath(QDir(raiz).absoluteFilePath(p));
    const QString r = QDir::cleanPath(raiz);
    return (abs == r || abs.startsWith(r + "/")) ? abs : QString();
}

} // namespace ba::app::medios
