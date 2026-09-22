// ============================================================================
// src/app/ui/RotuladorViewModel.hpp — los textos de lo que está al aire
//
// El IDE de gráficos es para DISEÑARLOS. Esto es para USARLOS mientras el
// evento corre: la escena que está al aire tiene unas fuentes de gráfico, cada
// una muestra un gráfico, y ese gráfico tiene campos de texto. El rotulador es
// esa lista de campos, escribible, con un botón que lo tira al aire.
//
// Por qué va por escena y no por gráfico: en vivo uno no piensa «el zócalo
// grande», piensa «lo que se está viendo». Si la escena al aire no tiene
// ninguna fuente de gráfico, la tarjeta lo dice en vez de mostrar campos que
// no van a ningún lado.
//
// Los valores se guardan por fuente, no por gráfico: dos zócalos en dos
// esquinas distintas de la misma escena son dos rótulos distintos.
//
// Esta misma lista es la que sale por la web al celular (ver web/Rotulador),
// así que todo lo que sabe hacer está acá y no en el QML.
// ============================================================================
#pragma once

#include <memory>

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

QT_FORWARD_DECLARE_CLASS(QJsonObject)

namespace ba::app {

class AppRuntime;
class ConferenceViewModel;
namespace graficos {
class Emisor;
}

class RotuladorViewModel : public QObject {
    Q_OBJECT
    /// La escena que se está viendo. Es de la que se listan las fuentes.
    Q_PROPERTY(QString escena READ escena NOTIFY cambio)
    /// Las fuentes de gráfico de esa escena, con sus campos y valores.
    Q_PROPERTY(QVariantList fuentes READ fuentes NOTIFY cambio)
    /// Los gráficos guardados, para poder elegir cuál va en cada fuente.
    Q_PROPERTY(QStringList graficos READ graficos NOTIFY cambio)

  public:
    RotuladorViewModel(AppRuntime& rt, graficos::Emisor& emisor, ConferenceViewModel* sala,
                       QObject* parent = nullptr);
    ~RotuladorViewModel() override;

    QString escena() const;
    QVariantList fuentes() const;
    QStringList graficos() const;

    /// Vuelve a mirar qué hay en la escena. Se llama sola cuando cambia la
    /// escena al aire.
    Q_INVOKABLE void refrescar();
    /// Qué gráfico muestra esa fuente.
    Q_INVOKABLE void setGrafico(qulonglong fuente, const QString& grafico);
    /// Cambia un campo. No sale al aire hasta que se dispara: en vivo, que un
    /// texto cambie mientras se escribe letra por letra sería un desastre.
    Q_INVOKABLE void setValor(qulonglong fuente, const QString& clave, const QString& valor);
    /// Lo tira al aire con los valores actuales. Devuelve "" si salió bien.
    Q_INVOKABLE QString disparar(qulonglong fuente, bool repetir = false);
    Q_INVOKABLE QString ocultar(qulonglong fuente);

    /// Pide el enlace y el QR para rotular desde el celular. Contesta por
    /// `qrListo`. `conClave` mete la clave en el propio QR: comodo si el QR se
    /// lo pasa el operador a alguien, malo si va a quedar pegado en una pared.
    Q_INVOKABLE void pedirQr(const QString& target, bool conClave);

  signals:
    void cambio();
    /// `modulos` son las filas del QR en unos y ceros; `mensaje` explica por
    /// que no hay enlace, si no lo hay.
    void qrListo(const QString& url, const QStringList& modulos, const QString& mensaje,
                 const QString& clave);
    void notify(const QString& texto, const QString& tipo);

  private:
    /// Publica lo que hay en B y se lleva lo que encargaron desde los
    /// celulares. Una vez por segundo, en un solo viaje.
    void sincronizar();
    /// Ejecuta un encargo que vino del celular.
    void atender(const QJsonObject& encargo);

    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace ba::app
