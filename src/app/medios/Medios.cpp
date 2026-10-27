// ============================================================================
// src/app/medios/Medios.cpp — ver Medios.hpp
// ============================================================================
#include "medios/Medios.hpp"
#include "ajustes/Ajustes.hpp"

#include <algorithm>
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
/// De estos sale un carrusel (un .pptx, con DeckCraft).
const QStringList kDocumentos{"pdf", "pptx", "deckcraft"}; // los de carrusel::Conversor::esDocumento

/// Cuantos archivos se miran como mucho. Una carpeta de medios tiene cientos;
/// diez mil es que alguien apunto a su disco entero, y recorrerlo entero
/// congelaria la interfaz sin decir por que.
inline int kTope() { return ba::ajustes::entero("limites.mediosArchivos", 5000); }

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

bool sePuedeDesarmar(const QString& archivo) { return tipoDe(archivo) == Tipo::Documento; }

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
            if (out.size() >= kTope()) break;
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

// --- El arbol de carpetas ---------------------------------------------------

namespace {

/// Carga un nivel y baja. Devuelve las carpetas en orden de dibujo.
void bajar(const QString& raiz, const QString& rel, int nivel, QVector<Carpeta>& out) {
    QDir d(raiz + "/" + rel);
    QStringList hijas = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& h : hijas) {
        // Las ocultas no se muestran: si alguien puso un `.cache` adentro de
        // Media no es suyo y ensucia el arbol.
        if (h.startsWith('.')) continue;
        Carpeta c;
        c.relativa = rel + "/" + h;
        c.nombre = h;
        c.nivel = nivel;
        out.push_back(c);
        // Un limite duro: un arbol de veinte niveles no lo mira nadie y casi
        // siempre es un enlace circular o una copia de seguridad entera.
        if (nivel < 8) bajar(raiz, c.relativa, nivel + 1, out);
    }
}

} // namespace

QVector<Carpeta> arbol(const QString& raiz, const QVector<Archivo>& todos) {
    QVector<Carpeta> out;
    for (const QString& base : carpetas()) {
        if (!QDir(raiz + "/" + base).exists()) continue;
        Carpeta c;
        c.relativa = base;
        c.nombre = base;
        c.nivel = 0;
        c.raiz = true;
        out.push_back(c);
        bajar(raiz, base, 1, out);
    }
    // Las cuentas de una sola pasada por los archivos: preguntarle al disco
    // por cada carpeta serian cientos de recorridos para el mismo dato.
    for (const Archivo& a : todos) {
        const int corte = a.relativa.lastIndexOf('/');
        const QString suya = corte < 0 ? QString() : a.relativa.left(corte);
        for (Carpeta& c : out) {
            if (c.relativa == suya) ++c.propios;
            if (suya == c.relativa || suya.startsWith(c.relativa + "/")) ++c.total;
        }
    }
    return out;
}

void ordenar(QVector<Archivo>& lista, const QString& criterio, bool descendente) {
    auto por = [&](auto clave) {
        std::stable_sort(lista.begin(), lista.end(), [&](const Archivo& x, const Archivo& y) {
            return descendente ? clave(y) < clave(x) : clave(x) < clave(y);
        });
    };
    if (criterio == "fecha") {
        por([](const Archivo& a) { return a.modificadoMs; });
    } else if (criterio == "tamano") {
        por([](const Archivo& a) { return a.bytes; });
    } else if (criterio == "tipo") {
        // Dentro de cada tipo queda el orden anterior (por eso stable_sort):
        // «todas las imagenes, alfabeticas» es lo que uno espera.
        por([](const Archivo& a) { return int(a.tipo); });
    } else if (criterio == "nombre") {
        por([](const Archivo& a) { return aplanar(a.nombre); });
    }
}

namespace {

/// Las dos validaciones que comparten todas las operaciones de archivo: que la
/// ruta caiga adentro del sistema y que no sea una de las carpetas raiz.
bool aptoParaTocar(const QString& raiz, const QString& queRel, QString* error, QString& abs) {
    abs = resolverAdentro(raiz, queRel);
    if (abs.isEmpty()) {
        if (error) *error = "esa ruta se sale de la carpeta del sistema";
        return false;
    }
    if (!QFileInfo::exists(abs)) {
        if (error) *error = "no existe: " + queRel;
        return false;
    }
    if (carpetas().contains(QDir::cleanPath(queRel))) {
        if (error) *error = "«" + queRel + "» es una carpeta del sistema: no se mueve ni se borra";
        return false;
    }
    return true;
}

/// Un nombre libre en `dir`. Numera en vez de pisar.
QString libre(const QString& dir, const QString& nombre) {
    if (!QFileInfo::exists(dir + "/" + nombre)) return nombre;
    const QFileInfo fi(nombre);
    const QString base = fi.completeBaseName();
    const QString ext = fi.suffix().isEmpty() ? QString() : "." + fi.suffix();
    for (int i = 2; i < 1000; ++i) {
        const QString n = base + " (" + QString::number(i) + ")" + ext;
        if (!QFileInfo::exists(dir + "/" + n)) return n;
    }
    return nombre;
}

} // namespace

bool mover(const QString& raiz, const QString& queRel, const QString& destinoRel, QString* error) {
    QString abs;
    if (!aptoParaTocar(raiz, queRel, error, abs)) return false;
    const QString dest = resolverAdentro(raiz, destinoRel);
    if (dest.isEmpty() || !QFileInfo(dest).isDir()) {
        if (error) *error = "el destino no es una carpeta: " + destinoRel;
        return false;
    }
    // Una carpeta adentro de si misma desaparece del arbol y se lleva lo que
    // tenia: el sistema de archivos lo permite y nadie lo quiso hacer nunca.
    if (QFileInfo(abs).isDir() && (dest == abs || dest.startsWith(abs + "/"))) {
        if (error) *error = "no se puede mover una carpeta adentro de si misma";
        return false;
    }
    if (QFileInfo(abs).dir().absolutePath() == dest) return true; // ya estaba ahi
    const QString destino = dest + "/" + libre(dest, QFileInfo(abs).fileName());
    if (!QDir().rename(abs, destino)) {
        if (error) *error = "no se pudo mover (¿permisos?)";
        return false;
    }
    return true;
}

bool crearCarpeta(const QString& raiz, const QString& padreRel, const QString& nombre, QString* error) {
    const QString limpio = nombre.trimmed();
    // Un nombre con barra crearia dos carpetas de una y el operador vería una
    // sola: mejor decirlo que adivinar cual quiso.
    if (limpio.isEmpty() || limpio.contains('/') || limpio.contains('\\') || limpio.startsWith('.')) {
        if (error) *error = "nombre de carpeta no valido";
        return false;
    }
    const QString padre = resolverAdentro(raiz, padreRel);
    if (padre.isEmpty() || !QFileInfo(padre).isDir()) {
        if (error) *error = "no existe la carpeta " + padreRel;
        return false;
    }
    if (QFileInfo::exists(padre + "/" + limpio)) {
        if (error) *error = "ya hay algo llamado «" + limpio + "» ahi";
        return false;
    }
    if (!QDir(padre).mkdir(limpio)) {
        if (error) *error = "no se pudo crear (¿permisos?)";
        return false;
    }
    return true;
}

bool renombrar(const QString& raiz, const QString& queRel, const QString& nombre, QString* error) {
    QString abs;
    if (!aptoParaTocar(raiz, queRel, error, abs)) return false;
    const QString limpio = nombre.trimmed();
    if (limpio.isEmpty() || limpio.contains('/') || limpio.contains('\\') || limpio.startsWith('.')) {
        if (error) *error = "nombre no valido";
        return false;
    }
    const QString dir = QFileInfo(abs).dir().absolutePath();
    if (QFileInfo::exists(dir + "/" + limpio) && dir + "/" + limpio != abs) {
        if (error) *error = "ya hay algo llamado «" + limpio + "» ahi";
        return false;
    }
    if (!QDir().rename(abs, dir + "/" + limpio)) {
        if (error) *error = "no se pudo renombrar (¿permisos?)";
        return false;
    }
    return true;
}

bool aPapelera(const QString& raiz, const QString& queRel, QString* error) {
    QString abs;
    if (!aptoParaTocar(raiz, queRel, error, abs)) return false;
    const QString papelera = raiz + "/Papelera";
    if (!QDir().mkpath(papelera)) {
        if (error) *error = "no se pudo crear la papelera";
        return false;
    }
    if (!QDir().rename(abs, papelera + "/" + libre(papelera, QFileInfo(abs).fileName()))) {
        if (error) *error = "no se pudo mover a la papelera (¿permisos?)";
        return false;
    }
    return true;
}

} // namespace ba::app::medios
