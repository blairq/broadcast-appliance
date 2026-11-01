// ============================================================================
// src/app/ui/InformeEventoViewModel.hpp — lo que queda de correr el evento
//
// El informe (lo previsto contra lo que duró de verdad, por bloque y por paso), los
// capítulos de la grabación, el acta (quiénes estuvieron y qué se votó) y la escaleta
// para afuera y desde afuera (CSV, MOS). Lee la corrida, el evento abierto y el padrón;
// no corre nada.
// ============================================================================
#pragma once

#include <functional>

#include <QJsonArray>
#include <QObject>
#include <QVariantMap>

#include <nlohmann/json.hpp>

namespace ba::app {

class CorridaViewModel;
class EventoAbierto;
class PadronViewModel;

class InformeEventoViewModel : public QObject {
    Q_OBJECT
    /// Lo que realmente pasó (ver `informe()`): cambia con la corrida y con la línea.
    Q_PROPERTY(QVariantMap informe READ informe NOTIFY informeCambio)
   public:
    InformeEventoViewModel(EventoAbierto& abierto, CorridaViewModel& corrida, PadronViewModel& padron, QObject* parent = nullptr);

    /// Lo que realmente pasó: por bloque y por paso, cuánto estaba previsto y cuánto duró.
    /// Vacío si no se corrió. {evento, ensayo, previstoSeg, realSeg, desvioSeg, cues, bloques: […]}
    QVariantMap informe() const;
    /// Lo guarda en la carpeta del evento, con la fecha en el nombre. El archivo, o vacío.
    Q_INVOKABLE QString guardarInforme();
    /// Los capítulos de la grabación, uno por bloque, medidos desde que arrancó a grabar
    /// (`recInicioMs`, reloj de pared). {ok, motivo, capitulos: [{seg, marca, titulo}], texto, antesDeGrabar}
    QVariantMap capitulos(qint64 recInicioMs) const;
    /// El acta: quiénes asistieron, el quórum y qué se votó (`votaciones`: el historial de la urna).
    QString acta(const QJsonArray& votaciones) const;
    /// Lo que se suma al acta de otro lado (orden del día y uso de la palabra).
    void setAnexoActa(std::function<QString()> fn) { anexoActa_ = std::move(fn); }
    /// La escribe en la carpeta del evento (con su huella SHA-256 al lado). La ruta, o vacío.
    QString guardarActa(const QString& texto) const;
    static QString hashActa(const QString& texto);

    /// La escaleta en "csv" o "mos" (evento/LineaIntercambio.hpp).
    std::string lineaExportar(const std::string& formato) const;
    /// Una escaleta de afuera (CSV o MOS; `formato` vacío: se adivina) como lote: con `probar`
    /// no aplica. {ok, error?, diff, pronostico, ops, avisos, bloques}.
    nlohmann::json lineaImportar(const std::string& formato, const std::string& texto, bool probar,
                                 const std::string& nombreBloque = {});
    /// Lo mismo desde la página (un archivo): "" o el error; el resultado de importar.
    Q_INVOKABLE QString exportarEscaleta(const QString& formato, const QString& ruta);
    Q_INVOKABLE QVariantMap importarEscaleta(const QString& ruta, bool probar);

   signals:
    void notify(const QString& texto, const QString& tipo);
    void informeCambio();

   private:
    EventoAbierto& ab_;
    CorridaViewModel& corrida_;
    PadronViewModel& padron_;
    std::function<QString()> anexoActa_;
};

} // namespace ba::app
