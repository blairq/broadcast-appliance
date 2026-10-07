// ============================================================================
// src/app/ui/HistoricoViewModel.hpp — la página Histórico (H6 de docs/planes/PLAN-EVENTOS-HISTORICO.md)
//
// El archivo de todos los eventos: carpetas, la lista, y el detalle de cada
// uno (resumen, línea de tiempo, grabaciones y vivos, configuración, archivos),
// la búsqueda en todo, comparar y volver a una configuración, exportar y los
// ajustes de qué se registra. La página Evento sigue siendo preparar y correr;
// esto es lo que queda.
//
// Lee con evento::Historico / Bitacora / FotosConfig / CierreEvento: lo mismo
// que las herramientas del MCP (evento_* e historico_*). Lo que tarda (buscar
// en todo, verificar sellos, exportar con grabaciones) va al pool de hilos y
// avisa con una señal.
// ============================================================================
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <functional>
#include <memory>

#include <nlohmann/json.hpp>

namespace ba::app {

class EventoViewModel;

class HistoricoViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList carpetas READ carpetas NOTIFY cambio)
    Q_PROPERTY(QString enCurso READ enCurso NOTIFY cambio)
    Q_PROPERTY(QVariantMap ajustes READ ajustes NOTIFY ajustesCambio)
    Q_PROPERTY(QVariantMap restauracion READ restauracion NOTIFY restauracionCambio)
    Q_PROPERTY(bool buscando READ buscando NOTIFY buscandoCambio)
   public:
    using json = nlohmann::json;
    explicit HistoricoViewModel(EventoViewModel& ev, QObject* parent = nullptr);
    ~HistoricoViewModel() override;

    /// Cuando cambian los ajustes de qué se registra (los aplica la bitácora).
    void setAplicarAjustes(std::function<void(const json&)> fn) { aplicarAjustes_ = std::move(fn); }
    /// De dónde traer grabaciones viejas (Recordings/ de la configuración).
    void setCarpetasGrabacion(std::function<std::vector<std::filesystem::path>()> fn) { carpetasGrabacion_ = std::move(fn); }

    QStringList carpetas() const;
    QString enCurso() const;

    /// Los eventos de `grupo` (y sus subcarpetas; vacío = todos) que tienen
    /// `texto` en el nombre o la carpeta, con su resumen. Lo más nuevo primero.
    Q_INVOKABLE QVariantList eventos(const QString& grupo, const QString& texto) const;
    Q_INVOKABLE QVariantMap resumen(const QString& carpeta) const;
    Q_INVOKABLE QVariantList bitacora(const QString& carpeta, const QStringList& tipos, const QString& texto,
                                      int limite) const;
    Q_INVOKABLE QVariantMap grabaciones(const QString& carpeta) const;
    Q_INVOKABLE QVariantList archivos(const QString& carpeta) const;
    /// Trae al evento las grabaciones y transcripciones de su día (ver
    /// evento::Historico::traerDelDia). {grabaciones, transcripciones, desde, hasta} o {error}.
    Q_INVOKABLE QVariantMap traerDelDia(const QString& carpeta, bool mover);
    json traerDelDiaJson(const std::string& carpeta, bool mover, std::int64_t desdeMs = 0, std::int64_t hastaMs = 0);

    /// La foto de Config/ de un evento: [{archivo, bytes}] (foto: inicio | cierre | ultima).
    Q_INVOKABLE QVariantList config(const QString& carpeta, const QString& foto) const;
    Q_INVOKABLE QString contenidoConfig(const QString& carpeta, const QString& foto, const QString& archivo) const;
    /// Qué cambió: contra otro evento, o contra la configuración de ahora (`otro` vacío y `otraFoto` "actual").
    Q_INVOKABLE QVariantList comparar(const QString& carpeta, const QString& foto, const QString& otro,
                                      const QString& otraFoto) const;
    /// Prepara volver a una foto (se aplica al reiniciar). Vacío = todos los archivos. "" o el error.
    Q_INVOKABLE QString prepararRestauracion(const QString& carpeta, const QString& foto, const QStringList& archivos);
    Q_INVOKABLE void cancelarRestauracion();
    QVariantMap restauracion() const;

    /// En segundo plano; avisan con su señal.
    Q_INVOKABLE void buscar(const QString& texto);
    Q_INVOKABLE void verificar(const QString& carpeta);
    Q_INVOKABLE void exportar(const QString& carpeta, const QString& destino, bool conGrabaciones);
    bool buscando() const { return buscando_; }

    Q_INVOKABLE void abrirCarpeta(const QString& carpeta, const QString& sub = {});
    Q_INVOKABLE void abrirArchivo(const QString& ruta);
    Q_INVOKABLE QString carpetaDeExportar() const;

    QVariantMap ajustes() const;
    /// RETENCIÓN (Config/historico.json «retencion»: grabaciones_dias, 0 = nunca;
    /// aviso_disco_pct): borra las grabaciones de los eventos terminados hace más
    /// de N días (todo lo demás queda). Con `simular`, solo dice qué borraría.
    json retencion(bool simular);
    Q_INVOKABLE QVariantMap retencionUi(bool simular) { return aQvPublico(retencion(simular)); }
    /// El disco de Eventos/: {libre_gb, total_gb, usado_pct, aviso_pct, lleno}.
    json disco() const;
    Q_INVOKABLE QVariantMap discoUi() const { return aQvPublico(disco()); }
    static QVariantMap aQvPublico(const json& j);
    /// {registrar: {bus, mcp, …}, bus_excluir: [...], evento_automatico}. "" o el error.
    Q_INVOKABLE QString guardarAjustes(const QVariantMap& cambios);

    Q_INVOKABLE void refrescar() { emit cambio(); }

    // Para el MCP (mismas respuestas que la página, en JSON).
    json ajustesJson() const;
    std::string guardarAjustesJson(const json& cambios);
    std::filesystem::path carpetaEvento(const std::string& carpeta) const;

   signals:
    void cambio();
    void ajustesCambio();
    void restauracionCambio();
    void buscandoCambio();
    void resultados(const QString& texto, const QVariantList& lista);
    void verificado(const QString& carpeta, const QVariantMap& resultado);
    void exportado(const QString& carpeta, const QString& error, const QString& copia);

   private:
    EventoViewModel& ev_;
    std::function<void(const json&)> aplicarAjustes_;
    std::function<std::vector<std::filesystem::path>()> carpetasGrabacion_;
    bool buscando_ = false;
    std::shared_ptr<std::atomic<bool>> cancelarBusqueda_;
};

} // namespace ba::app
