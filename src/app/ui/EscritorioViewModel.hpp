// ============================================================================
// src/app/ui/EscritorioViewModel.hpp — el área del escritorio: monitores y ventanas (F3-03, partición)
//
// Sale de StudioViewModel: dónde se muestra cada cosa en esta máquina, cada pieza su
// modelo (src/app/pantallas/ y ui/MultiviewViewModel):
//
//   CatalogoVisible     todo lo que se puede mostrar en una ventana, un monitor o una TV,
//                       con id estable (escenas, multivistas, el fondo del tablero, las
//                       cámaras con PTZ, las páginas, lo de las pantallas de la red, los
//                       tótems y cada video suelto)
//   MonitoresModel      los monitores de la máquina y qué va en cada uno
//   VentanasModel       las ventanas sueltas, que se recuerdan
//   MultiviewViewModel  las multivistas y sus ventanas
//
// Acá se arma el catálogo con lo que publica cada uno y se lo refresca cuando algo de
// eso cambia; identificar los monitores (un cartel en cada uno) también es de acá.
//
// En QML los modelos siguen en Studio (Studio.catalogo, Studio.pantallasConectadas,
// Studio.ventanas, Studio.multivistas); lo propio del área en Studio.escritorio.
// ============================================================================
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <functional>
#include <memory>

namespace ba::app {
class AppRuntime;
class CatalogoVisible;
class DashboardModel;
class FrameService;
class ImagenViewModel;
class MonitoresModel;
class MultiviewViewModel;
class PantallasViewModel;
class VentanasModel;
} // namespace ba::app

namespace ba::app {

class EscritorioViewModel : public QObject {
    Q_OBJECT
    /// Los monitores conectados, en orden físico: [{id, nombre, orden, sistema,
    /// principal, ancho, alto, x, y}] (los de Salidas → Pantallas).
    Q_PROPERTY(QVariantList monitores READ monitores NOTIFY monitoresCambio)
    /// Si en este escritorio un programa puede decir DONDE va una ventana.
    ///
    /// En Wayland no: ubicarlas es del compositor. Y lo peor es que pedirlo no
    /// falla —Qt hasta informa la pantalla nueva— pero la ventana no se mueve.
    /// Con esto la interfaz puede hacer lo único que sí funciona (pantalla
    /// completa en un monitor) en vez de un botón que no hace nada.
    Q_PROPERTY(bool puedeUbicarVentanas READ puedeUbicarVentanas CONSTANT)
   public:
    struct Partes {
        AppRuntime& rt;
        FrameService& cuadros;            ///< los videos sueltos (invitados, sus pantallas, los tótems)
        ImagenViewModel& imagen;          ///< las cámaras con PTZ y las entradas de video
        DashboardModel* tablero = nullptr; ///< el fondo del tablero (puede no haber)
        PantallasViewModel* pantallas = nullptr; ///< lo que muestran las pantallas de la red
        /// Los tótems de registro (se arman después): cada uno a una TV.
        std::function<QVariantList()> totems;
    };
    explicit EscritorioViewModel(Partes p, QObject* parent = nullptr);
    ~EscritorioViewModel() override;

    CatalogoVisible& catalogo() { return *catalogo_; }
    MonitoresModel& monitoresModelo() { return *monitores_; }
    VentanasModel& ventanas() { return *ventanas_; }
    MultiviewViewModel& multivistas() { return *multivistas_; }
    QObject* catalogoQml() const;
    QObject* monitoresQml() const;
    QObject* ventanasQml() const;
    QObject* multivistasQml() const;

    QVariantList monitores() const;
    static bool puedeUbicarVentanas();
    /// El cartel con su número y su nombre (los de la página Pantallas) en cada monitor, o
    /// solo en uno: `cual` es su número, su nombre o su id. Devuelve el error ("" = puesto).
    /// Es la única forma de saber cuál es «DP-1» sin ponerse a mover ventanas a ver cuál
    /// se mueve.
    Q_INVOKABLE QString identificarPantallas(const QString& cual = {});
    /// Los monitores donde YA hay una ventana nuestra, por nombre. Lo usa una
    /// ventana que se abre para nacer en uno libre en vez de encima de otra.
    Q_INVOKABLE QStringList ventanasAbiertas() const;
    /// Algo de lo que se puede mostrar cambió (las escenas, los tótems): el catálogo se rehace.
    void refrescarCatalogo();

   signals:
    void monitoresCambio();
    /// Alguien pidió identificar los monitores (ver `identificarPantallas`).
    void identificarPedido(const QStringList& ids); ///< vacío: todos

   private:
    Partes p_;
    // El catálogo antes que los monitores: los monitores le piden los títulos
    // (y se destruyen primero). Las multivistas, al final: usan las ventanas y los monitores.
    std::unique_ptr<CatalogoVisible> catalogo_;
    std::unique_ptr<MonitoresModel> monitores_;
    std::unique_ptr<VentanasModel> ventanas_;
    std::unique_ptr<MultiviewViewModel> multivistas_;
};

} // namespace ba::app
