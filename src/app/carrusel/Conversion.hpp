// ============================================================================
// src/app/carrusel/Conversion.hpp — de un PDF o un PowerPoint a láminas
//
// EL CASO. Faltan diez minutos y el orador trae un .pptx en un pendrive. Nadie
// va a abrirlo en otra máquina, exportarlo a imágenes y copiarlas: se arrastra
// acá y sale un carrusel.
//
// DOS PASOS, Y EL PRIMERO A VECES NO CORRE. Un .pptx pasa por LibreOffice
// headless para volverse PDF; un PDF ya está y se saltea ese paso entero. Del
// PDF a las imágenes rinde Qt, que ya viaja con nosotros. Por eso el que sólo
// recibe PDF no necesita LibreOffice: `necesitaOffice` lo dice antes de
// empezar, para poder avisarlo en vez de fallar a mitad de camino.
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
    /// true si hace falta LibreOffice, es decir: si NO es un PDF.
    static bool necesitaOffice(const QString& archivo);
    /// El soffice del bundle, o "" si este equipo no lo trae.
    static QString soffice();
    /// Deja `p` listo para que LibreOffice (headless) convierta `archivo` a PDF en
    /// `salida`, con su perfil en `perfil` (uno por conversión) y lo que hace falta para
    /// que no muera adentro de la app (señales limpias, entorno mínimo). Lo usan el
    /// carrusel y el conversor a PDF (documentos/APdf).
    static void prepararOffice(QProcess& p, const QString& bin, const QString& archivo, const QString& salida,
                               const QString& perfil);

    /// Arranca. Las imágenes salen en `destino` como lamina-001.png…
    /// :param anchoPx: ancho de cada lámina. 1920 alcanza para pantalla
    ///     completa; más grande sólo hace archivos pesados que nadie mira.
    void convertir(const QString& archivo, const QString& destino, int anchoPx = 1920);
    void cancelar();
    bool ocupado() const;

   signals:
    /// :param etapa: qué está haciendo, tal cual para mostrar.
    /// :param hecho: páginas listas. Durante LibreOffice es 0 de 0: no informa
    ///     progreso, así que la barra va indeterminada y no finge saber.
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
