// src/app/ptz/Posiciones.hpp — las posiciones con nombre de una cámara
//
// Una posición es un lugar al que se vuelve por su nombre («Atril», «Mesa
// del jurado»), con un comentario para el operador y la definición (la
// resolución de la entrada) que tenía cuando se registró. Guarda:
//   * el encuadre DIGITAL (x, y, zoom), si el PTZ digital estaba prendido;
//   * la memoria de la CÁMARA donde quedó grabada, si es motorizada: un
//     número de memoria (VISCA, Panasonic) reservado de arriba hacia abajo
//     para no pisar los presets rápidos 1-16, o el token que devolvió la
//     cámara (ONVIF).
// Ir a una posición manda las dos cosas.
//
// Esta clase es solo la lista (alta, baja, modificación, búsqueda por id o
// nombre, memorias libres, json): sin cámaras ni libobs, para poder probarla.

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace ba::app::ptz {

struct Posicion {
    QString id;         ///< "1", "2"… (no cambia aunque cambie el nombre)
    QString nombre;     ///< único en la cámara
    QString comentario;
    // El encuadre digital (si se registró).
    bool digital = false;
    double x = 0.5, y = 0.5, zoom = 1.0;
    // La memoria de la cámara (si se registró).
    QString backend;    ///< el que tenía la cámara: visca | onvif | panasonic ("" = sin)
    int memoria = 0;    ///< VISCA / Panasonic (0 = ninguna)
    QString token;      ///< ONVIF
    // La definición de la entrada al registrarla.
    int ancho = 0, alto = 0;
    QString creada, modificada; ///< ISO 8601

    bool tieneCamara() const { return memoria > 0 || !token.isEmpty(); }
    QString definicion() const;
    QJsonObject json() const;
    static Posicion desdeJson(const QJsonObject& j);
};

class Posiciones {
   public:
    const QList<Posicion>& lista() const { return lista_; }
    /// Por id exacto, o por nombre sin importar mayúsculas ni tildes.
    Posicion* buscar(const QString& idONombre);
    const Posicion* buscar(const QString& idONombre) const;
    /// "" si el nombre sirve; si no, por qué (vacío, repetido).
    QString validarNombre(const QString& nombre, const QString& excepto = {}) const;
    /// Agrega (le pone id y fechas) y devuelve la guardada.
    Posicion& agregar(Posicion p);
    bool borrar(const QString& id);
    /// La memoria libre más alta de la cámara entre `desde` y `hasta` (0 si no hay).
    int memoriaLibre(int hasta, int desde = 17) const;

    QJsonArray json() const;
    void cargar(const QJsonArray& a);

    /// Nombre normalizado para comparar: minúsculas, sin tildes ni espacios de más.
    static QString normalizar(const QString& s);
    static QString ahora();

   private:
    QList<Posicion> lista_;
    int siguiente_ = 1;
};

} // namespace ba::app::ptz
