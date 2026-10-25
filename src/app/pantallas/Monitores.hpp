// ============================================================================
// src/app/pantallas/Monitores.hpp — los monitores de la máquina y qué va en cada uno
//
// «Salidas → Pantallas → Pantallas conectadas». Antes, para llenar los
// monitores, el operador sacaba a ventana partes del tablero y las arrastraba;
// al día siguiente, de nuevo. Acá se dice UNA vez «en el monitor de la
// izquierda, la multivista; en el del escenario, el programa a pantalla
// completa», y queda.
//
// SE RECUERDA POR MONITOR, NO POR POSICIÓN. El índice de QGuiApplication::
// screens() cambia con cualquier cosa (cuál se prendió primero, un cable que
// se movió) y el nombre del sistema también según dónde: en Windows es
// «\\.\DISPLAY2», un número de orden; en Linux es el conector («DP-1»). Lo
// que identifica al APARATO es fabricante + modelo + número de serie (lo que
// dice su EDID). Cuando el monitor no informa serie —pasa, sobre todo con
// adaptadores— se usa el nombre del sistema como desempate, que es lo mejor
// que hay; y dos iguales sin serie se separan con «#2».
//
// SI NO ESTÁ, SE LO ESPERA. Después de un corte de luz los monitores prenden
// cuando quieren. Un monitor recordado que no está queda en la lista como
// «esperando» y su contenido NO se manda a otro (el programa del escenario no
// tiene que aparecer en el monitor del operador). Cuando aparece, su ventana
// aparece sola.
//
// Lo de la máquina (QScreen) entra por `actualizarPantallas`: en la app lo
// llama `conectarAlSistema` con lo que dice Qt; los tests le pasan pantallas
// inventadas. Así el orden físico, la persistencia y la espera se prueban sin
// monitores de verdad.
//
// Persistencia: <datos>/Config/monitores.json.
// ============================================================================
#pragma once

#include "pantallas/ListaConClave.hpp"

#include <QRect>
#include <QRectF>
#include <QSortFilterProxyModel>
#include <QStringList>

#include <filesystem>
#include <functional>
#include <vector>

namespace ba::app {

class CatalogoVisible;

/// Un monitor conectado, tal como lo informa el sistema.
struct PantallaFisica {
    QString nombre;     ///< el del sistema (QScreen::name)
    QString fabricante; ///< QScreen::manufacturer
    QString modelo;     ///< QScreen::model
    QString serie;      ///< QScreen::serialNumber (puede venir vacío)
    QRect geometria;    ///< en el escritorio virtual
    bool principal = false;
    bool operator==(const PantallaFisica&) const = default;
};

/// Una fila: un monitor conectado, o uno recordado que falta.
struct FilaMonitor {
    QString id;
    PantallaFisica pantalla; ///< vacía si no está
    bool conectado = false;
    int orden = 0;
    // --- lo que se recuerda ---
    QString descripcion; ///< cómo se llamaba, para mostrarlo cuando falta
    QStringList cosas;
    bool pantallaCompleta = true;
    bool operator==(const FilaMonitor&) const = default;
};

class MonitoresModel : public ListaConClave<FilaMonitor> {
    Q_OBJECT
    /// Solo los que tienen que tener ventana AHORA (conectados y con algo
    /// asignado): es el modelo del Instantiator de Main.qml.
    Q_PROPERTY(QObject* activos READ activos CONSTANT)
    /// Cuántos hay conectados.
    Q_PROPERTY(int conectados READ conectados NOTIFY cambio)
    /// Cuántos recordados faltan (esperando que aparezcan).
    Q_PROPERTY(int esperando READ esperando NOTIFY cambio)
    /// Ancho/alto del rectángulo que encierra a todos los conectados (para el dibujo).
    Q_PROPERTY(double proporcion READ proporcion NOTIFY cambio)

   public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NombreRole,        ///< para leer: «DELL U2720Q»
        NombreSistemaRole, ///< QScreen::name, para encontrarlo en Qt.application.screens
        ConectadoRole,
        PrincipalRole,
        OrdenRole, ///< 1, 2, 3… de izquierda a derecha y de arriba abajo (0 si no está)
        GeometriaRole,
        DibujoRole, ///< QRectF 0..1 dentro del rectángulo de todos (para el dibujo)
        CosasRole,  ///< QStringList de ids del catálogo
        TituloRole, ///< lo que muestra, para leer
        PantallaCompletaRole,
        ActivaRole,
        EsperandoRole,
    };

    /// `archivo` vacío = no se guarda (modo seguro, tests que no lo piden).
    MonitoresModel(std::filesystem::path archivo, CatalogoVisible* catalogo, QObject* parent = nullptr);

    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QObject* activos() const { return activos_; }
    int conectados() const;
    int esperando() const;
    double proporcion() const;

    /// El id estable de un monitor (ver la cabecera).
    static QString idDe(const PantallaFisica& p);
    /// Lo mismo desde QML, con lo que trae Qt.application.screens[i].
    Q_INVOKABLE QString idDePantalla(const QString& nombre, const QString& fabricante, const QString& modelo,
                                     const QString& serie) const;

    /// Lo que hay conectado ahora. Ordena, recalcula ids y avisa fila por fila.
    void actualizarPantallas(const std::vector<PantallaFisica>& pantallas);
    /// Se engancha a QGuiApplication (pantallas que entran, salen o se mueven).
    void conectarAlSistema();

    /// Pone en el monitor `id` estas cosas del catálogo (una, o varias en
    /// grilla). Devuelve el error, o vacío. El monitor puede no estar
    /// conectado (se lo recuerda y se espera).
    Q_INVOKABLE QString asignar(const QString& id, const QStringList& cosas, bool pantallaCompleta);
    Q_INVOKABLE QString setPantallaCompleta(const QString& id, bool si);
    /// Deja el monitor sin nada (su ventana se cierra). No lo olvida si no está.
    Q_INVOKABLE QString quitar(const QString& id);
    /// Olvida un monitor que no está (deja de esperarlo).
    Q_INVOKABLE QString olvidar(const QString& id);
    /// La fila de ese monitor como mapa (los nombres de los roles), o vacío.
    Q_INVOKABLE QVariantMap monitor(const QString& id) const;
    /// Un monitor por id, por su número de orden («2») o por su nombre.
    QString resolver(const QString& quien) const;

   signals:
    void cambio();

   private:
    using Fila = FilaMonitor;
    int filaDe(const QString& id) const;
    QString nombreDe(const Fila& f) const;
    QString tituloDe(const Fila& f) const;
    /// Avisa una fila que cambió y guarda.
    void tocada(int fila);
    void cargar();
    void guardar() const;

    std::filesystem::path archivo_;
    CatalogoVisible* catalogo_ = nullptr;
    /// Lo último que dijo el sistema (para volver a armar al olvidar o asignar).
    std::vector<PantallaFisica> pantallas_;
    QRect limites_;
    QSortFilterProxyModel* activos_ = nullptr;
};

} // namespace ba::app
