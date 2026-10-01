// ============================================================================
// src/app/ui/Discursos.hpp — los textos del teleprompter, en carpetas
//
// Una carpeta con archivos de texto y subcarpetas. Suena a poco para una
// clase, pero tiene tres cosas que conviene tener en un solo lugar y probadas:
//
//   1. NADA SALE DE LA RAIZ. Los nombres los escribe una persona apurada y las
//      rutas relativas vienen del guion, que pudo escribirlo otro en otra
//      maquina. Un ".." no puede hacer que el programa lea o borre cualquier
//      archivo del disco.
//   2. LOS NOMBRES TIENEN QUE SERVIR EN WINDOWS Y EN LINUX. Los dos puntos y
//      la barra son legales en uno y no en el otro, y un punto al final hace
//      que Windows cree un archivo con otro nombre y nadie se entere.
//   3. LA CUENTA DE PALABRAS ES LA DEL GATEWAY. De ella sale la duracion
//      estimada; si las dos cuentas no coinciden, lo que dice la pantalla no
//      es lo que despues se ve en el teleprompter.
//
// La raiz la pone quien la usa: hoy es Eventos/<evento>/Discursos/, que es de
// donde los lee el cue `teleprompter` del guion. Una sola carpeta, no dos
// parecidas.
// ============================================================================
#pragma once

#include <QString>
#include <QStringList>
#include <QVariantList>

namespace ba::app {

class Discursos {
   public:
    /// Palabras por minuto de una lectura normal en castellano (la misma que
    /// usa el gateway para estimar cuanto dura un discurso).
    static constexpr int kPpm = 140;

    /// `raiz` vacia = no hay dónde trabajar (no hay evento abierto): todo
    /// devuelve vacío o el error, sin tocar el disco.
    explicit Discursos(QString raiz = {});

    void setRaiz(const QString& raiz);
    QString raiz() const { return raiz_; }
    bool listo() const { return !raiz_.isEmpty(); }

    /// La MISMA cuenta que hace el gateway (ver Prompter::contarPalabras).
    static int contarPalabras(const QString& texto);
    /// Un nombre que sirva de archivo en Windows y en Linux; vacío si no
    /// queda nada usable. Le agrega .txt si no tiene extensión de discurso.
    static QString limpiarNombre(QString n, bool conExtension = true);
    static bool esDiscurso(const QString& nombre);

    /// La ruta absoluta de `rel`, o vacía si se sale de la raíz.
    QString ruta(const QString& rel) const;

    /// Todas las carpetas, con su ruta relativa; la primera es "" (la raíz).
    QStringList carpetas() const;
    /// Los documentos de `carpeta` (no los de sus subcarpetas):
    /// [{nombre, rel, palabras, duracionSeg, modificado}].
    QVariantList listar(const QString& carpeta) const;

    /// Devuelve el texto; `ok` dice si se pudo.
    QString leer(const QString& rel, bool* ok = nullptr) const;
    /// Escribe (crea si no está). Devuelve el error, o vacío si salió bien.
    QString escribir(const QString& rel, const QString& texto);
    /// Crea uno vacío en `carpeta`. Deja su `rel` en `relOut`.
    QString crear(const QString& carpeta, const QString& nombre, QString* relOut = nullptr);
    QString crearCarpeta(const QString& carpeta, const QString& nombre, QString* relOut = nullptr);
    /// Le cambia el nombre dejándolo en su carpeta.
    QString renombrar(const QString& rel, const QString& nombre, QString* relOut = nullptr);
    /// Lo lleva a otra carpeta ("" es la raíz), con el mismo nombre.
    QString mover(const QString& rel, const QString& aCarpeta, QString* relOut = nullptr);
    QString borrar(const QString& rel);

   private:
    QString raiz_;
};

} // namespace ba::app
