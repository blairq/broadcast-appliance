// ============================================================================
// src/app/ui/CorridaViewModel.hpp — la línea de tiempo corriendo, para la vista de conducción
//
// LA VISTA de la conducción (Studio.evento.corridaVm): lo que se ve —el estado, el cue al
// aire y el que sigue, las marcas del reloj, el HOLD, la retoma tras un corte, lo grabado,
// la bitácora— y los botones, que solo convierten y llaman a las órdenes. Las piezas:
//   - el NÚCLEO DE ÓRDENES (ui/ConduccionLinea): la corrida, el cursor del guion de cues y
//     las órdenes del vivo con sus reglas (`linea*`: "" o por qué no). Las usan también la
//     vista de la línea (ui/LineaViewModel) y el MCP, por los mismos nombres de acá;
//   - lo que cada pieza le hace al sistema (ui/EjecucionLinea): escenas, música, video,
//     audio, gráficos, PTZ, DSK, asamblea, playlist…;
//   - las reglas puras (evento/Corrida): el cursor, el reloj, lo público, las salidas.
//
// EL ENSAYO es la misma máquina con la ejecución apagada: se recorre igual, se ven los
// tiempos, pero no se toca lo que sale al aire.
// ============================================================================
#pragma once

#include <functional>
#include <optional>

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include "evento/Corrida.hpp"
#include "ui/CapturaError.hpp"
#include "ui/ConduccionLinea.hpp"

namespace ba::app {

class CorridaViewModel : public QObject {
    Q_OBJECT
    /// El error de una acción de la interfaz (las Q_INVOKABLE void): a quien mira.
    void avisarError(const QString& e) {
        if (!e.isEmpty() && !::ba::app::CapturaError::tomar(e)) emit notify(e, "error");
    }
    void avisarError(const std::string& e) { avisarError(QString::fromStdString(e)); }
    /// "parado" | "corriendo" | "pausado" | "terminado"
    Q_PROPERTY(QString estado READ estado NOTIFY correChanged)
    Q_PROPERTY(bool ensayo READ ensayo WRITE setEnsayo NOTIFY correChanged)
    /// El guion está corriendo DE VERDAD (no en ensayo): los cues ejecutan. NO es estar al
    /// aire: eso es que salga algo (Studio.aire). Correr el guion para probar sin
    /// transmitir no bloquea nada.
    Q_PROPERTY(bool guionCorriendo READ guionCorriendo NOTIFY correChanged)
    Q_PROPERTY(QVariantMap actual READ actual NOTIFY correChanged)
    Q_PROPERTY(QVariantMap siguiente READ siguiente NOTIFY correChanged)
    /// Lo que el reloj necesita y no cambia mientras nada cambie (el cue, la pausa, lo
    /// previsto): con la hora, `relojEn` da el reloj. La vista lo calcula con el Reloj de las
    /// vistas (un solo reloj para toda la interfaz); el modelo avisa solo los cambios de estado.
    Q_PROPERTY(QVariantMap marcasReloj READ marcasReloj NOTIFY relojChanged)
    /// HOLD: el paso al aire no se va solo aunque se cumpla su duración.
    Q_PROPERTY(bool hold READ hold WRITE setHold NOTIFY correChanged)
    /// Tras un corte de luz: {activa, pideDescripcion, descripcion, resumen}.
    Q_PROPERTY(QVariantMap retoma READ retoma NOTIFY correChanged)
    /// Grabar la línea en vivo (L10): el ensayo corre con todo a GO y lo que dura cada paso
    /// queda para guardarlo como duración prevista. Se elige antes de arrancar.
    Q_PROPERTY(bool grabarTiempos READ grabarTiempos WRITE setGrabarTiempos NOTIFY correChanged)
    /// Lo que grabó el último ensayo, sin guardar: {ops, pasos, totalAntesSeg, totalGrabadoSeg}.
    Q_PROPERTY(QVariantMap tiemposGrabados READ tiemposGrabadosUi NOTIFY correChanged)
    /// Lo último que se ejecutó o se intentó, para la vista de conducción.
    Q_PROPERTY(QVariantList bitacora READ bitacora NOTIFY bitacoraChanged)

   public:
    /// Lo que la línea maneja además del guion (ver ui/ConduccionLinea).
    using Sistema = SistemaLinea;
    using Herramienta = ConduccionLinea::Herramienta;
    /// Lo público para la página web del evento (evento/Corrida).
    using Publico = evento::linea::Publico;

    CorridaViewModel(EventoAbierto& abierto, GuionViewModel& guion, AppRuntime& rt, VentanillaMotor& ventanilla,
                     ConferenceViewModel* sala, graficos::Emisor* graficos, QObject* parent = nullptr);
    ~CorridaViewModel() override;

    /// El núcleo de órdenes (las mismas que reenvía esta clase).
    ConduccionLinea& nucleo() { return n_; }
    const ConduccionLinea& nucleo() const { return n_; }

    void setSistema(std::function<Sistema()> f) { n_.setSistema(std::move(f)); }
    void setHerramienta(Herramienta fn) { n_.setHerramienta(std::move(fn)); }
    void setVinculos(graficos::Vinculos* v) { n_.setVinculos(v); }
    void setCarruseles(CarruselViewModel* c) { n_.setCarruseles(c); }
    void setPonerEnCurso(std::function<void()> f) { n_.setPonerEnCurso(std::move(f)); }
    void setAbrir(std::function<bool(const QString&)> f) { n_.setAbrir(std::move(f)); }

    // --- Lo que se ve ------------------------------------------------------------------
    QString estado() const;
    bool ensayo() const { return n_.ensayo(); }
    void setEnsayo(bool on) { n_.setEnsayo(on); }
    bool guionCorriendo() const;
    QVariantMap actual() const;
    QVariantMap siguiente() const;
    /// {desvioSeg, enCueSeg, restanteSeg, cueNro, cuesTotal, …} ahora (el MCP y el bus), más
    /// lo del avance automático (entraSoloEnSeg, hold, retoma).
    QVariantMap reloj() const;
    QVariantMap marcasReloj() const;
    /// El reloj con las marcas y la hora (ms): una sola cuenta para la vista, el MCP y el bus.
    /// Lo pausado no cuenta (lo resuelve el modelo en las marcas).
    Q_INVOKABLE static QVariantMap relojEn(const QVariantMap& marcas, double ahoraMs);
    int entraSoloEnSeg() const;
    bool hold() const;
    QVariantMap retoma() const;
    QVariantList bitacora() const { return n_.bitacora(); }
    bool grabarTiempos() const { return n_.grabarTiempos(); }
    void setGrabarTiempos(bool on) { n_.setGrabarTiempos(on); }
    QVariantMap tiemposGrabadosUi() const;
    bool corriendo() const { return n_.corriendo(); }
    const evento::linea::Corrida* corrida() const { return n_.corrida(); }
    std::string salidaPorOmision() const { return n_.salidaPorOmision(); }
    std::string horaDe(int64_t ms) const { return n_.horaDe(ms); }
    int64_t horaHoyMs(const std::string& hhmm) const { return n_.horaHoyMs(hhmm); }
    evento::linea::Entrada entradaBase(bool hoy) const { return n_.entradaBase(hoy); }
    Publico publico() const;

    void anotar(const QString& texto, const QString& tipo, bool alRegistro = true) { n_.anotar(texto, tipo, alRegistro); }
    void limpiarBitacora() { n_.limpiarBitacora(); }

    // --- Correr: la página (el error, a `notify`); las órdenes son del núcleo ------------------
    Q_INVOKABLE int arrancarRevisando(bool forzar) { return n_.arrancarRevisando(forzar); }
    /// GO (lineaGo): la pieza pendiente del paso al aire o el paso que sigue; parada, arranca
    /// con la revisión previa.
    Q_INVOKABLE void go(const QString& salida = {}) { avisarError(n_.lineaGo(salida.toStdString())); }
    Q_INVOKABLE void retroceder() { n_.retroceder(); }
    Q_INVOKABLE void irA(int bloque, int cue) { n_.irA(bloque, cue); }
    Q_INVOKABLE void pausar() { n_.pausar(); }
    Q_INVOKABLE void reanudar() { n_.reanudar(); }
    void setHold(bool on) { avisarError(n_.lineaHold({}, on)); }
    Q_INVOKABLE void parar() { n_.parar(); }
    Q_INVOKABLE void ensayoAcelerado() { n_.ensayoAcelerado(); }
    Q_INVOKABLE QString describirRetoma(const QString& texto) { return n_.describirRetoma(texto); }
    Q_INVOKABLE void seguirComoEstaba() { n_.seguirComoEstaba(); }
    Q_INVOKABLE void aplicarTiemposGrabados() { avisarError(n_.aplicarTiemposGrabados()); }
    Q_INVOKABLE void descartarTiemposGrabados() { n_.descartarTiemposGrabados(); }
    Q_INVOKABLE void pulsar(const QString& clave) { avisarError(n_.lineaPulso(clave.toStdString())); }
    Q_INVOKABLE void reenganchar(const QString& salida) { avisarError(n_.lineaReenganchar(salida.toStdString())); }
    Q_INVOKABLE void decidirPtz(const QString& pieza, const QString& decision) {
        avisarError(n_.lineaPtz(pieza.toStdString(), decision.toStdString()));
    }

    void olvidar() { n_.olvidar(); }
    void retomarLinea() { n_.retomarLinea(); }
    void borrarVivo() { n_.borrarVivo(); }
    void enchufar() { n_.enchufar(); }

    // --- La línea, para el MCP (linea_*) y la vista de la línea: el núcleo ------------------
    nlohmann::json lineaEstado() const { return n_.lineaEstado(); }
    nlohmann::json lineaVer(const std::string& salida, int64_t desdeMs, int64_t hastaMs, const std::string& nivel) const {
        return n_.lineaVer(salida, desdeMs, hastaMs, nivel);
    }
    nlohmann::json lineaEditar(const nlohmann::json& ops, bool probar) { return n_.lineaEditar(ops, probar); }
    nlohmann::json lineaDeshacer() { return n_.lineaDeshacer(); }
    nlohmann::json lineaRevisar() const { return n_.lineaRevisar(); }
    nlohmann::json lineaAjustar(const std::string& salida, int64_t objetivoMs) const { return n_.lineaAjustar(salida, objetivoMs); }
    nlohmann::json lineaDesdePlantilla(const nlohmann::json& a, const std::string& salida, const nlohmann::json& guia,
                                       const nlohmann::json& ordenDelDia) {
        return n_.lineaDesdePlantilla(a, salida, guia, ordenDelDia);
    }
    nlohmann::json lineaCamaras(const std::string& camara,
                                const std::function<std::vector<std::string>(const std::string&)>& camarasDeEscena) const {
        return n_.lineaCamaras(camara, camarasDeEscena);
    }
    std::string lineaGo(const std::string& salida) { return n_.lineaGo(salida); }
    std::string lineaHold(const std::string& salida, std::optional<bool> on) { return n_.lineaHold(salida, on); }
    std::string lineaMoverProximo(const std::string& salida, int delta) { return n_.lineaMoverProximo(salida, delta); }
    std::string lineaSaltar(const std::string& salida) { return n_.lineaSaltar(salida); }
    std::string lineaIr(const std::string& salida, const std::string& paso) { return n_.lineaIr(salida, paso); }
    std::string lineaRepetir(const std::string& salida) { return n_.lineaRepetir(salida); }
    std::string lineaAdlib(const std::string& salida, const std::string& pieza) { return n_.lineaAdlib(salida, pieza); }
    std::string lineaInsertar(const std::string& salida, const std::string& item, bool alAire) {
        return n_.lineaInsertar(salida, item, alAire);
    }
    nlohmann::json lineaRecorrido(int ultimos) const { return n_.lineaRecorrido(ultimos); }
    std::string lineaRetomar(const std::string& descripcion, bool simular) { return n_.lineaRetomar(descripcion, simular); }
    std::string lineaSoltar(const std::string& decision) { return n_.lineaSoltar(decision); }
    std::string lineaPulso(const std::string& clave) { return n_.lineaPulso(clave); }
    std::string lineaReenganchar(const std::string& salida) { return n_.lineaReenganchar(salida); }
    std::string lineaPtz(const std::string& pieza, const std::string& decision) { return n_.lineaPtz(pieza, decision); }
    nlohmann::json lineaTiemposGrabados() const { return n_.lineaTiemposGrabados(); }

   signals:
    void correChanged();
    void relojChanged();
    void bitacoraChanged();
    void notify(const QString& texto, const QString& tipo);

   private:
    /// El cue (bloque, c) del guion como lo ve la página, con su bloque y su paso.
    QVariantMap cueEnVista(int bloque, int c) const;

    ConduccionLinea n_;
};

} // namespace ba::app
