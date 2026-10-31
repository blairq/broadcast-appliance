// ============================================================================
// src/app/ui/MultiviewViewModel.hpp — el área de las multivistas (F3-03, partición)
//
// Sale de StudioViewModel: las multivistas (pestañas de la página Multiview) y sus
// ventanas. El modelo es Config/multiview.json:
//   {"vistas": [{id, nombre, fuentes, distribucion, ...}], "actual": id}
// (el formato viejo, una sola y plana, se lee como «Multiview 1»). Lo que falte en
// una vista de una versión vieja sale de fábrica.
//
// Las ventanas son las de pantallas/Ventanas (una ventana por multivista, con la
// cosa «multiview:<id>») en los monitores de pantallas/Monitores.
//
// En QML: Studio.multivistas (lista, actual, elegida, conVentana y los comandos). El
// MCP (ui_multiview) usa lo mismo.
// ============================================================================
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <filesystem>

namespace ba::app {

class VentanasModel;
class MonitoresModel;

class MultiviewViewModel : public QObject {
    Q_OBJECT
    /// Las multivistas: [{id, nombre, fuentes, distribucion, separacion, fondo, borde,
    /// colorBorde, nombres, tamNombres, tally, vumetros}].
    Q_PROPERTY(QVariantList lista READ lista NOTIFY cambio)
    /// El id de la de la pestaña elegida.
    Q_PROPERTY(QString actual READ actual WRITE setActual NOTIFY cambio)
    /// La de la pestaña elegida.
    Q_PROPERTY(QVariantMap elegida READ elegida NOTIFY cambio)
    /// Los ids de las que tienen una ventana abierta.
    Q_PROPERTY(QStringList conVentana READ conVentana NOTIFY ventanasCambio)
   public:
    /// `config`: la carpeta Config/ de la configuración (ahí está multiview.json).
    explicit MultiviewViewModel(std::filesystem::path config, QObject* parent = nullptr);

    /// Las ventanas sueltas y los monitores (se arman después: el catálogo de las
    /// ventanas lee la lista de acá).
    void setVentanas(VentanasModel* ventanas, MonitoresModel* monitores);

    QVariantList lista() const { return vistas_; }
    QString actual() const { return actual_; }
    void setActual(const QString& id);
    QVariantMap elegida() const { return de(actual_); }
    QStringList conVentana() const;

    /// La de `id` ("" = la elegida); vacío si no hay.
    Q_INVOKABLE QVariantMap de(const QString& id) const;
    /// Cambia lo que venga en `cambios` (lo demás queda) y lo guarda: la elegida / la de `id`.
    Q_INVOKABLE void cambiar(const QVariantMap& cambios);
    Q_INVOKABLE void cambiarDe(const QString& id, const QVariantMap& cambios);
    /// Una pestaña nueva (de fábrica o copia de la elegida); queda elegida. Devuelve su id.
    Q_INVOKABLE QString nueva(const QString& nombre, bool copiarActual);
    Q_INVOKABLE void renombrar(const QString& id, const QString& nombre);
    /// false si es la última (siempre queda una).
    Q_INVOKABLE bool borrar(const QString& id);
    /// La elegida vuelve a la de fábrica (con su nombre).
    Q_INVOKABLE void restablecer();
    /// Abre la ventana de esa multivista (cierra la que tuviera) en `monitor` (id,
    /// número de orden o nombre; "" = el principal, en una ventana), en pantalla
    /// completa si se pide.
    Q_INVOKABLE void abrirEnVentana(const QString& id, const QString& monitor, bool pantallaCompleta);
    /// Una ventana por multivista: una por monitor (los que no son el principal), en
    /// pantalla completa; si sobran o no hay otros monitores, ventanas escalonadas.
    /// Devuelve cuántas abrió.
    Q_INVOKABLE int abrirTodas();
    Q_INVOKABLE int cerrarVentanas();

   signals:
    void cambio();
    void ventanasCambio();

   private:
    void leer();
    int indice(const QString& id) const;
    void guardar();

    std::filesystem::path config_;
    VentanasModel* ventanas_ = nullptr;
    MonitoresModel* monitores_ = nullptr;
    QVariantList vistas_;
    QString actual_;
};

} // namespace ba::app
