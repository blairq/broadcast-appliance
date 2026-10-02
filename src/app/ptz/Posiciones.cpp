// src/app/ptz/Posiciones.cpp — ver Posiciones.hpp

#include "ptz/Posiciones.hpp"

#include <QDateTime>
#include <QSet>

#include <algorithm>

namespace ba::app::ptz {

QString Posicion::definicion() const {
    return ancho > 0 && alto > 0 ? QStringLiteral("%1x%2").arg(ancho).arg(alto) : QString();
}

QJsonObject Posicion::json() const {
    QJsonObject j{{"id", id}, {"nombre", nombre}, {"comentario", comentario}, {"creada", creada}, {"modificada", modificada}};
    if (ancho > 0 && alto > 0) j["definicion"] = QJsonObject{{"ancho", ancho}, {"alto", alto}};
    if (digital) j["digital"] = QJsonObject{{"x", x}, {"y", y}, {"zoom", zoom}};
    if (tieneCamara()) {
        QJsonObject c{{"backend", backend}};
        if (memoria > 0) c["memoria"] = memoria;
        if (!token.isEmpty()) c["token"] = token;
        j["camara"] = c;
    }
    return j;
}

Posicion Posicion::desdeJson(const QJsonObject& j) {
    Posicion p;
    p.id = j.value("id").toString();
    p.nombre = j.value("nombre").toString();
    p.comentario = j.value("comentario").toString();
    p.creada = j.value("creada").toString();
    p.modificada = j.value("modificada").toString();
    const QJsonObject d = j.value("definicion").toObject();
    p.ancho = d.value("ancho").toInt();
    p.alto = d.value("alto").toInt();
    if (j.contains("digital")) {
        const QJsonObject g = j.value("digital").toObject();
        p.digital = true;
        p.x = g.value("x").toDouble(0.5);
        p.y = g.value("y").toDouble(0.5);
        p.zoom = g.value("zoom").toDouble(1.0);
    }
    const QJsonObject c = j.value("camara").toObject();
    p.backend = c.value("backend").toString();
    p.memoria = c.value("memoria").toInt();
    p.token = c.value("token").toString();
    return p;
}

QString Posiciones::normalizar(const QString& s) {
    QString out;
    for (const QChar ch : s.normalized(QString::NormalizationForm_D)) {
        if (ch.category() == QChar::Mark_NonSpacing) continue;
        out += ch.toLower();
    }
    return out.simplified();
}

QString Posiciones::ahora() { return QDateTime::currentDateTime().toString(Qt::ISODate); }

Posicion* Posiciones::buscar(const QString& idONombre) {
    return const_cast<Posicion*>(static_cast<const Posiciones*>(this)->buscar(idONombre));
}

const Posicion* Posiciones::buscar(const QString& idONombre) const {
    const QString clave = idONombre.trimmed();
    for (const Posicion& p : lista_)
        if (p.id == clave) return &p;
    const QString n = normalizar(clave);
    for (const Posicion& p : lista_)
        if (normalizar(p.nombre) == n) return &p;
    return nullptr;
}

QString Posiciones::validarNombre(const QString& nombre, const QString& excepto) const {
    if (nombre.trimmed().isEmpty()) return QStringLiteral("La posición necesita un nombre");
    const QString n = normalizar(nombre);
    for (const Posicion& p : lista_)
        if (p.id != excepto && normalizar(p.nombre) == n)
            return QStringLiteral("Ya hay una posición «%1» en esta cámara").arg(p.nombre);
    // Un nombre que es solo un número se confundiría con un id.
    bool esNumero = false;
    nombre.trimmed().toInt(&esNumero);
    if (esNumero) return QStringLiteral("El nombre no puede ser solo un número (se confunde con el id)");
    return {};
}

Posicion& Posiciones::agregar(Posicion p) {
    p.id = QString::number(siguiente_++);
    p.nombre = p.nombre.trimmed();
    p.creada = p.modificada = ahora();
    lista_.append(p);
    return lista_.last();
}

bool Posiciones::borrar(const QString& id) {
    for (int i = 0; i < lista_.size(); ++i)
        if (lista_[i].id == id) {
            lista_.removeAt(i);
            return true;
        }
    return false;
}

int Posiciones::memoriaLibre(int hasta, int desde) const {
    QSet<int> usadas;
    for (const Posicion& p : lista_)
        if (p.memoria > 0) usadas.insert(p.memoria);
    for (int m = hasta; m >= desde; --m)
        if (!usadas.contains(m)) return m;
    return 0;
}

QJsonArray Posiciones::json() const {
    QJsonArray a;
    for (const Posicion& p : lista_) a.append(p.json());
    return a;
}

void Posiciones::cargar(const QJsonArray& a) {
    lista_.clear();
    siguiente_ = 1;
    for (const QJsonValue& v : a) {
        Posicion p = Posicion::desdeJson(v.toObject());
        if (p.id.isEmpty() || p.nombre.isEmpty()) continue;
        siguiente_ = std::max(siguiente_, p.id.toInt() + 1);
        lista_.append(p);
    }
}

} // namespace ba::app::ptz
