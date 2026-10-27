// ============================================================================
// src/app/carrusel/Conversion.hpp — de un PDF o un PowerPoint a láminas
//
// EL CASO. Faltan diez minutos y el orador trae un .pptx en un pendrive. Nadie
// va a abrirlo en otra máquina, exportarlo a imágenes y copiarlas: se arrastra
// acá y sale un carrusel.
//
// DOS PASOS, Y EL PRIMERO A VECES NO CORRE. Un .pptx pasa por DeckCraft (su
// herramienta de línea de comandos, tools/deps/craft.sh) para volverse PDF; un
// PDF ya está y se saltea ese paso entero. Del PDF a las imágenes rinde Qt, que
// ya viaja con nosotros. `necesitaConvertir` lo dice antes de empezar, para
// poder avisar en vez de fallar a mitad de camino.
//
// POR QUE VA DE A UNA PAGINA Y AVISA. Una presentación de cincuenta láminas a
// 1920 px tarda. Si esto fuera una función que devuelve la lista, la ventana
// quedaría congelada justo cuando el operador más mira el reloj. Rinde de a una
// y emite `avance`, que es lo que mueve la barra de trabajo.
//
// CANCELAR NO ES CORTESIA: si alguien apuntó al PDF de 900 páginas equivocado,
// tiene que poder frenarlo sin cerrar el programa.
// ============================================================================
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QProcess;
QT_END_NAMESPACE

namespace ba::carrusel {

class Conversor : public QObject {
    Q_OBJECT
   public:
    explicit Conversor(QObject* parent = nullptr);
    ~Conversor() override;

    /// Extensiones que sabemos desarmar en láminas.
    static bool esDocumento(const QString& archivo);
    /// true si hay que convertirlo antes (NO es un PDF): un PowerPoint pasa por DeckCraft.
    static bool necesitaConvertir(const QString& archivo);
    /// La herramienta de línea de comandos de WordCraft, DeckCraft o GridCraft que viaja
    /// en el bundle (`nombre`: "wordcraft", "deckcraft" o "gridcraft"), o "" si no está.
    static QString herramienta(const QString& nombre);
    /// Deja `p` listo para que `bin` (una de `herramienta`) pase `archivo` a `salida`, en
    /// el formato que dice la extensión de `salida` (.pdf, .png…), con su HOME y sus
    /// carpetas de datos en <datos>/Runtime/craft (portable: nada en el sistema). Lo usan
    /// el carrusel y el conversor a PDF (evento/APdf).
    static void prepararConversion(QProcess& p, const QString& bin, const QString& archivo, const QString& salida);

    /// Arranca. Las imágenes salen en `destino` como lamina-001.png…
    /// :param anchoPx: ancho de cada lámina. 1920 alcanza para pantalla
    ///     completa; más grande sólo hace archivos pesados que nadie mira.
    void convertir(const QString& archivo, const QString& destino, int anchoPx = 1920);
    void cancelar();
    bool ocupado() const;

   signals:
    /// :param etapa: qué está haciendo, tal cual para mostrar.
    /// :param hecho: páginas listas. Mientras DeckCraft convierte es 0 de 0: no
    ///     informa progreso, así que la barra va indeterminada y no finge saber.
    void avance(QString etapa, int hecho, int total);
    void listo(QStringList imagenes);
    void fallo(QString motivo);

   private:
    /// Del PDF (propio o recién hecho) a las láminas.
    void arrancarPaginas(const QString& rutaPdf);
    /// Una página y vuelve al bucle de eventos. Ver el .cpp.
    void rendirUna();

    struct Impl;
    Impl* d_;
};

} // namespace ba::carrusel
