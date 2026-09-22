// ============================================================================
// src/app/ui/RegistroViewModel.hpp — el visor de registros
//
// De qué sirve tener todo registrado si para leerlo hay que salir del programa,
// buscar la carpeta y abrir un archivo de texto de cinco megabytes. Esto es el
// visor: los registros de LOS TRES PROCESOS, entrelazados por hora, con filtro
// por nivel, por módulo y por texto, y siguiendo la cola mientras pasan cosas.
//
// DOS FUENTES, y la diferencia importa:
//
//   * EN MEMORIA: el anillo de este proceso (ver core/diag/Registro.hpp). Es
//     instantáneo y no toca el disco, pero es solo lo de la app y solo desde
//     que arrancó.
//   * DEL DISCO: los archivos de Logs/, que traen también al gateway, al
//     launcher y —lo que de verdad importa después de un incidente— la
//     corrida ANTERIOR.
//
// La hora se muestra en LOCAL aunque en el archivo esté en UTC: en el archivo
// hace falta UTC para poder entrelazar tres procesos sin pensar en husos, y en
// pantalla hace falta la hora del reloj de la pared, que es con la que el
// operador se acuerda de cuándo pasó.
// ============================================================================
#pragma once

#include <memory>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

namespace ba::app {

class AppRuntime;

class RegistroViewModel : public QObject {
    Q_OBJECT
    /// Las líneas que pasan el filtro, de la más vieja a la más nueva.
    Q_PROPERTY(QVariantList lineas READ lineas NOTIFY cambio)
    /// Los nombres de los niveles, de traza a fatal (para la interfaz).
    Q_PROPERTY(QStringList niveles READ niveles CONSTANT)
    /// Los módulos que aparecen en los registros.
    Q_PROPERTY(QStringList modulos READ modulos NOTIFY cambio)

    /// Desde qué nivel se muestra (0 = traza).
    Q_PROPERTY(int minimo READ minimo WRITE setMinimo NOTIFY filtroCambio)
    /// Vacío = todos.
    Q_PROPERTY(QString modulo READ modulo WRITE setModulo NOTIFY filtroCambio)
    Q_PROPERTY(QString busca READ busca WRITE setBusca NOTIFY filtroCambio)
    /// Si se refresca solo y se va al final cuando llega algo nuevo.
    Q_PROPERTY(bool siguiendo READ siguiendo WRITE setSiguiendo NOTIFY filtroCambio)
    /// true = el anillo en memoria de este proceso; false = los archivos.
    Q_PROPERTY(bool enMemoria READ enMemoria WRITE setEnMemoria NOTIFY filtroCambio)

    /// El nivel que el proceso está REGISTRANDO (distinto del que se muestra):
    /// bajarlo hace que se escriba más, subirlo que se escriba menos.
    Q_PROPERTY(int nivelDelProceso READ nivelDelProceso WRITE setNivelDelProceso NOTIFY cambio)
    /// Cuántas líneas se descartaron por estar bajo ese nivel. Se muestra para
    /// que nadie confunda «no pasó nada» con «no lo estaba mirando».
    Q_PROPERTY(qulonglong descartadas READ descartadas NOTIFY cambio)
    Q_PROPERTY(QString carpeta READ carpeta CONSTANT)
    /// Lo que el diario de arranque apagó por haberse llevado puesto el
    /// arranque. Se muestra en el visor porque si algo dejó de cargarse, el
    /// operador tiene que enterarse ahí y poder volver a prenderlo.
    Q_PROPERTY(QStringList apagados READ apagados NOTIFY cambio)
    /// Lo que rompió el arranque ANTERIOR (vacío si arrancó bien).
    Q_PROPERTY(QString culpable READ culpable CONSTANT)

  public:
    explicit RegistroViewModel(AppRuntime& rt, QObject* parent = nullptr);
    ~RegistroViewModel() override;

    QVariantList lineas() const;
    QStringList niveles() const;
    QStringList modulos() const;
    int minimo() const;
    void setMinimo(int n);
    QString modulo() const;
    void setModulo(const QString& m);
    QString busca() const;
    void setBusca(const QString& b);
    bool siguiendo() const;
    void setSiguiendo(bool s);
    bool enMemoria() const;
    void setEnMemoria(bool m);
    int nivelDelProceso() const;
    void setNivelDelProceso(int n);
    qulonglong descartadas() const;
    QString carpeta() const;
    QStringList apagados() const;
    QString culpable() const;

    /// Vuelve a leer ahora.
    Q_INVOKABLE void refrescar();
    /// Todo lo que se está viendo, como texto, para pegarlo en un mensaje.
    Q_INVOKABLE QString comoTexto() const;
    /// Escribe una línea en el registro desde la interfaz. Sirve para dejar
    /// una marca antes de probar algo: después se busca esa marca.
    Q_INVOKABLE void marcar(const QString& texto);
    /// Vuelve a prender algo que el diario había apagado. Tiene efecto en el
    /// próximo arranque: lo que no se cargó no se puede cargar a mitad de camino.
    Q_INVOKABLE void prender(const QString& id);

  signals:
    void cambio();
    void filtroCambio();
    void notify(const QString& texto, const QString& tipo);

  private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace ba::app
