// ============================================================================
// src/app/ui/LineaProductor.hpp — el rol «productor»: vigila el over/under
//
// PLAN-LINEA-DE-TIEMPO.md 3.7 (L9). Se suscribe a linea/+/overunder (lo publica
// ui/LineaBus) y, mientras la línea corre, cuando una salida se pasa del umbral
// (evento/LineaProductor.hpp, Vigia: la primera vez, cada escalón más y, si sigue
// igual, cada tanto) arma una PROPUESTA con el solucionador del back-timing
// (linea_ajustar, cada opción ya probada) y avisa en la conducción: «vamos +3:20;
// si acortamos el panel 3 min llegamos».
//
// NO EJECUTA. La propuesta queda a la vista (propiedad `propuestas`, y en el bus
// linea/<salida>/propuesta para la escaleta web) hasta que una persona elige una
// opción (aplicar) o la descarta. Si el over se recupera, la propuesta se va sola.
// ============================================================================
#pragma once

#include <QObject>
#include <QVariantList>

#include <nlohmann/json.hpp>

#include <map>
#include <string>

#include "evento/LineaProductor.hpp"

namespace ba::app {

class EventoViewModel;
namespace control {
class BusEventos;
}

class LineaProductor : public QObject {
    Q_OBJECT
    /// [{salida, overSeg, texto, opciones: [{id, tipo, texto, ganaSeg, resultado}]}]
    Q_PROPERTY(QVariantList propuestas READ propuestasUi NOTIFY propuestasChanged)
    /// Vigila o no (por omisión, sí).
    Q_PROPERTY(bool activo READ activo WRITE setActivo NOTIFY activoChanged)
    /// Desde cuántos segundos pasados propone (60 por omisión).
    Q_PROPERTY(int umbralSeg READ umbralSeg WRITE setUmbralSeg NOTIFY activoChanged)
   public:
    LineaProductor(EventoViewModel& ev, control::BusEventos& bus, QObject* parent = nullptr);
    ~LineaProductor() override;

    /// linea_ajustar entero: el solucionador con cada opción probada (`resultado`:
    /// {fin, overUnderSeg, reparos}). `hastaMs` <= 0: el fin planeado. Lo usan el
    /// MCP y la propuesta, así hay una sola manera de proponer.
    static nlohmann::json ajusteProbado(EventoViewModel& ev, const std::string& salida, int64_t hastaMs);

    QVariantList propuestasUi() const;
    bool activo() const { return activo_; }
    void setActivo(bool a);
    int umbralSeg() const { return vigia_.umbralSeg; }
    void setUmbralSeg(int s);

    /// Una persona eligió la opción `opcion` de la propuesta de `salida`: se aplica
    /// (linea_editar, queda la revisión para deshacer). "" o el error.
    Q_INVOKABLE QString aplicar(const QString& salida, const QString& opcion);
    /// La persona la descarta (vuelve a proponer si el over crece un escalón).
    Q_INVOKABLE void descartar(const QString& salida);

    /// Lo que llega del bus (público para las pruebas).
    void overUnder(const std::string& salida, const nlohmann::json& valor, int64_t ahoraMs);

   signals:
    void propuestasChanged();
    void activoChanged();
    /// El aviso para la conducción (StudioViewModel lo muestra).
    void aviso(const QString& texto);

   private:
    void sacar(const std::string& salida);
    EventoViewModel& ev_;
    control::BusEventos& bus_;
    int oyente_ = 0;
    bool activo_ = true;
    evento::linea::Vigia vigia_;
    std::map<std::string, nlohmann::json> propuestas_; ///< por salida (con las ops)
};

} // namespace ba::app
