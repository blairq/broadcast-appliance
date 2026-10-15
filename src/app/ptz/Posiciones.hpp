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
// Cada una tiene además un NÚMERO (1, 2, 3…, único en la cámara): es su «preset».
// Los que llaman por número (ptz_preset, un control VISCA por IP, los botones de
// un joystick) van a la posición de ese número. No hay presets aparte: los
// digitales y los de la cámara son posiciones, con su nombre y su comentario
// (con los que se las pide de palabra).
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
    QString id;     ///< "1", "2"… (no cambia aunque cambie el nombre)
    QString nombre; ///< único en la cámara
    int numero = 0; ///< su número de preset (único en la cámara; 0 = se le asigna el primero libre)
    QString comentario;
    // El encuadre digital (si se registró).
    bool digital = false;
    double x = 0.5, y = 0.5, zoom = 1.0;
    // La memoria de la cámara (si se registró).
    QString backend; ///< el que tenía la cámara: visca | onvif | panasonic ("" = sin)
    int memoria = 0; ///< VISCA / Panasonic (0 = ninguna)
    QString token;   ///< ONVIF
    // Cómo se llega: velocidad de la cabeza (0..1; 0 = la de la cámara) y
    // segundos del recorte digital (-1 = lo que diga quien la llama).
    double velocidad = 0;
    double segundos = -1;
    // La foto de cómo se veía al registrarla (archivo en Miniaturas/ptz/, "" = sin).
    QString miniatura;
    // La definición de la entrada al registrarla.
    int ancho = 0, alto = 0;
    QString creada, modificada; ///< ISO 8601
    /// La del sistema (la memoria de la foto del panel PTZ): ocupa su memoria de
    /// la cámara, pero no se lista ni se elige.
    bool interna = false;

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
    /// Agrega (le pone id, fechas y, si no trae uno libre, el primer número libre) y
    /// devuelve la guardada.
    Posicion& agregar(Posicion p);
    /// La de ese número de preset (nullptr si no hay).
    Posicion* porNumero(int numero);
    /// El primer número de preset que nadie usa (desde 1).
    int numeroLibre() const;
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
