// ============================================================================
// src/app/ui/HistoricoViewModel.hpp — la página Histórico (H6 de docs/planes/PLAN-EVENTOS-HISTORICO.md)
//
// El archivo de todos los eventos: carpetas, la lista, y el detalle de cada
// uno (resumen, línea de tiempo, grabaciones y vivos, configuración, archivos),
// la búsqueda en todo, comparar y volver a una configuración, exportar y los
// ajustes de qué se registra. La página Evento sigue siendo preparar y correr;
// esto es lo que queda.
//
// Lee con HistoricoConsulta: lo mismo que las herramientas del MCP (evento_* e
// historico_*), con los mismos nombres de evento («@<configuración>/<carpeta>» para los
// de otra configuración).
//
// LA VISTA ELIGE, EL MODELO LEE Y AVISA. La página dice qué quiere ver (`grupo`,
// `filtro`, `seleccion`, `pestana` y los filtros de cada pestaña) y lee propiedades:
// `eventos`, `resumen`, `lineas`, `grabaciones`, `archivosConfig`, `contenidoConfig`,
// `archivos`, `cambios`, `disco`. Todo se lee del disco en un hilo de trabajo y avisa al
// llegar; se vuelve a leer cuando cambia lo elegido, la lista de eventos (crear, mover,
// traer lo del día…) o la bitácora del evento elegido (mientras se mira su línea de
// tiempo). Nada de «version» ni de botón para releer.
// ============================================================================
#pragma once

#include "ui/HistoricoConsulta.hpp"

#include <QFileSystemWatcher>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThreadPool>
#include <QTimer>
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

    // --- Lo que elige la vista ---------------------------------------------------------
    /// "" = todos; "@<evento>" = los de esa configuración; "@<evento>/<sub>" o "<sub>" (la abierta).
    Q_PROPERTY(QString grupo READ grupo WRITE setGrupo NOTIFY eleccionCambio)
    /// Por nombre o carpeta.
    Q_PROPERTY(QString filtro READ filtro WRITE setFiltro NOTIFY eleccionCambio)
    /// El evento elegido (su carpeta; «@…» si es de otra configuración).
    Q_PROPERTY(QString seleccion READ seleccion WRITE setSeleccion NOTIFY eleccionCambio)
    /// resumen | tiempo | grabaciones | config | archivos: solo se lee lo de la que se ve.
    Q_PROPERTY(QString pestana READ pestana WRITE setPestana NOTIFY eleccionCambio)
    /// Los filtros de la línea de tiempo y la foto/archivo de la configuración.
    Q_PROPERTY(QStringList tiposTiempo READ tiposTiempo WRITE setTiposTiempo NOTIFY eleccionCambio)
    Q_PROPERTY(QString textoTiempo READ textoTiempo WRITE setTextoTiempo NOTIFY eleccionCambio)
    Q_PROPERTY(QString fotoConfig READ fotoConfig WRITE setFotoConfig NOTIFY eleccionCambio)
    Q_PROPERTY(QString archivoConfig READ archivoConfig WRITE setArchivoConfig NOTIFY eleccionCambio)

    // --- Lo leído (avisa al llegar) ----------------------------------------------------
    /// Los eventos del grupo con el filtro, con su resumen. Lo más nuevo primero.
    Q_PROPERTY(QVariantList eventos READ eventos NOTIFY eventosCambio)
    Q_PROPERTY(QVariantMap resumen READ resumen NOTIFY detalleCambio)
    Q_PROPERTY(QVariantList lineas READ lineas NOTIFY detalleCambio)
    Q_PROPERTY(QVariantMap grabaciones READ grabaciones NOTIFY detalleCambio)
    Q_PROPERTY(QVariantList archivosConfig READ archivosConfig NOTIFY detalleCambio)
    Q_PROPERTY(QString contenidoConfig READ contenidoConfig NOTIFY detalleCambio)
    Q_PROPERTY(QVariantList archivos READ archivos NOTIFY detalleCambio)
    /// Lo que dio «comparar con ahora» (null: no se comparó la foto que se ve).
    Q_PROPERTY(QVariant cambios READ cambios NOTIFY detalleCambio)
    /// El disco de Eventos/: {libre_gb, total_gb, usado_pct, aviso_pct, lleno} (ver medirDisco).
    Q_PROPERTY(QVariantMap disco READ discoUi NOTIFY discoCambio)

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

    QString grupo() const { return grupo_; }
    void setGrupo(const QString& g);
    QString filtro() const { return filtro_; }
    void setFiltro(const QString& f);
    QString seleccion() const { return sel_; }
    void setSeleccion(const QString& s);
    QString pestana() const { return pestana_; }
    void setPestana(const QString& p);
    QStringList tiposTiempo() const { return tipos_; }
    void setTiposTiempo(const QStringList& t);
    QString textoTiempo() const { return textoTiempo_; }
    void setTextoTiempo(const QString& t);
    QString fotoConfig() const { return foto_; }
    void setFotoConfig(const QString& f);
    QString archivoConfig() const { return archivoConfig_; }
    void setArchivoConfig(const QString& a);

    QVariantList eventos() const { return eventos_; }
    QVariantMap resumen() const { return resumen_; }
    QVariantList lineas() const { return lineas_; }
    QVariantMap grabaciones() const { return grabaciones_; }
    QVariantList archivosConfig() const { return archivosConfig_; }
    QString contenidoConfig() const { return contenidoConfig_; }
    QVariantList archivos() const { return archivos_; }
    QVariant cambios() const { return cambios_; }

    /// «Comparar con ahora»: la foto que se ve contra la configuración de ahora (en el
    /// fondo; llega en `cambios`).
    Q_INVOKABLE void comparar();

    /// Trae al evento las grabaciones y transcripciones de su día (ver
    /// evento::Historico::traerDelDia). En el fondo: avisa con `traido`.
    Q_INVOKABLE void traerDelDia(const QString& carpeta, bool mover);
    /// Lo mismo, para el MCP: {grabaciones, transcripciones, desde, hasta} o {error}.
    json traerDelDiaJson(const std::string& carpeta, bool mover, std::int64_t desdeMs = 0, std::int64_t hastaMs = 0);

    /// Prepara volver a una foto (se aplica al reiniciar). Vacío = todos los archivos. "" o el error.
    Q_INVOKABLE QString prepararRestauracion(const QString& carpeta, const QString& foto, const QStringList& archivos);
    Q_INVOKABLE void cancelarRestauracion();
    QVariantMap restauracion() const { return restauracion_; }

    /// En segundo plano; avisan con su señal.
    Q_INVOKABLE void buscar(const QString& texto);
    Q_INVOKABLE void verificar(const QString& carpeta);
    Q_INVOKABLE void exportar(const QString& carpeta, const QString& destino, bool conGrabaciones);
    /// «¿Qué borraría?»: la retención simulada, en el fondo (avisa con `retencionSimulada`).
    Q_INVOKABLE void simularRetencion();
    /// Vuelve a medir el disco (en el fondo; llega en `disco`).
    Q_INVOKABLE void medirDisco();
    bool buscando() const { return buscando_; }

    Q_INVOKABLE void abrirCarpeta(const QString& carpeta, const QString& sub = {});
    Q_INVOKABLE void abrirArchivo(const QString& ruta);
    Q_INVOKABLE QString carpetaDeExportar() const;

    QVariantMap ajustes() const;
    /// RETENCIÓN (Config/historico.json «retencion»: grabaciones_dias, 0 = nunca;
    /// aviso_disco_pct): borra las grabaciones de los eventos terminados hace más
    /// de N días (todo lo demás queda). Con `simular`, solo dice qué borraría.
    json retencion(bool simular);
    /// El disco de Eventos/: {libre_gb, total_gb, usado_pct, aviso_pct, lleno}.
    json disco() const;
    QVariantMap discoUi() const { return disco_; }
    static QVariantMap aQvPublico(const json& j);
    /// {registrar: {bus, mcp, …}, bus_excluir: [...], evento_automatico}. "" o el error.
    Q_INVOKABLE QString guardarAjustes(const QVariantMap& cambios);

    // Para el MCP (mismas respuestas que la página, en JSON).
    json ajustesJson() const;
    std::string guardarAjustesJson(const json& cambios);
    std::filesystem::path carpetaEvento(const std::string& carpeta) const;
    /// Lo que lee el histórico (un valor: se copia al hilo de trabajo o al del MCP).
    HistoricoConsulta consulta() const;

   signals:
    void cambio();
    void ajustesCambio();
    void restauracionCambio();
    void buscandoCambio();
    void eleccionCambio();
    void eventosCambio();
    void detalleCambio();
    void discoCambio();
    void resultados(const QString& texto, const QVariantList& lista);
    void verificado(const QString& carpeta, const QVariantMap& resultado);
    void exportado(const QString& carpeta, const QString& error, const QString& copia);
    void traido(const QString& carpeta, const QVariantMap& resultado);
    void retencionSimulada(const QVariantMap& resultado);

   private:
    /// Vuelve a leer la lista (en el fondo; lo viejo que llegue tarde se descarta).
    void leerLista();
    /// Vuelve a leer el detalle del elegido para la pestaña que se ve (ídem).
    void leerDetalle();
    /// El archivo de la bitácora del elegido, vigilado mientras se mira su línea de tiempo.
    void vigilarBitacora();
    /// El evento y el rango de traer lo del día (de fábrica, su día). "" o el error.
    std::string delDia(const std::string& carpeta, std::int64_t& desdeMs, std::int64_t& hastaMs, std::filesystem::path& dir) const;

    EventoViewModel& ev_;
    std::function<void(const json&)> aplicarAjustes_;
    std::function<std::vector<std::filesystem::path>()> carpetasGrabacion_;
    bool buscando_ = false;
    std::shared_ptr<std::atomic<bool>> cancelarBusqueda_;

    QString grupo_, filtro_, sel_, pestana_ = "resumen", textoTiempo_, foto_ = "inicio", archivoConfig_;
    QStringList tipos_;
    QVariantList eventos_, lineas_, archivosConfig_, archivos_;
    QVariantMap resumen_, grabaciones_, disco_, restauracion_;
    QString contenidoConfig_;
    QVariant cambios_;

    QThreadPool fondo_; ///< las lecturas de la vista (un hilo; se espera al irse)
    int genLista_ = 0, genDetalle_ = 0;
    QTimer juntarLista_, juntarDetalle_; ///< junta los pedidos seguidos (tipear el filtro, la bitácora que crece)
    QFileSystemWatcher vigia_;
};

} // namespace ba::app
