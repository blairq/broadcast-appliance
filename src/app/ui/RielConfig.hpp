// ============================================================================
// src/app/ui/RielConfig.hpp — la barra de navegación, a gusto del operador
//
// El riel creció hasta catorce páginas y no todo el mundo usa las mismas: el
// que hace streaming a Twitch no quiere ver Facebook, y el que trabaja en una
// pantalla ancha y baja prefiere la barra arriba y no al costado.
//
// Acá vive ESE gusto, y también EL CATÁLOGO de páginas. Los dos juntos a
// propósito: el catálogo estaba escrito adentro del QML del riel, así que para
// poder elegir qué mostrar había que repetirlo en la pantalla de
// Configuración. Repetido, se desincroniza; una página nueva aparecía en un
// lado y no en el otro. Ahora se declara una vez y las dos pantallas lo leen.
//
// Cada página tiene un ID CORTO Y ESTABLE ("dashboard", "prompter"): es el
// nombre con el que se la pide por MCP y el que se escribe en riel.json. El
// índice del StackLayout no sirve para eso —cambia si se agrega una página en
// el medio— y el título tampoco, porque se puede reescribir.
//
// Se guarda en Config/riel.json. Es preferencia de interfaz, no configuración
// del aparato: no reinicia nada y no viaja con el evento.
// ============================================================================
#pragma once

#include <filesystem>
#include <memory>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

namespace ba::app {

class RielConfig : public QObject {
    Q_OBJECT
    /// Dónde va la barra: "izquierda", "derecha", "arriba" o "abajo".
    Q_PROPERTY(QString posicion READ posicion WRITE setPosicion NOTIFY cambio)
    /// Lado del botón, en píxeles. El grosor de la barra sale de esto.
    Q_PROPERTY(int tamano READ tamano WRITE setTamano NOTIFY cambio)
    /// true si la barra va en vertical (izquierda o derecha).
    Q_PROPERTY(bool vertical READ vertical NOTIFY cambio)
    /// Cuánto ocupa la barra a lo ancho (o alto, si es horizontal).
    Q_PROPERTY(int grosor READ grosor NOTIFY cambio)
    /// Si se muestran los rótulos de grupo (solo cuando hay lugar).
    Q_PROPERTY(bool separadores READ separadores WRITE setSeparadores NOTIFY cambio)
    /// Si la barra se abre en árbol al pasarle el puntero por encima.
    Q_PROPERTY(bool expandir READ expandir WRITE setExpandir NOTIFY cambio)
    /// Cuánto mide abierta, en píxeles. Solo aplica en vertical.
    Q_PROPERTY(int anchoAbierto READ anchoAbierto WRITE setAnchoAbierto NOTIFY cambio)

    /// Los grupos con sus páginas: [{ titulo, items: [{page, icon, tip, visible}] }].
    /// Es lo que dibuja el riel Y lo que lista Configuración.
    Q_PROPERTY(QVariantList grupos READ grupos NOTIFY cambio)
    /// Todas las páginas en una sola lista, para el formulario de Configuración.
    Q_PROPERTY(QVariantList paginas READ paginas NOTIFY cambio)
    /// Cuántas están escondidas (para poder decirlo sin contar a mano).
    Q_PROPERTY(int escondidas READ escondidas NOTIFY cambio)

   public:
    explicit RielConfig(std::filesystem::path archivo, QObject* parent = nullptr);
    ~RielConfig() override;

    QString posicion() const;
    void setPosicion(const QString& p);
    int tamano() const;
    void setTamano(int px);
    bool vertical() const;
    int grosor() const;
    bool separadores() const;
    void setSeparadores(bool s);
    bool expandir() const;
    void setExpandir(bool e);
    int anchoAbierto() const;
    void setAnchoAbierto(int px);
    QVariantList grupos() const;
    QVariantList paginas() const;
    int escondidas() const;

    /// El índice de página de un id ("guion"), o -1 si no existe.
    Q_INVOKABLE int paginaDeId(const QString& id) const;
    /// El id de una página por su índice, o vacío.
    Q_INVOKABLE QString idDePagina(int pagina) const;
    /// El título corto de una página por su índice, o vacío.
    Q_INVOKABLE QString tituloDePagina(int pagina) const;
    /// Todos los ids, en el orden del catálogo. Es lo que ofrece el MCP.
    static QStringList idsDePaginas();
    /// Todas las páginas, también el asistente: [{page, id, icon, titulo}]. Es
    /// lo que usa el catálogo de cosas que se pueden mostrar (pantallas/Catalogo).
    static QVariantList todasLasPaginas();

    /// Muestra o esconde una página. La que está a la vista no se puede
    /// esconder desde el propio riel sin dejar al operador sin salida, así que
    /// quien la llama tiene que ocuparse de mover la vista antes.
    Q_INVOKABLE void setVisible(int pagina, bool visible);
    Q_INVOKABLE bool visible(int pagina) const;
    /// Vuelve a como viene de fábrica: todas a la vista, a la izquierda.
    Q_INVOKABLE void restaurar();

   signals:
    void cambio();

   private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace ba::app
