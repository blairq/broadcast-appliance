// ============================================================================
// src/app/ui/ManualesViewModel.hpp — los manuales de usuario, generados por el asistente
//
// Configuración → Sistema → Manuales. El asistente de evento arma una biblioteca de
// manuales (capturas de cada pantalla y documentos Word/PDF) siguiendo un encargo en
// inglés (assets/idiomas/en/prompts/manuales.md) que esto completa: el idioma de salida
// lo decide el programa (el de la interfaz del operador), la carpeta, el alcance y las
// páginas del riel. Los manuales quedan en <Datos>/Manuales/<idioma>/, fuera de las
// configuraciones: se ven desde cualquiera, como los registros.
//
// El asistente hace el trabajo; esto solo arma el pedido, frena si hay algo al aire y
// muestra lo que va apareciendo en la carpeta.
// ============================================================================
#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QString>
#include <QVariantList>

#include <functional>

namespace ba::app {

class ManualesViewModel : public QObject {
    Q_OBJECT
    /// El idioma en que se generan: el de la interfaz ({codigo, nombre}).
    Q_PROPERTY(QString idioma READ idioma CONSTANT)
    Q_PROPERTY(QString nombreIdioma READ nombreIdioma CONSTANT)
    /// Dónde quedan (<Datos>/Manuales/<idioma>).
    Q_PROPERTY(QString carpeta READ carpeta CONSTANT)
    /// Los generados: [{nombre, docx, pdf, modificado}] (rutas absolutas; "" si falta uno).
    Q_PROPERTY(QVariantList manuales READ manuales NOTIFY cambio)
    /// Cuántas capturas hay.
    Q_PROPERTY(int capturas READ capturas NOTIFY cambio)
    /// Opciones del encargo.
    Q_PROPERTY(int alcance READ alcance WRITE setAlcance NOTIFY opcionesCambio)
    Q_PROPERTY(bool pdf READ pdf WRITE setPdf NOTIFY opcionesCambio)
    Q_PROPERTY(bool anotar READ anotar WRITE setAnotar NOTIFY opcionesCambio)
    /// El piso de modelo para el asistente y el recomendado (un solo lugar: también van al encargo).
    Q_PROPERTY(QString modeloMinimo READ modeloMinimo CONSTANT)
    Q_PROPERTY(QString modeloRecomendado READ modeloRecomendado CONSTANT)

   public:
    struct Partes {
        std::function<bool()> enVivo;                     ///< sale algo de verdad (no se genera)
        std::function<void(const QString&)> encargar;     ///< al asistente de evento
        std::function<QString()> faltaAsistente;          ///< "" si el asistente puede tomar el encargo
        std::function<QStringList()> paginas;             ///< los ids del riel
    };
    explicit ManualesViewModel(Partes partes, QObject* parent = nullptr);

    QString idioma() const;
    QString nombreIdioma() const;
    QString carpeta() const;
    QVariantList manuales() const { return manuales_; }
    int capturas() const { return capturas_; }
    int alcance() const { return alcance_; }
    void setAlcance(int a);
    bool pdf() const { return pdf_; }
    void setPdf(bool b);
    bool anotar() const { return anotar_; }
    void setAnotar(bool b);
    QString modeloMinimo() const;
    QString modeloRecomendado() const;

    /// El encargo completo (el prompt en inglés con los campos llenos).
    Q_INVOKABLE QString encargo() const;
    /// Se lo pasa al asistente de evento (lo arranca si hace falta). "" o por qué no.
    Q_INVOKABLE QString generar();
    Q_INVOKABLE void refrescar();
    Q_INVOKABLE void abrir(const QString& archivo) const;
    Q_INVOKABLE void abrirCarpeta() const;

   signals:
    void cambio();
    void opcionesCambio();

   private:
    void vigilar();
    Partes p_;
    QVariantList manuales_;
    int capturas_ = 0;
    int alcance_ = 7;
    bool pdf_ = true;
    bool anotar_ = true;
    QFileSystemWatcher vigia_;
};

} // namespace ba::app
