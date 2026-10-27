// ============================================================================
// src/app/ui/Discursos.cpp — ver Discursos.hpp
// ============================================================================
#include "ui/Discursos.hpp"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QVariantMap>

namespace ba::app {

Discursos::Discursos(QString raiz) : raiz_(std::move(raiz)) {}

void Discursos::setRaiz(const QString& raiz) { raiz_ = raiz; }

bool Discursos::esDiscurso(const QString& nombre) {
    return nombre.endsWith(".txt", Qt::CaseInsensitive) || nombre.endsWith(".md", Qt::CaseInsensitive);
}

int Discursos::contarPalabras(const QString& texto) {
    int n = 0;
    bool dentro = false;
    for (const QChar ch : texto) {
        if (ch.isSpace()) {
            dentro = false;
        } else if (!dentro) {
            dentro = true;
            ++n;
        }
    }
    return n;
}

QString Discursos::limpiarNombre(QString n, bool conExtension) {
    n = n.trimmed();
    // Lo que Windows no admite en un nombre de archivo (Linux solo prohíbe la
    // barra, pero esto tiene que andar en los dos).
    for (const QChar malo : QString(R"(<>:"/\|?*)"))
        n.remove(malo);
    for (QChar& c : n)
        if (c.unicode() < 32) c = ' ';
    n = n.trimmed();
    // Un punto al final: Windows lo tira en silencio y el archivo queda con
    // otro nombre que el que se pidió.
    while (n.endsWith('.'))
        n.chop(1);
    n = n.trimmed();
    if (n.isEmpty()) return {};
    if (conExtension && !esDiscurso(n)) n += ".txt";
    return n;
}

QString Discursos::ruta(const QString& rel) const {
    if (raiz_.isEmpty() || rel.isEmpty()) return {};
    const QDir base(raiz_);
    const QString abs = QDir::cleanPath(base.filePath(rel));
    // Tiene que quedar DENTRO de la raíz: un ".." en una ruta que vino de otro
    // lado no puede sacarle archivos al que la abre.
    const QString dentro = QDir::cleanPath(base.absolutePath());
    if (abs != dentro && !abs.startsWith(dentro + "/")) return {};
    return abs;
}

QStringList Discursos::carpetas() const {
    QStringList out;
    if (raiz_.isEmpty()) return out;
    out << ""; // la raíz siempre está
    const QDir base(raiz_);
    QDirIterator it(raiz_, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        out << base.relativeFilePath(it.filePath());
    }
    out.sort(Qt::CaseInsensitive);
    return out;
}

QVariantList Discursos::listar(const QString& carpeta) const {
    QVariantList out;
    if (raiz_.isEmpty()) return out;
    // La carpeta también se valida: puede venir de una configuración vieja.
    const QString dirAbs = carpeta.isEmpty() ? QDir::cleanPath(QDir(raiz_).absolutePath()) : ruta(carpeta);
    if (dirAbs.isEmpty()) return out;
    const QDir base(raiz_);
    const QDir dir(dirAbs);
    for (const QFileInfo& fi : dir.entryInfoList(QDir::Files, QDir::Name | QDir::IgnoreCase)) {
        if (!esDiscurso(fi.fileName())) continue;
        QFile f(fi.absoluteFilePath());
        int palabras = 0;
        if (f.open(QIODevice::ReadOnly | QIODevice::Text))
            palabras = contarPalabras(QString::fromUtf8(f.readAll()));
        out.append(QVariantMap{{"nombre", fi.fileName()},
                               {"rel", base.relativeFilePath(fi.absoluteFilePath())},
                               {"palabras", palabras},
                               {"duracionSeg", static_cast<int>(palabras * 60.0 / kPpm)},
                               {"modificado", fi.lastModified().toString("dd/MM HH:mm")}});
    }
    return out;
}

QString Discursos::leer(const QString& rel, bool* ok) const {
    if (ok != nullptr) *ok = false;
    const QString abs = ruta(rel);
    if (abs.isEmpty()) return {};
    QFile f(abs);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    if (ok != nullptr) *ok = true;
    return QString::fromUtf8(f.readAll());
}

QString Discursos::escribir(const QString& rel, const QString& texto) {
    const QString abs = ruta(rel);
    if (abs.isEmpty()) return tr("Esa ruta se sale de la carpeta de discursos.");
    QDir().mkpath(QFileInfo(abs).absolutePath());
    QFile f(abs);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return tr("No pude escribir %1").arg(abs);
    f.write(texto.toUtf8());
    return {};
}

QString Discursos::crear(const QString& carpeta, const QString& nombre, QString* relOut) {
    if (raiz_.isEmpty()) return tr("No hay carpeta de discursos.");
    const QString limpio = limpiarNombre(nombre);
    if (limpio.isEmpty()) return tr("Ponele un nombre.");
    const QString rel = carpeta.isEmpty() ? limpio : carpeta + "/" + limpio;
    const QString abs = ruta(rel);
    if (abs.isEmpty()) return tr("Ese nombre no se puede usar.");
    if (QFile::exists(abs)) return tr("Ya hay un discurso que se llama así.");
    QDir().mkpath(QFileInfo(abs).absolutePath());
    QFile f(abs);
    if (!f.open(QIODevice::WriteOnly)) return tr("No pude crear %1").arg(abs);
    if (relOut != nullptr) *relOut = rel;
    return {};
}

QString Discursos::crearCarpeta(const QString& carpeta, const QString& nombre, QString* relOut) {
    if (raiz_.isEmpty()) return tr("No hay carpeta de discursos.");
    const QString limpio = limpiarNombre(nombre, false);
    if (limpio.isEmpty()) return tr("Ponele un nombre.");
    const QString rel = carpeta.isEmpty() ? limpio : carpeta + "/" + limpio;
    const QString abs = ruta(rel);
    if (abs.isEmpty()) return tr("Ese nombre no se puede usar.");
    if (QDir(abs).exists()) return tr("Ya hay una carpeta que se llama así.");
    if (!QDir().mkpath(abs)) return tr("No pude crear la carpeta.");
    if (relOut != nullptr) *relOut = rel;
    return {};
}

QString Discursos::renombrar(const QString& rel, const QString& nombre, QString* relOut) {
    const QString abs = ruta(rel);
    if (abs.isEmpty() || !QFile::exists(abs)) return tr("No encuentro ese discurso.");
    const QString limpio = limpiarNombre(nombre);
    if (limpio.isEmpty()) return tr("Ponele un nombre.");
    const QString carpeta = QFileInfo(rel).path(); // "." si está en la raíz
    const QString nuevoRel = carpeta == "." ? limpio : carpeta + "/" + limpio;
    const QString nueva = ruta(nuevoRel);
    if (nueva.isEmpty()) return tr("Ese nombre no se puede usar.");
    if (nueva == abs) {
        if (relOut != nullptr) *relOut = nuevoRel;
        return {}; // el mismo nombre: no es un error
    }
    if (QFile::exists(nueva)) return tr("Ya hay un discurso que se llama así.");
    if (!QFile::rename(abs, nueva)) return tr("No pude renombrarlo.");
    if (relOut != nullptr) *relOut = nuevoRel;
    return {};
}

QString Discursos::mover(const QString& rel, const QString& aCarpeta, QString* relOut) {
    const QString abs = ruta(rel);
    if (abs.isEmpty() || !QFile::exists(abs)) return tr("No encuentro ese discurso.");
    const QString nombre = QFileInfo(rel).fileName();
    const QString nuevoRel = aCarpeta.isEmpty() ? nombre : aCarpeta + "/" + nombre;
    if (relOut != nullptr) *relOut = nuevoRel;
    if (nuevoRel == rel) return {};
    const QString nueva = ruta(nuevoRel);
    if (nueva.isEmpty()) return tr("Esa carpeta no se puede usar.");
    if (QFile::exists(nueva)) return tr("Allá ya hay un discurso que se llama así.");
    QDir().mkpath(QFileInfo(nueva).absolutePath());
    if (!QFile::rename(abs, nueva)) return tr("No pude moverlo.");
    return {};
}

QString Discursos::borrar(const QString& rel) {
    const QString abs = ruta(rel);
    if (abs.isEmpty() || !QFile::exists(abs)) return tr("No encuentro ese discurso.");
    if (!QFile::remove(abs)) return tr("No pude borrarlo.");
    return {};
}

} // namespace ba::app
