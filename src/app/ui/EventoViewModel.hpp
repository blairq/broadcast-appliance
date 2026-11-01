// ============================================================================
// src/app/ui/EventoViewModel.hpp — el evento, para la interfaz (Studio.evento)
//
// La página Evento es varias cosas, y cada una es su view model (con sus propiedades y
// sus avisos):
//
//   lista     EventosListaViewModel   los eventos, el en curso, las carpetas, el automático
//   guion     GuionViewModel          editar el guion, guardar, la revisión, el chequeo
//   padron    PadronViewModel         la gente, las acreditaciones, las invitaciones
//   corrida   CorridaViewModel        la línea viva: EjecutorLinea, HOLD, la retoma
//   web       WebEventoViewModel      la página web del evento
//   informe   InformeEventoViewModel  informe, capítulos, acta, escaleta
//
// Todos leen el mismo EventoAbierto (el guion, la línea y su lectura como cues): una sola
// copia del evento abierto.
//
// LA LÍNEA MANDA. El plan del evento es su línea de tiempo (evento/Linea.hpp); el guion
// de cues que ven la página Guion y `guion_*` es su lectura (proyectar), y editarlo edita
// la línea (reconciliar). Un evento escrito antes de la línea se migra al abrirlo (con una
// copia del v1 en Respaldos/).
//
// ESTO ES LA COSTURA: abrir, cerrar, crear y borrar un evento tocan a todos (en orden), y
// Studio.evento sigue diciendo lo de siempre para la QML y el C++ que lo usan (reexpone lo
// de cada pieza). Lo nuevo se enlaza a la pieza: `Studio.evento.guionVm.opciones`…
// ============================================================================
#pragma once

#include <filesystem>
#include <functional>
#include <optional>

#include <QJsonArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include "evento/Corrida.hpp"
#include "evento/Evento.hpp"
#include "evento/EventoStore.hpp"
#include "evento/LineaEdicion.hpp"
#include "evento/Padron.hpp"
#include "sala/ListaPrompters.hpp"
#include "ui/CorridaViewModel.hpp"
#include "ui/EventoAbierto.hpp"
#include "ui/EventosListaViewModel.hpp"
#include "ui/GuionViewModel.hpp"
#include "ui/InformeEventoViewModel.hpp"
#include "ui/PadronViewModel.hpp"
#include "ui/WebEventoViewModel.hpp"

namespace ba::app {
class CarruselViewModel;
class AppRuntime;
class ConferenceViewModel;
class VentanillaMotor;
namespace graficos {
class Emisor;
class Vinculos;
} // namespace graficos

class EventoViewModel : public QObject {
    Q_OBJECT
    // --- Las piezas ---------------------------------------------------------------------
    Q_PROPERTY(QObject* listaVm READ listaObj CONSTANT)
    Q_PROPERTY(QObject* guionVm READ guionObj CONSTANT)
    Q_PROPERTY(QObject* padronVm READ padronObj CONSTANT)
    Q_PROPERTY(QObject* corridaVm READ corridaObj CONSTANT)
    Q_PROPERTY(QObject* webVm READ webObj CONSTANT)
    Q_PROPERTY(QObject* informeVm READ informeObj CONSTANT)

    // --- Lo de siempre (de cada pieza) ----------------------------------------------------
    Q_PROPERTY(QVariantList eventos READ eventos NOTIFY listaChanged)
    Q_PROPERTY(QString abierto READ abierto NOTIFY guionChanged)
    /// El evento EN CURSO: el que recibe todo lo que pasa (no es el abierto para editar).
    /// {carpeta, nombre, grupo, desdeMs, automatico}; vacío si no hay ninguno.
    Q_PROPERTY(QVariantMap enCurso READ enCurso NOTIFY enCursoChanged)
    Q_PROPERTY(QStringList carpetas READ carpetas NOTIFY listaChanged)
    Q_PROPERTY(bool eventoAutomatico READ eventoAutomatico WRITE setEventoAutomatico NOTIFY enCursoChanged)
    Q_PROPERTY(QVariantMap guion READ guion NOTIFY guionChanged)
    Q_PROPERTY(bool sucio READ sucio NOTIFY sucioChanged)
    Q_PROPERTY(QVariantList reparos READ reparos NOTIFY reparosChanged)
    Q_PROPERTY(QVariantList chequeo READ chequeo NOTIFY chequeoChanged)
    Q_PROPERTY(QVariantList tiposDeCue READ tiposDeCue CONSTANT)
    Q_PROPERTY(QVariantList idiomas READ idiomas CONSTANT)
    Q_PROPERTY(QVariantList escenarios READ escenarios CONSTANT)
    Q_PROPERTY(QString estado READ estado NOTIFY correChanged)
    Q_PROPERTY(bool ensayo READ ensayo WRITE setEnsayo NOTIFY correChanged)
    Q_PROPERTY(bool guionCorriendo READ guionCorriendo NOTIFY correChanged)
    Q_PROPERTY(QVariantMap actual READ actual NOTIFY correChanged)
    Q_PROPERTY(QVariantMap siguiente READ siguiente NOTIFY correChanged)
    Q_PROPERTY(QVariantMap reloj READ reloj NOTIFY relojChanged)
    Q_PROPERTY(int entraSoloEnSeg READ entraSoloEnSeg NOTIFY relojChanged)
    Q_PROPERTY(bool hold READ hold WRITE setHold NOTIFY correChanged)
    Q_PROPERTY(QVariantMap retoma READ retoma NOTIFY correChanged)
    Q_PROPERTY(bool grabarTiempos READ grabarTiempos WRITE setGrabarTiempos NOTIFY correChanged)
    Q_PROPERTY(QVariantMap tiemposGrabados READ tiemposGrabadosUi NOTIFY correChanged)
    Q_PROPERTY(QVariantList bitacora READ bitacora NOTIFY bitacoraChanged)
    Q_PROPERTY(QVariantMap web READ web NOTIFY webChanged)
    Q_PROPERTY(QVariantList padron READ padron NOTIFY padronChanged)
    Q_PROPERTY(QVariantMap resumenPadron READ resumenPadron NOTIFY padronChanged)

   public:
    using Sistema = CorridaViewModel::Sistema;

    /// :param sinAbrirUltimo: no abre solo el último evento que se tocó (el arranque
    ///        anterior se lo llevó puesto; ver diag/DiarioDeArranque.hpp).
    /// `ventanilla`: el motor (del plano de control) se le pide y se lee su foto (F3-03).
    EventoViewModel(AppRuntime& rt, VentanillaMotor& ventanilla, ConferenceViewModel* sala, graficos::Emisor* graficos,
                    bool sinAbrirUltimo = false, QObject* parent = nullptr);
    ~EventoViewModel() override;

    // --- Las piezas, para el C++ ------------------------------------------------------------
    EventosListaViewModel& listaVm() { return *lista_; }
    const EventosListaViewModel& listaVm() const { return *lista_; }
    GuionViewModel& guionVm() { return *guion_; }
    PadronViewModel& padronVm() { return *padron_; }
    CorridaViewModel& corridaVm() { return *corrida_; }
    const CorridaViewModel& corridaVm() const { return *corrida_; }
    WebEventoViewModel& webVm() { return *web_; }
    InformeEventoViewModel& informeVm() { return *informe_; }
    const EventoAbierto& eventoAbierto() const { return *ab_; }

    // --- Lo que se enchufa después (lo pone StudioViewModel) -----------------------------
    void setHerramienta(CorridaViewModel::Herramienta fn) { corrida_->setHerramienta(std::move(fn)); }
    void setChequeosAparato(std::function<QVariantList()> fn) { guion_->setChequeosAparato(std::move(fn)); }
    void setVinculos(graficos::Vinculos* v) { corrida_->setVinculos(v); }
    void setCarruseles(CarruselViewModel* c);
    void setSistema(std::function<Sistema()> f);
    void setPlataformas(std::function<nlohmann::json()> f) { web_->setPlataformas(std::move(f)); }
    void setSala(std::function<nlohmann::json()> f) { web_->setSala(std::move(f)); }
    void setPublico(std::function<nlohmann::json()> preguntas, std::function<int()> enLaSala) {
        web_->setPublico(std::move(preguntas), std::move(enLaSala));
    }
    void setAnexoActa(std::function<QString()> fn) { informe_->setAnexoActa(std::move(fn)); }
    /// Si una ficha de rostro existe (la regla de «entra solo» del padrón).
    void setFichaDe(PadronViewModel::FichaDe f) { padron_->setFichaDe(std::move(f)); }

    // --- Abrir, cerrar, crear, borrar (tocan a todas las piezas) --------------------------
    Q_INVOKABLE QString crear(const QString& nombre, const QString& enCarpeta = {});
    Q_INVOKABLE bool abrir(const QString& carpeta);
    Q_INVOKABLE void cerrar();
    /// Lo borra del disco (en el fondo: la lista se entera al terminar; si falla, `notify`).
    /// Si es el abierto, primero lo cierra SIN guardar.
    Q_INVOKABLE void borrar(const QString& carpeta);
    /// Pone en curso un evento (termina el que estaba).
    Q_INVOKABLE bool iniciarEvento(const QString& carpeta);
    /// Termina el que está en curso. Con el automático prendido, el del día vuelve a quedar en curso.
    Q_INVOKABLE bool terminarEvento();
    /// Lo lleva a otra carpeta (se crea si no está). Devuelve la ruta nueva.
    Q_INVOKABLE QString moverEvento(const QString& carpeta, const QString& aCarpeta);
    /// Crea una carpeta para agrupar ("2026/Octubre"). Devuelve la ruta limpia.
    Q_INVOKABLE QString crearCarpeta(const QString& ruta);
    /// Cambia lugar, etiquetas y notas de la ficha (solo las claves que vengan).
    Q_INVOKABLE bool cambiarDatos(const QString& carpeta, const QVariantMap& campos);
    /// La ficha del histórico de un evento (de la caché de la lista), como mapa.
    QVariantMap datosDe(const QString& carpeta) const;
    /// Que haya uno en curso (el del día si hace falta). Corre cada minuto.
    void asegurarEnCurso() { lista_->asegurarEnCurso(); }

    // --- La lista -------------------------------------------------------------------------
    QVariantList eventos() const { return lista_->eventos(); }
    QStringList carpetas() const { return lista_->carpetas(); }
    QVariantMap enCurso() const { return lista_->enCurso(); }
    bool eventoAutomatico() const { return lista_->eventoAutomatico(); }
    void setEventoAutomatico(bool si) { lista_->setEventoAutomatico(si); }
    /// El bitacora.jsonl del evento en curso (vacío si no hay ninguno).
    std::filesystem::path archivoBitacora() const { return archivoBitacoraDe({}); }
    /// La carpeta de un evento (vacía = el en curso; vacío si no hay).
    std::filesystem::path carpetaEvento(const std::string& carpeta = {}) const;
    std::filesystem::path archivoBitacoraDe(const std::string& carpeta) const;
    /// Eventos/ (donde están todos) y Config/ (lo que se fotografía).
    std::filesystem::path raizEventos() const { return ab_->store().raiz(); }
    std::filesystem::path carpetaConfig() const;
    /// ficha.json de un evento (de la caché).
    evento::DatosDelEvento datosEvento(const std::string& carpeta) const { return lista_->datos(carpeta); }
    /// Config/historico.json (lo que se registra, el automático…), de la caché.
    nlohmann::json ajustesHistorico() const { return lista_->ajustesHistorico(); }

    // --- El guion ---------------------------------------------------------------------------
    QString abierto() const { return ab_->carpeta(); }
    QVariantMap guion() const { return guion_->guion(); }
    bool sucio() const { return ab_->sucio(); }
    QVariantList reparos() const { return guion_->reparos(); }
    QVariantList chequeo() const { return guion_->chequeo(); }
    QVariantList tiposDeCue() const { return guion_->tiposDeCue(); }
    QVariantList idiomas() const { return guion_->idiomas(); }
    QVariantList escenarios() const { return guion_->escenarios(); }
    /// El evento abierto, para quien tenga que leerlo entero. Vacío si no hay ninguno abierto.
    const evento::Evento& guionAbierto() const { return ab_->evento(); }
    /// La carpeta del evento abierto, vacía si no hay ninguno.
    std::filesystem::path carpetaAbierta() const { return ab_->dir(); }
    QString cuando() const { return ab_->cuando(); }
    Q_INVOKABLE static QString husoDelAparato() { return GuionViewModel::husoDelAparato(); }
    Q_INVOKABLE void setNombre(const QString& v) { guion_->setNombre(v); }
    Q_INVOKABLE void setInicio(const QString& v) { guion_->setInicio(v); }
    Q_INVOKABLE void setFecha(const QString& v) { guion_->setFecha(v); }
    Q_INVOKABLE void setHuso(const QString& v) { guion_->setHuso(v); }
    Q_INVOKABLE void setIdioma(const QString& v) { guion_->setIdioma(v); }
    Q_INVOKABLE void setInvitados(bool si) { guion_->setInvitados(si); }
    Q_INVOKABLE void setPreguntas(bool si) { guion_->setPreguntas(si); }
    Q_INVOKABLE void setAsamblea(bool si) { guion_->setAsamblea(si); }
    Q_INVOKABLE QString agregarBloque(const QString& nombre) { return guion_->agregarBloque(nombre); }
    Q_INVOKABLE void renombrarBloque(int b, const QString& nombre) { guion_->renombrarBloque(b, nombre); }
    Q_INVOKABLE void quitarBloque(int b) { guion_->quitarBloque(b); }
    Q_INVOKABLE void moverBloque(int desde, int hasta) { guion_->moverBloque(desde, hasta); }
    Q_INVOKABLE QString agregarCue(int b, const QString& tipo) { return guion_->agregarCue(b, tipo); }
    Q_INVOKABLE void editarCue(int b, int c, const QVariantMap& campos) { guion_->editarCue(b, c, campos); }
    Q_INVOKABLE void quitarCue(int b, int c) { guion_->quitarCue(b, c); }
    Q_INVOKABLE void moverCue(int db, int dc, int ab, int ac) { guion_->moverCue(db, dc, ab, ac); }
    Q_INVOKABLE void setValorDeCue(int b, int c, const QString& clave, const QVariant& v) { guion_->setValorDeCue(b, c, clave, v); }
    Q_INVOKABLE bool guardar() { return guion_->guardar(); }
    Q_INVOKABLE void revisar() { guion_->revisar(); }
    Q_INVOKABLE bool marcarChequeo(const QString& item, bool hecho, const QString& nota, const QString& texto = {}) {
        return guion_->marcarChequeo(item, hecho, nota, texto);
    }
    /// La vista web para ver si un cue anda (el teleprompter); "" si no tiene.
    Q_INVOKABLE QString enlaceDeCue(const QString& tipo, const QVariantMap& datos = {}) const { return guion_->enlaceDeCue(tipo, datos); }
    Q_INVOKABLE QString pedidoDeEscenario(const QString& id, const QString& notas, const QString& plantilla = {}) const {
        return guion_->pedidoDeEscenario(id, notas, plantilla);
    }
    QStringList graficosDisponibles() const { return guion_->graficosDisponibles(); }
    QStringList fuentesDeGrafico() const { return guion_->fuentesDeGrafico(); }
    QVariantList parametrosDeGrafico(const QString& g) const { return guion_->parametrosDeGrafico(g); }
    QVariantList opcionesDeCampo(const QString& tipo, const QString& campo, const QVariantMap& datos = {}) const {
        return guion_->opcionesDeCampo(tipo, campo, datos);
    }
    sala::ListaPrompters listaPrompters() const { return guion_->listaPrompters(); }
    /// La carpeta Discursos/ del evento abierto (la misma que usa el teleprompter).
    QString carpetaDeDiscursos() const { return guion_->carpetaDeDiscursos(); }
    QString rutaDeDiscurso(const QString& rel) const { return guion_->rutaDeDiscurso(rel); }
    /// SOLO PARA MIGRAR: la lista vieja de auto-aceptar (ver PadronViewModel::migrarAutoAceptar).
    bool setAutoAceptar(std::vector<evento::Invitado> lista);

    // --- El padrón ------------------------------------------------------------------------
    QVariantList padron() const { return padron_->padron(); }
    QVariantMap resumenPadron() const { return padron_->resumenPadron(); }
    std::optional<evento::Padron::Votantes> votantes() const { return padron_->votantes(); }
    Q_INVOKABLE QString anotar(const QVariantMap& datos) { return padron_->anotar(datos); }
    Q_INVOKABLE bool cambiarAsistente(const QString& id, const QVariantMap& d) { return padron_->cambiarAsistente(id, d); }
    Q_INVOKABLE bool quitarAsistente(const QString& id) { return padron_->quitarAsistente(id); }
    Q_INVOKABLE bool acreditar(const QString& id, const QString& porDonde) { return padron_->acreditar(id, porDonde); }
    Q_INVOKABLE bool despedir(const QString& id) { return padron_->despedir(id); }
    QString buscarEnPadron(const QString& personaId, const QString& nombre) const { return padron_->buscarEnPadron(personaId, nombre); }
    bool guardarPadronAhora() { return padron_->guardarAhora(); }
    QString registrarDesdeTotem(const QString& personaId, const QString& nombre, const QString& totem, const nlohmann::json& datos) {
        return padron_->registrarDesdeTotem(personaId, nombre, totem, datos);
    }
    bool registradoPorTotem(const QString& personaId) const { return padron_->registradoPorTotem(personaId); }
    bool puedeEntrarSolo(const QString& personaId) const { return padron_->puedeEntrarSolo(personaId); }
    bool acreditarPorPersona(const QString& personaId, const QString& porDonde) {
        return padron_->acreditarPorPersona(personaId, porDonde);
    }
    void pedirInvitacion(const QVariantMap& quien, bool remoto, std::function<void(const QVariantMap&)> luego) {
        padron_->pedirInvitacion(quien, remoto, std::move(luego));
    }

    // --- Correr -----------------------------------------------------------------------------
    QString estado() const { return corrida_->estado(); }
    bool ensayo() const { return corrida_->ensayo(); }
    void setEnsayo(bool on) { corrida_->setEnsayo(on); }
    bool guionCorriendo() const { return corrida_->guionCorriendo(); }
    QVariantMap actual() const { return corrida_->actual(); }
    QVariantMap siguiente() const { return corrida_->siguiente(); }
    QVariantMap reloj() const { return corrida_->reloj(); }
    int entraSoloEnSeg() const { return corrida_->entraSoloEnSeg(); }
    bool hold() const { return corrida_->hold(); }
    void setHold(bool on) { corrida_->setHold(on); }
    QVariantMap retoma() const { return corrida_->retoma(); }
    QVariantList bitacora() const { return corrida_->bitacora(); }
    bool grabarTiempos() const { return corrida_->grabarTiempos(); }
    void setGrabarTiempos(bool on) { corrida_->setGrabarTiempos(on); }
    QVariantMap tiemposGrabadosUi() const { return corrida_->tiemposGrabadosUi(); }
    Q_INVOKABLE void arrancar() { corrida_->arrancar(); }
    Q_INVOKABLE int arrancarRevisando(bool forzar) { return corrida_->arrancarRevisando(forzar); }
    Q_INVOKABLE void avanzar() { corrida_->avanzar(); }
    Q_INVOKABLE void retroceder() { corrida_->retroceder(); }
    Q_INVOKABLE void irA(int b, int c) { corrida_->irA(b, c); }
    Q_INVOKABLE void pausar() { corrida_->pausar(); }
    Q_INVOKABLE void reanudar() { corrida_->reanudar(); }
    Q_INVOKABLE void alternarHold() { corrida_->alternarHold(); }
    Q_INVOKABLE void parar() { corrida_->parar(); }
    Q_INVOKABLE void ensayoAcelerado() { corrida_->ensayoAcelerado(); }
    Q_INVOKABLE QString describirRetoma(const QString& t) { return corrida_->describirRetoma(t); }
    Q_INVOKABLE void seguirComoEstaba() { corrida_->seguirComoEstaba(); }
    Q_INVOKABLE QString aplicarTiemposGrabados() { return corrida_->aplicarTiemposGrabados(); }
    Q_INVOKABLE void descartarTiemposGrabados() { corrida_->descartarTiemposGrabados(); }
    Q_INVOKABLE QString pulsar(const QString& clave) { return corrida_->pulsar(clave); }
    Q_INVOKABLE QString reenganchar(const QString& salida) { return corrida_->reenganchar(salida); }
    Q_INVOKABLE QString decidirPtz(const QString& pieza, const QString& d) { return corrida_->decidirPtz(pieza, d); }

    // --- La línea de tiempo, para el MCP (linea_*) y LineaViewModel ------------------------
    const evento::linea::Linea& linea() const { return ab_->linea(); }
    std::string salidaPorOmision() const { return corrida_->salidaPorOmision(); }
    std::string horaDe(int64_t ms) const { return corrida_->horaDe(ms); }
    int64_t horaHoyMs(const std::string& hhmm) const { return corrida_->horaHoyMs(hhmm); }
    const evento::linea::Corrida* corridaLinea() const { return corrida_->corrida(); }
    graficos::Emisor* emisorGraficos() const { return graficos_; }
    evento::linea::Entrada entradaLinea(bool hoy) const { return corrida_->entradaBase(hoy); }
    nlohmann::json lineaEstado() const { return corrida_->lineaEstado(); }
    nlohmann::json lineaVer(const std::string& s, int64_t d, int64_t h, const std::string& n) const { return corrida_->lineaVer(s, d, h, n); }
    nlohmann::json lineaEditar(const nlohmann::json& ops, bool probar) { return corrida_->lineaEditar(ops, probar); }
    nlohmann::json lineaDeshacer() { return corrida_->lineaDeshacer(); }
    nlohmann::json lineaRevisar() const { return corrida_->lineaRevisar(); }
    nlohmann::json lineaAjustar(const std::string& s, int64_t objetivoMs) const { return corrida_->lineaAjustar(s, objetivoMs); }
    std::string lineaGo(const std::string& s) { return corrida_->lineaGo(s); }
    std::string lineaHold(const std::string& s, std::optional<bool> on) { return corrida_->lineaHold(s, on); }
    std::string lineaMoverProximo(const std::string& s, int delta) { return corrida_->lineaMoverProximo(s, delta); }
    std::string lineaSaltar(const std::string& s) { return corrida_->lineaSaltar(s); }
    std::string lineaIr(const std::string& s, const std::string& paso) { return corrida_->lineaIr(s, paso); }
    std::string lineaRepetir(const std::string& s) { return corrida_->lineaRepetir(s); }
    std::string lineaAdlib(const std::string& s, const std::string& pieza) { return corrida_->lineaAdlib(s, pieza); }
    std::string lineaInsertar(const std::string& s, const std::string& item, bool alAire) { return corrida_->lineaInsertar(s, item, alAire); }
    nlohmann::json lineaRecorrido(int ultimos) const { return corrida_->lineaRecorrido(ultimos); }
    std::string lineaRetomar(const std::string& d, bool simular) { return corrida_->lineaRetomar(d, simular); }
    std::string lineaSoltar(const std::string& d) { return corrida_->lineaSoltar(d); }
    std::string lineaPulso(const std::string& clave) { return corrida_->lineaPulso(clave); }
    std::string lineaReenganchar(const std::string& s) { return corrida_->lineaReenganchar(s); }
    std::string lineaPtz(const std::string& pieza, const std::string& d) { return corrida_->lineaPtz(pieza, d); }
    nlohmann::json lineaTiemposGrabados() const { return corrida_->lineaTiemposGrabados(); }

    // --- El informe, el acta, la escaleta --------------------------------------------------
    Q_INVOKABLE QVariantMap informe() const { return informe_->informe(); }
    Q_INVOKABLE QString guardarInforme() { return informe_->guardarInforme(); }
    QVariantMap capitulos(qint64 recInicioMs) const { return informe_->capitulos(recInicioMs); }
    QString acta(const QJsonArray& votaciones) const { return informe_->acta(votaciones); }
    QString guardarActa(const QString& texto) const { return informe_->guardarActa(texto); }
    static QString hashActa(const QString& texto) { return InformeEventoViewModel::hashActa(texto); }
    std::string lineaExportar(const std::string& f) const { return informe_->lineaExportar(f); }
    nlohmann::json lineaImportar(const std::string& f, const std::string& t, bool probar, const std::string& nb = {}) {
        return informe_->lineaImportar(f, t, probar, nb);
    }
    Q_INVOKABLE QString exportarEscaleta(const QString& f, const QString& ruta) { return informe_->exportarEscaleta(f, ruta); }
    Q_INVOKABLE QVariantMap importarEscaleta(const QString& ruta, bool probar) { return informe_->importarEscaleta(ruta, probar); }

    // --- La página web del evento ------------------------------------------------------------
    QVariantMap web() const { return web_->web(); }
    Q_INVOKABLE QVariantMap generarWeb(bool sobrescribir = false) { return web_->generarWeb(sobrescribir); }
    Q_INVOKABLE QString setWebSitio(const QVariantMap& campos) { return web_->setWebSitio(campos); }
    QString leerPaginaWeb(const QString& pagina) const { return web_->leerPaginaWeb(pagina); }
    Q_INVOKABLE QString escribirPaginaWeb(const QString& p, const QString& c) { return web_->escribirPaginaWeb(p, c); }
    Q_INVOKABLE void publicarWeb() { web_->publicarWeb(); }
    Q_INVOKABLE void setAvisoWeb(const QString& texto, const QString& nivel = "info") { web_->setAvisoWeb(texto, nivel); }
    QVariantMap avisoWeb() const { return web_->avisoWeb(); }

   signals:
    void listaChanged();
    void guionChanged();
    void sucioChanged();
    void correChanged();
    void relojChanged();
    void bitacoraChanged();
    void reparosChanged();
    void chequeoChanged();
    void padronChanged();
    void enCursoChanged();
    void notify(const QString& texto, const QString& tipo);
    void webChanged();
    /// Se puso en curso un evento con sitio y «dirección al iniciar»: la dirección pública
    /// tiene que pasar a ser la de su subdominio (lo hace StudioViewModel).
    void pedirDireccion(const QString& subdominio);

   private:
    QObject* listaObj() const { return lista_; }
    QObject* guionObj() const { return guion_; }
    QObject* padronObj() const { return padron_; }
    QObject* corridaObj() const { return corrida_; }
    QObject* webObj() const { return web_; }
    QObject* informeObj() const { return informe_; }

    AppRuntime& rt_;
    graficos::Emisor* graficos_ = nullptr;
    // Hijos de este QObject: se van con él.
    EventoAbierto* ab_;
    EventosListaViewModel* lista_;
    GuionViewModel* guion_;
    PadronViewModel* padron_;
    CorridaViewModel* corrida_;
    WebEventoViewModel* web_;
    InformeEventoViewModel* informe_;
    QTimer vigiaEnCurso_; ///< cada minuto: el cambio de día y el evento automático
};

} // namespace ba::app
