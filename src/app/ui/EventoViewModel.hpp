// ============================================================================
// src/app/ui/EventoViewModel.hpp — el guion del evento, para la interfaz
//
// Le da a QML el guion (`evento.Evento`) y la línea de tiempo corriendo
// (`evento::linea::Corrida`, un EjecutorLinea por salida), y traduce cada pieza a
// lo que hay que hacerle al sistema.
//
// LA LÍNEA MANDA. El plan del evento es su línea de tiempo (evento/Linea.hpp); el
// guion de cues que ven la página Guion y `guion_*` es su lectura (proyectar), y
// editarlo edita la línea (reconciliar). Un evento escrito antes de la línea se
// migra al abrirlo (con una copia del v1 en Respaldos/). Correr el guion de cues
// es correr la línea: «siguiente» dispara la pieza siguiente del paso al aire (lo
// que en el v1 era el cue siguiente) o, si no queda, hace GO.
//
// El modelo de abajo no sabe ejecutar nada a propósito; acá es donde se cierra
// esa costura. Lo importante es que la traducción sea HONESTA: un cue de un
// tipo que todavía no sabemos ejecutar no se hace el que sí — lo dice, queda
// registrado, y el guion sigue. En vivo, un cue que falla en silencio es peor
// que uno que avisa.
//
// EL ENSAYO es la misma máquina con la ejecución apagada: se recorre el guion
// igual, se ven los tiempos, pero no se toca lo que sale al aire.
// ============================================================================
#pragma once
#include <optional>

#include <memory>

#include <QObject>
#include <QString>
#include <QTimer>

#include <functional>
#include <map>

#include <nlohmann/json.hpp>
#include <QJsonArray>
#include <QVariantList>
#include <QVariantMap>

#include "sala/ListaPrompters.hpp"
#include "evento/Corrida.hpp"
#include "evento/Evento.hpp"
#include "evento/LineaEdicion.hpp"
#include "evento/EventoStore.hpp"
#include "evento/Padron.hpp"

namespace ba::app {
class CarruselViewModel;
class EjecutorLinea;

class AppRuntime;
class ConferenceViewModel;
namespace graficos {
class Emisor;
class Vinculos;
} // namespace graficos
namespace escenas {
class Conmutador;
}

class EventoViewModel : public QObject {
    Q_OBJECT
    // --- La lista de eventos --------------------------------------------------
    Q_PROPERTY(QVariantList eventos READ eventos NOTIFY listaChanged)
    Q_PROPERTY(QString abierto READ abierto NOTIFY guionChanged)
    // --- El histórico (docs/planes/PLAN-EVENTOS-HISTORICO.md) -------------------------
    /// El evento EN CURSO: el que recibe todo lo que pasa (no es el abierto para
    /// editar: se puede preparar el de la semana que viene con el de hoy en curso).
    /// {carpeta, nombre, grupo, desdeMs, automatico}; vacío si no hay ninguno.
    Q_PROPERTY(QVariantMap enCurso READ enCurso NOTIFY enCursoChanged)
    /// Las carpetas que agrupan eventos ("2026/Octubre"...).
    Q_PROPERTY(QStringList carpetas READ carpetas NOTIFY listaChanged)
    /// Si se crea solo el evento del día cuando no hay ninguno en curso
    /// (Config/historico.json, `evento_automatico`; de fábrica, sí).
    Q_PROPERTY(bool eventoAutomatico READ eventoAutomatico WRITE setEventoAutomatico NOTIFY enCursoChanged)

    // --- El guion abierto -----------------------------------------------------
    /// {nombre, inicio, nota, duracionSeg, bloques: [{id, nombre, nota,
    /// duracionSeg, offsetSeg, cues: [{...}]}]}. Cada cue trae ademas `falta`
    /// (lo que le impide ejecutarse) para poder marcarlo en la lista.
    Q_PROPERTY(QVariantMap guion READ guion NOTIFY guionChanged)
    Q_PROPERTY(bool sucio READ sucio NOTIFY sucioChanged)

    // --- El recorrido en vivo -------------------------------------------------
    /// "parado" | "corriendo" | "pausado" | "terminado"
    Q_PROPERTY(QString estado READ estado NOTIFY correChanged)
    Q_PROPERTY(bool ensayo READ ensayo WRITE setEnsayo NOTIFY correChanged)
    /// El guion esta corriendo DE VERDAD (no en ensayo): los cues ejecutan. NO es
    /// estar al aire: eso es que salga algo (StudioViewModel::modoVivo / alAire,
    /// el `en_vivo` de status). Correr el guion para probar sin transmitir no
    /// bloquea nada.
    Q_PROPERTY(bool guionCorriendo READ guionCorriendo NOTIFY correChanged)
    Q_PROPERTY(QVariantMap actual READ actual NOTIFY correChanged)
    Q_PROPERTY(QVariantMap siguiente READ siguiente NOTIFY correChanged)
    /// {desvioSeg, enCueSeg, restanteSeg, cueNro, cuesTotal}: el reloj, que
    /// cambia cada segundo (aparte para no redibujar el guion entero).
    Q_PROPERTY(QVariantMap reloj READ reloj NOTIFY relojChanged)
    /// El aviso «entra solo en 0:05»: los segundos que faltan para que el guion pase
    /// solo al siguiente, durante los últimos 5 s; 0 = no hay aviso (ver Corrida::entraSoloEnSeg).
    Q_PROPERTY(int entraSoloEnSeg READ entraSoloEnSeg NOTIFY relojChanged)
    /// HOLD: el paso al aire no se va solo aunque se cumpla su duración (ver
    /// Corrida::setHold). Tras un corte de luz la línea vuelve en HOLD (ver `retoma`).
    Q_PROPERTY(bool hold READ hold WRITE setHold NOTIFY correChanged)
    /// Tras un corte de luz (docs/planes/PLAN-LINEA-DE-TIEMPO.md 3.8): {activa,
    /// pideDescripcion, descripcion, resumen}. Activa: la línea volvió en HOLD y la
    /// conducción pregunta «¿Qué está pasando?».
    Q_PROPERTY(QVariantMap retoma READ retoma NOTIFY correChanged)
    /// Lo ultimo que se ejecuto o se intento, para la vista de conduccion.
    Q_PROPERTY(QVariantList bitacora READ bitacora NOTIFY bitacoraChanged)

    // --- Revision previa ------------------------------------------------------
    Q_PROPERTY(QVariantList reparos READ reparos NOTIFY reparosChanged)
    /// La página web del evento en vivo (evento/EventoWeb.hpp): {hay, carpeta, paginas:[…],
    /// sitio:{titulo, bajada, color, fondo, subdominio, barra, direccion_al_iniciar}}.
    Q_PROPERTY(QVariantMap web READ web NOTIFY webChanged)

   public:
    /// :param sinAbrirUltimo: no abre solo el ultimo evento que se toco. Lo
    ///        usa el arranque cuando ese evento se llevo puesto el arranque
    ///        anterior (ver diag/DiarioDeArranque.hpp).
    EventoViewModel(AppRuntime& rt, ConferenceViewModel* sala, graficos::Emisor* graficos,
                    bool sinAbrirUltimo = false, QObject* parent = nullptr);
    ~EventoViewModel() override;

    QVariantList eventos() const;
    QString abierto() const { return abierto_; }
    /// Los votantes del evento abierto (la regla es de evento::Padron), o nada sin evento.
    std::optional<evento::Padron::Votantes> votantes() const {
        if (abierto_.isEmpty()) return std::nullopt;
        return padron_.votantes();
    }
    QVariantMap enCurso() const;
    /// El bitacora.jsonl del evento en curso (vacío si no hay ninguno).
    std::filesystem::path archivoBitacora() const;
    /// La carpeta de un evento (vacía = el en curso; vacío si no hay).
    std::filesystem::path carpetaEvento(const std::string& carpeta = {}) const;
    /// Eventos/ (donde están todos).
    std::filesystem::path raizEventos() const { return store_.raiz(); }
    /// Config/ (lo que se fotografía).
    std::filesystem::path carpetaConfig() const;
    /// ficha.json de un evento (el histórico).
    evento::DatosDelEvento datosEvento(const std::string& carpeta) const { return store_.datos(carpeta); }
    /// La bitácora de un evento (por su carpeta; vacía = el en curso).
    std::filesystem::path archivoBitacoraDe(const std::string& carpeta) const;
    /// Config/historico.json (lo que se registra, el automático…).
    nlohmann::json ajustesHistorico() const;
    QStringList carpetas() const;
    bool eventoAutomatico() const { return automatico_; }
    void setEventoAutomatico(bool si);
    /// Pone en curso un evento (termina el que estaba). Lo hace solo al correr
    /// el guion de verdad (no en ensayo).
    Q_INVOKABLE bool iniciarEvento(const QString& carpeta);
    /// Termina el que está en curso. Con el automático prendido, el del día
    /// vuelve a quedar en curso (nada pasa sin evento).
    Q_INVOKABLE bool terminarEvento();
    /// Lo lleva a otra carpeta (se crea si no está). Devuelve la ruta nueva.
    Q_INVOKABLE QString moverEvento(const QString& carpeta, const QString& aCarpeta);
    /// Crea una carpeta para agrupar ("2026/Octubre"). Devuelve la ruta limpia.
    Q_INVOKABLE QString crearCarpeta(const QString& ruta);
    /// La ficha del histórico de un evento (ficha.json), como mapa.
    Q_INVOKABLE QVariantMap datosDe(const QString& carpeta) const;
    /// Cambia lugar, etiquetas y notas de la ficha (solo las claves que vengan).
    Q_INVOKABLE bool cambiarDatos(const QString& carpeta, const QVariantMap& campos);
    /// Que haya uno en curso (el del día si hace falta). Corre cada minuto.
    void asegurarEnCurso();
    QVariantMap guion() const;
    bool sucio() const { return sucio_; }
    QString estado() const;
    bool ensayo() const { return ensayo_; }
    bool guionCorriendo() const;
    QVariantMap actual() const;
    QVariantMap siguiente() const;
    QVariantMap reloj() const;
    int entraSoloEnSeg() const;
    bool hold() const;
    QVariantMap retoma() const;
    QVariantList bitacora() const { return bitacora_; }
    QVariantList reparos() const;

    // --- La lista -------------------------------------------------------------
    Q_INVOKABLE void refrescar();
    /// Cómo el guion llama una herramienta del servidor (p. ej. pantalla_mostrar).
    void setHerramienta(std::function<void(const std::string&, const nlohmann::json&,
                                           std::function<void(bool, const std::string&)>)> fn) {
        herramienta_ = std::move(fn);
    }
    /// Lo que la revisión mira del aparato (disco, destino, entradas, pantallas):
    /// [{grave, texto}]. Lo pone la app.
    void setChequeosAparato(std::function<QVariantList()> fn) { chequeosAparato_ = std::move(fn); }
    /// La lista de chequeo manual del evento abierto (Eventos/<id>/chequeo.json):
    /// [{item, texto, hecho, nota, cuando}].
    Q_INVOKABLE QVariantList chequeo() const;
    /// Marca un ítem (o lo agrega si no existe). false si no hay evento abierto.
    /// `texto`: lo que ve la persona, para un ítem nuevo (de fábrica, el nombre del ítem).
    Q_INVOKABLE bool marcarChequeo(const QString& item, bool hecho, const QString& nota, const QString& texto = {});
    Q_INVOKABLE QString crear(const QString& nombre, const QString& enCarpeta = {});
    Q_INVOKABLE bool abrir(const QString& carpeta);
    Q_INVOKABLE void cerrar();
    /// Lo borra del disco. Si es el abierto, primero lo cierra SIN guardar
    /// (se tira entero: guardarlo para borrarlo no tiene sentido).
    Q_INVOKABLE bool borrar(const QString& carpeta);

    // --- Editar el guion ------------------------------------------------------
    Q_INVOKABLE void setNombre(const QString& nombre);
    Q_INVOKABLE void setInicio(const QString& hhmm);
    Q_INVOKABLE void setFecha(const QString& aaaammdd);
    Q_INVOKABLE void setHuso(const QString& iana);
    /// El idioma del evento ("" = el del operador) y los que se pueden elegir
    /// ([{value, label}], el primero "").
    Q_INVOKABLE void setIdioma(const QString& codigo);
    Q_INVOKABLE QVariantList idiomas() const;
    /// Lo que usa el evento: invitados en la sala; preguntas y votos del público.
    Q_INVOKABLE void setInvitados(bool si);
    Q_INVOKABLE void setPreguntas(bool si);
    Q_INVOKABLE void setAsamblea(bool si);
    /// El arranque como instante absoluto, ISO-8601 con desfasaje
    /// ("2026-09-24T19:00:00-03:00"). Vacio si falta la fecha o la hora.
    ///
    /// Es lo unico que le sirve al que esta en otro huso: con eso su telefono
    /// le dice que hora es para el.
    Q_INVOKABLE QString cuando() const;
    /// El huso del aparato ahora mismo, en nombre IANA. Para proponerlo como
    /// valor por defecto al crear un evento.
    Q_INVOKABLE static QString husoDelAparato();
    Q_INVOKABLE QString agregarBloque(const QString& nombre);
    Q_INVOKABLE void renombrarBloque(int bloque, const QString& nombre);
    Q_INVOKABLE void quitarBloque(int bloque);
    Q_INVOKABLE void moverBloque(int desde, int hasta);
    /// `tipo` es el nombre del tipo ("escena", "orador"...).
    Q_INVOKABLE QString agregarCue(int bloque, const QString& tipo);
    Q_INVOKABLE void editarCue(int bloque, int cue, const QVariantMap& campos);
    Q_INVOKABLE void quitarCue(int bloque, int cue);
    /// Mueve un cue, incluso a otro bloque (arrastrar y soltar).
    Q_INVOKABLE void moverCue(int deBloque, int deCue, int aBloque, int aCue);
    Q_INVOKABLE bool guardar();

    // --- El padron: la gente de este evento ---------------------------------
    // Vive en la misma carpeta pero en su propio archivo, y con otro ritmo: el
    // guion se escribe antes y se guarda cuando uno quiere; el padron cambia
    // durante el evento —cada persona que entra es un cambio— y se guarda
    // SOLO, en el momento. Por eso no tiene "sin guardar": en la puerta, con
    // cola, nadie va a acordarse de apretar guardar.
    /// [{id, personaId, nombre, papel, canal, estado, contacto, nota,
    ///   dejarPasarSolo, vota, codigo, llegoMs, porDonde, registro, datos}]
    ///
    /// PROPIEDAD y no método: como método, QML recibe la función en vez de la
    /// lista —y no se entera cuando el padrón cambia—. Se vio en la vista: la
    /// lista salía vacía y el contador de la pestaña no se movía.
    Q_INVOKABLE QVariantList padron() const;
    /// {total, presentes, presenciales, enLinea, confirmados, faltan,
    ///  conVoto, votoPresente}
    Q_INVOKABLE QVariantMap resumenPadron() const;
    /// Devuelve el id del anotado, o vacio si no hay evento abierto.
    Q_INVOKABLE QString anotar(const QVariantMap& datos);
    /// Cambia solo las claves que vengan en el mapa.
    Q_INVOKABLE bool cambiarAsistente(const QString& id, const QVariantMap& datos);
    Q_INVOKABLE bool quitarAsistente(const QString& id);
    /// Marca que llego. `porDonde`: "rostro", "qr", "mostrador", "lobby".
    Q_INVOKABLE bool acreditar(const QString& id, const QString& porDonde);
    Q_INVOKABLE bool despedir(const QString& id);
    /// Busca a alguien por su ficha de rostro o por el nombre tipeado.
    /// Devuelve el id, o vacio si no esta.
    Q_INVOKABLE QString buscarEnPadron(const QString& personaId, const QString& nombre) const;
    /// Escribe el padron al disco. Lo llaman los de arriba; no hace falta
    /// desde afuera porque cada cambio ya se guarda solo.
    bool guardarPadronAhora();
    /// La carpeta del evento abierto (la del padrón), vacía si no hay ninguno.
    std::filesystem::path carpetaAbierta() const;
    /// Lo que deja un tótem de registro (ui/RegistroViewModel): busca a la
    /// persona por su ficha (o por el nombre, si ya estaba en la lista), le
    /// suma `datos` y el tótem, o la anota; y la acredita («registro»).
    /// Devuelve el id en el padrón, o vacío sin evento abierto.
    QString registrarDesdeTotem(const QString& personaId, const QString& nombre, const QString& totem,
                                const nlohmann::json& datos);
    /// Si esa ficha ya se registró por un tótem en este evento.
    bool registradoPorTotem(const QString& personaId) const;
    /// Si esa ficha de rostro tiene permiso para entrar sola a la sala.
    Q_INVOKABLE bool puedeEntrarSolo(const QString& personaId) const;
    /// Acredita por ficha de rostro. false si no esta en el padron.
    Q_INVOKABLE bool acreditarPorPersona(const QString& personaId, const QString& porDonde);

    /// El evento abierto, para quien tenga que leerlo entero (la lista de
    /// auto-aceptar del lobby). Vacío si no hay ninguno abierto.
    const evento::Evento& guionAbierto() const { return e_; }
    /// Cambia la lista de auto-aceptar y guarda. Ver `evento::Invitado`.
    ///
    /// SOLO PARA MIGRAR. El permiso de entrar solo vive en el padrón
    /// (`Asistente::dejarPasarSolo`); esto queda para poder leer los eventos
    /// escritos antes y volcarlos. Ver `migrarAutoAceptar`.
    bool setAutoAceptar(std::vector<evento::Invitado> lista);

    /// Lo que se puede poner en cada tipo de cue: para armar el formulario sin
    /// que QML tenga que saberse la lista de memoria.
    Q_INVOKABLE QVariantList tiposDeCue() const;

    // --- Cues de grafico ------------------------------------------------------
    /// Los graficos guardados, por nombre.
    Q_INVOKABLE QStringList graficosDisponibles() const;
    /// Las fuentes «Grafico animado» que hay en las escenas, por nombre.
    Q_INVOKABLE QStringList fuentesDeGrafico() const;
    /// Los parametros que declara ese grafico, para armar el formulario del cue.
    Q_INVOKABLE QVariantList parametrosDeGrafico(const QString& grafico) const;
    /// Las opciones para el campo `campo` de un cue de tipo `tipo`, para elegirlas en
    /// vez de escribirlas: [{valor, texto}] (las escenas, los medios del evento, los
    /// discursos, los gráficos y sus fuentes, las fuentes de audio, quién está en la
    /// sala, las acciones de cada tipo). Vacía = se escribe (o es un sí/no o un número).
    /// `datos`: los del cue (las láminas dependen del carrusel elegido en ese mismo cue).
    Q_INVOKABLE QVariantList opcionesDeCampo(const QString& tipo, const QString& campo,
                                             const QVariantMap& datos = {}) const;
    /// La vista web para ver si un cue anda (el teleprompter: la pantalla del orador,
    /// que avanza sola), en esta máquina; "" si ese tipo no tiene vista.
    Q_INVOKABLE QString enlaceDeCue(const QString& tipo, const QVariantMap& datos = {}) const;
    /// Los teleprompters del evento con su nombre (<evento>/prompters.json; ver sala/ListaPrompters).
    sala::ListaPrompters listaPrompters() const;
    /// Los tipos de evento con guía para prepararlos con el asistente (assets/idiomas/<c>/guias/escenarios,
    /// en el idioma del operador):
    /// [{id, titulo, icono, resumen}].
    Q_INVOKABLE QVariantList escenarios() const;
    /// El pedido para el asistente: preparar un evento de ese tipo siguiendo su guía
    /// (evento_escenario), con lo que el operador ya contó en `notas`.
    /// `plantilla`: la carpeta de la plantilla de ese tipo ("" = desde cero).
    Q_INVOKABLE QString pedidoDeEscenario(const QString& id, const QString& notas, const QString& plantilla = {}) const;
    /// Cambia UN valor de los parametros de un cue de grafico, sin pisar los
    /// otros (que es lo que pasaria mandando `datos` entero).
    Q_INVOKABLE void setValorDeCue(int bloque, int cue, const QString& clave, const QVariant& valor);
    /// Los vinculos se crean despues que el guion, asi que se enchufan aparte.
    void setVinculos(graficos::Vinculos* v) { vinculos_ = v; }
    /// Los carruseles, para el cue «carrusel» (ver CarruselViewModel::mostrar).
    void setCarruseles(CarruselViewModel* c) { carruseles_ = c; }
    /// La mesa de corte: el cue «escena» se arma en el PVW y entra con TAKE (ver
    /// armarSiguiente). Con varias salidas, por ahora la de la salida por omisión.
    void setConmutador(escenas::Conmutador* c) { conmutador_ = c; }
    /// Lo que se sabe de YouTube, Twitch y Facebook para «dónde verlo en vivo» de la
    /// página web (ver evento::armarVivos). Lo pone StudioViewModel.
    void setPlataformas(std::function<nlohmann::json()> f) { plataformas_ = std::move(f); }
    /// Las preguntas del público ya moderadas y cuántos hay en la sala (lo pone StudioViewModel).
    /// Lo que sabe el resto del sistema para la página (al aire, oradores, orden del día,
    /// quórum, votación): ver evento::ExtrasVivo::sala.
    void setSala(std::function<nlohmann::json()> f) { datosSala_ = std::move(f); }
    void setPublico(std::function<nlohmann::json()> preguntas, std::function<int()> enLaSala) {
        preguntas_ = std::move(preguntas);
        enLaSala_ = std::move(enLaSala);
    }

    // --- Correr ---------------------------------------------------------------
    Q_INVOKABLE void arrancar();
    /// Arrancar con la revisión previa: revisa y, si encontró problemas graves y no
    /// se pide `forzar`, NO arranca y devuelve cuántos son (la GUI pregunta, el MCP
    /// pide forzar). 0 = arrancó.
    Q_INVOKABLE int arrancarRevisando(bool forzar);
    Q_INVOKABLE void avanzar();
    Q_INVOKABLE void retroceder();
    Q_INVOKABLE void irA(int bloque, int cue);
    Q_INVOKABLE void pausar();
    Q_INVOKABLE void reanudar();
    /// HOLD sobre el paso al aire (el botón y la tecla H, y `evento_correr` por MCP).
    /// En una retoma, soltarlo es «seguir como estaba».
    void setHold(bool on);
    Q_INVOKABLE void alternarHold() { setHold(!hold()); }
    Q_INVOKABLE void parar();
    void setEnsayo(bool on);

    // --- Tras un corte de luz (3.8) --------------------------------------------------
    /// «¿Qué está pasando?»: anota la descripción en el recorrido (la línea sigue en
    /// HOLD) y devuelve el pedido para el asistente, con el resumen de la retoma,
    /// para que PROPONGA cómo adecuar la línea (la vista se lo encarga).
    Q_INVOKABLE QString describirRetoma(const QString& texto);
    /// «Seguir como estaba»: suelta el HOLD del corte sin cambiar nada.
    Q_INVOKABLE void seguirComoEstaba();

    // --- La línea de tiempo, para el MCP (linea_*) -------------------------------------
    const evento::linea::Linea& linea() const { return linea_; }
    /// La salida de la línea que sale hoy (la por omisión).
    std::string salidaPorOmision() const { return linea_.salidasEfectivas().front(); }
    /// "HH:MM:SS" en el huso del evento.
    std::string horaDe(int64_t ms) const;
    /// linea_estado: la corrida (o el plan, sin correr) por salida, la retoma y la pausa.
    nlohmann::json lineaEstado() const;
    /// linea_ver: el pronóstico compacto (ver evento::linea::vista). null si la salida no existe.
    nlohmann::json lineaVer(const std::string& salida, int64_t desdeMs, int64_t hastaMs, const std::string& nivel) const;
    /// linea_editar: un lote de operaciones; con `probar` no aplica. Aplicado: deja
    /// la línea anterior en Respaldos/linea-<n>.json (se deshace con lineaDeshacer) y
    /// guarda. {ok, error?, diff, pronostico, reparos, revision?}
    nlohmann::json lineaEditar(const nlohmann::json& ops, bool probar);
    /// Vuelve a la línea de la última revisión. {ok, error?, revision?}
    nlohmann::json lineaDeshacer();
    /// linea_revisar: los reparos propios de la línea (hard starts, anclas, ids,
    /// pistas, conflictos entre salidas), con dónde están.
    nlohmann::json lineaRevisar() const;
    /// linea_ajustar: lo que habría que recortar para terminar a `objetivoMs` (nunca aplica).
    nlohmann::json lineaAjustar(const std::string& salida, int64_t objetivoMs) const;
    /// La hora de hoy (en el huso del evento) en ms; -1 si "HH:MM" no es una hora.
    int64_t horaHoyMs(const std::string& hhmm) const;
    /// Las acciones de vivo. "" o por qué no.
    std::string lineaGo(const std::string& salida);
    std::string lineaHold(const std::string& salida, std::optional<bool> on);
    std::string lineaMoverProximo(const std::string& salida, int delta);
    std::string lineaSaltar(const std::string& salida);
    std::string lineaIr(const std::string& salida, const std::string& paso);
    std::string lineaRepetir(const std::string& salida);
    std::string lineaAdlib(const std::string& salida, const std::string& pieza);
    std::string lineaInsertar(const std::string& salida, const std::string& item, bool alAire);
    /// Lo emitido (Eventos/<x>/recorrido.jsonl): los últimos `ultimos` renglones (0 = todos).
    nlohmann::json lineaRecorrido(int ultimos) const;
    /// linea_retomar: la descripción de qué está pasando (la puede dar el asistente si
    /// la persona se la dictó). `simular`: hace como si se hubiera cortado la luz
    /// recién (para probar el flujo sin apagar nada). "" o por qué no.
    std::string lineaRetomar(const std::string& descripcion, bool simular);
    /// linea_soltar: suelta el HOLD del corte con lo que se decidió. "" o por qué no.
    std::string lineaSoltar(const std::string& decision);
    /// Para la vista de la línea (LineaViewModel, L4/L5): la corrida (la que corre o la
    /// última; nullptr si no corrió) y el arranque previsto, la medianoche y el fin
    /// planeado (del día del evento o, con `hoy`, de hoy).
    const evento::linea::Corrida* corridaLinea() const { return corrida_.get(); }
    evento::linea::Entrada entradaLinea(bool hoy) const { return entradaBase(hoy); }

    /// Ensayo acelerado: recorre el guion ENTERO de una, sin esperar las
    /// duraciones y sin tocar nada, y dice de cada cue si se podria ejecutar.
    /// A diferencia de `revisar`, que mira el guion quieto, esto camina el
    /// recorrido de verdad: encuentra los bloques que quedan colgados, el
    /// orden real y los cues que nadie sabe ejecutar todavia.
    Q_INVOKABLE void ensayoAcelerado();

    /// Lo que realmente paso, para el evento siguiente: por bloque y por cue,
    /// cuanto estaba previsto y cuanto duro de verdad.
    ///
    /// Se arma del recorrido que el guion fue guardando, asi que solo tiene
    /// sentido despues de correrlo (o de ensayarlo). Vacio si no se corrio.
    ///
    /// {previstoSeg, realSeg, desvioSeg, bloques: [{nombre, previstoSeg,
    ///  realSeg, desvioSeg, cues: [{nro, texto, tipo, previstoSeg, realSeg,
    ///  desvioSeg}]}]}
    Q_INVOKABLE QVariantMap informe() const;
    /// Los capítulos de la grabación, uno por bloque, medidos desde que
    /// arrancó a grabar (`recInicioMs`, reloj de pared).
    ///
    /// {ok, motivo, capitulos: [{seg, marca, titulo}], texto, antesDeGrabar}
    ///
    /// UNO POR BLOQUE Y NO POR CUE: cuarenta capítulos de quince segundos no
    /// le sirven a nadie. Los bloques son justamente los tramos con nombre.
    Q_INVOKABLE QVariantMap capitulos(qint64 recInicioMs) const;

    /// El acta: quiénes asistieron, el quórum y qué se votó.
    ///
    /// NO es `informe()`. El informe dice que el bloque 3 se fue cuatro
    /// minutos —es para el operador—; el acta dice quiénes estuvieron y qué se
    /// decidió, que es lo que vale en una asamblea.
    ///
    /// `votaciones` es el historial que guarda la urna (vive en el gateway).
    QString acta(const QJsonArray& votaciones) const;
    /// Lo que se suma al acta de otro lado (orden del día y uso de la palabra).
    void setAnexoActa(std::function<QString()> fn) { anexoActa_ = std::move(fn); }
    /// La escribe en la carpeta del evento. Devuelve la ruta, o vacío.
    QString guardarActa(const QString& texto) const;
    /// El SHA-256 del texto del acta (el que se guarda en acta-….md.sha256).
    static QString hashActa(const QString& texto);

    /// La invitación de una persona del padrón: el texto, el enlace con su
    /// código adentro y los módulos del QR. Va al gateway a pedir el enlace,
    /// así que contesta por `luego`.
    void pedirInvitacion(const QVariantMap& quien, bool remoto,
                         std::function<void(const QVariantMap&)> luego);
    /// Guarda el informe en la carpeta del evento, con la fecha en el nombre.
    /// Devuelve el archivo escrito, o vacio si no se pudo.
    Q_INVOKABLE QString guardarInforme();

    /// Revision previa completa: la del modelo mas lo que necesita el motor
    /// (que las escenas existan, que los archivos esten, que el orador este
    /// conectado). Se corre a pedido, no sola.
    Q_INVOKABLE void revisar();

    // --- La página web del evento en vivo ------------------------------------------
    QVariantMap web() const;
    /// Genera el sitio en Web/ (portada, una página por sección, cierre, estilo). No
    /// pisa lo que ya existe salvo `sobrescribir`. {escritas, salteadas, error}.
    Q_INVOKABLE QVariantMap generarWeb(bool sobrescribir = false);
    /// Cambia sitio.json (título, bajada, color, fondo, subdominio, barra…). "" o el error.
    Q_INVOKABLE QString setWebSitio(const QVariantMap& campos);
    /// Una página por nombre («portada», «cierre», el id/nombre/número de una sección,
    /// o un .html/.css propio): leerla y escribirla. Escribir sin evento abierto: error.
    Q_INVOKABLE QString leerPaginaWeb(const QString& pagina) const;
    Q_INVOKABLE QString escribirPaginaWeb(const QString& pagina, const QString& contenido);
    /// Le manda a la sala lo de la página del evento (lo hace solo al abrir, al
    /// avanzar el guion y cada 10 s; esto es para forzarlo).
    Q_INVOKABLE void publicarWeb();
    /// El aviso que se ve arriba de la página del evento («Arrancamos en 5 minutos»).
    /// `nivel`: info | urgente. Vacío lo saca. No se guarda en el evento (es del momento).
    Q_INVOKABLE void setAvisoWeb(const QString& texto, const QString& nivel = "info");
    Q_INVOKABLE QVariantMap avisoWeb() const;

   public:
    // Las dos de arriba, ademas, como propiedades: es lo que hace que la vista
    // se redibuje sola cuando alguien llega.
    Q_PROPERTY(QVariantList padron READ padron NOTIFY padronChanged)
    Q_PROPERTY(QVariantMap resumenPadron READ resumenPadron NOTIFY padronChanged)

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
    /// Se puso en curso un evento con sitio y «dirección al iniciar»: la dirección
    /// pública tiene que pasar a ser la de su subdominio (lo hace StudioViewModel con
    /// la API key de pinggy).
    void pedirDireccion(const QString& subdominio);

   public:
    /// La carpeta Discursos/ del evento abierto (vacía si no hay ninguno).
    /// La usa el teleprompter, que lee y escribe los mismos archivos que el
    /// cue `teleprompter`: una sola verdad, no dos carpetas parecidas.
    QString carpetaDeDiscursos() const;
    /// La ruta de un discurso por su nombre relativo, o vacía si se sale de
    /// la carpeta (un ".." en un guion que vino de otra máquina).
    QString rutaDeDiscurso(const QString& rel) const { return discursoPathPriv(rel); }

   private:
    std::function<QString()> anexoActa_;
    // --- La línea corriendo -----------------------------------------------------------
    /// La corrida nueva (con un ejecutor por salida), sin arrancarla.
    void nuevaCorrida();
    /// Hace lo que devolvió la corrida (cada acción, por el ejecutor de su salida) y
    /// después: reprograma los timers, escribe el recorrido y el estado vivo, y avisa
    /// a la vista.
    void despachar(const std::vector<evento::linea::Accion>& acc);
    /// Una acción en el aire (la llama el ejecutor de su salida). En ensayo no toca nada.
    void hacerAccion(const evento::linea::Accion& a);
    /// Ejecuta un cue (lo que hace una pieza de un tipo del v1). `clave` la identifica
    /// para poder sacarla después.
    void ejecutarCue(const evento::Cue& c, const std::string& paso, const std::string& clave);
    /// Saca una pieza que se terminó (un gráfico con su salida; la música que puso).
    void sacarPieza(const evento::linea::Accion& a);
    /// El LOOKAHEAD: deja la escena armada en el PVW, así el operador ve lo que va a
    /// entrar y el TAKE es inmediato.
    void armarEscena(const std::string& escena);
    /// Pone al aire la escena de un cue por el PVW: la arma (si el lookahead no la
    /// dejó armada) y hace TAKE con la transición del cue o la de entrada de la
    /// escena. "" o por qué no.
    std::string tomarEscena(const std::string& escena, const nlohmann::json& datos);
    /// Saca (con su salida) los gráficos que puso el guion y «se van al avanzar»
    /// según su ficha (títulos, placas): se llama al entrar otro paso.
    void sacarAlAvanzar();
    /// La línea cambió por el guion de cues (las ediciones de la página y de
    /// `guion_*`): reconcilia, vuelve a leer el guion y se lo pasa a la corrida.
    void lineaDesdeGuion();
    /// La línea cambió por su cuenta (linea_editar, deshacer): el guion de cues se
    /// vuelve a leer y la corrida se entera.
    void guionDesdeLinea();
    /// Arranque previsto, medianoche y fin planeado, en el huso del evento. `hoy`:
    /// del día de hoy (para correr); si no, del día del evento.
    evento::linea::Entrada entradaBase(bool hoy) const;
    /// Runtime/linea-aire.json (el estado vivo, para volver de un corte).
    std::filesystem::path archivoVivo() const;
    void escribirVivo();
    void borrarVivo();
    /// Al arrancar después de un corte: si la línea estaba corriendo, vuelve en HOLD.
    void retomarLinea();
    /// Retoma desde lo que dejó `aJsonVivo`: la corrida nueva vuelve en HOLD.
    void retomarDesde(const nlohmann::json& vivo);
    /// Los renglones nuevos del recorrido, a Eventos/<x>/recorrido.jsonl (no en ensayo).
    void volcarRecorrido();
    std::filesystem::path archivoRecorrido() const;
    /// El pedido para el asistente tras un corte, con la descripción y el resumen.
    QString pedidoDeRetoma(const QString& descripcion) const;
    /// El cue (del guion de cues) de una pieza, para ejecutarla con las reglas de su tipo.
    evento::Cue cueDePieza(const evento::linea::Pieza& z, const std::string& paso) const;
    /// ¿La corrida está andando (corriendo o en pausa)?
    bool corriendo() const;
    // El cursor del guion de cues: el cue que se ve «al aire» (el último disparado del paso).
    struct Cursor {
        int bloque = -1;
        int cue = -1;
        int64_t desdeMs = 0;
        int64_t pausadoAlEntrarMs = 0;
        bool valido() const { return bloque >= 0 && cue >= 0; }
    };
    void moverCursor(int bloque, int cue);
    /// El cue que sigue (o el anterior) en el guion de cues ({-1,-1} si no hay).
    std::pair<int, int> cueSiguienteDe(int bloque, int cue) const;
    std::pair<int, int> cueAnteriorDe(int bloque, int cue) const;
    int enCueSeg(int64_t ahora) const;
    /// A la bitácora de la página y, si `alRegistro`, al registro general (lo del ensayo no).
    void anotar(const QString& texto, const QString& tipo, bool alRegistro = true);
    std::function<void(const std::string&, const nlohmann::json&, std::function<void(bool, const std::string&)>)>
        herramienta_;
    std::function<QVariantList()> chequeosAparato_;
    void marcarSucio();
    evento::Cue* cuePtr(int bloque, int cue);
    const evento::Cue* cuePtrConst(int bloque, int cue) const;
    /// Ruta de un discurso, siempre DENTRO de la carpeta del evento: un
    /// guion escrito por otro no puede hacer que el programa lea cualquier
    /// archivo del disco.
    QString discursoPathPriv(const QString& rel) const;
    /// Ruta de un medio (musica o video), siempre dentro de Medios/.
    /// Se asegura de que exista la escena del video con su fuente adentro, y
    /// la deja protegida. Devuelve false si no se pudo (ya lo anoto).
    bool prepararEscenaDeVideo();
    QString medioPath(const QString& rel) const;
    /// Ruta de `rel` dentro de `sub` en la carpeta del evento, o vacia si se
    /// sale de ahi.
    QString dentroDelEvento(const QString& sub, const QString& rel) const;
    static QVariantMap cueAMap(const evento::Cue& c, int bloque, int cue);

    /// La escena y la fuente que usa el cue de video. Nombres fijos: asi un
    /// video nuevo reemplaza al anterior en vez de encimarse, y el operador
    /// puede componer esa escena una vez y que le sirva para todos.
    /// OJO con los nombres: una escena TAMBIEN es una fuente, y los
    /// nombres son unicos entre todas. Si la fuente se llamara igual que su
    /// escena, se le agrega un « 2» por su cuenta y despues nada la
    /// encuentra por nombre.
    static constexpr const char* kEscenaVideo = "Video del evento";
    static constexpr const char* kVideo = "Video";
    /// A donde volver cuando el video termina (todavia no se usa: ver el
    /// comentario de `alTerminar` en el cue).
    QString escenaPrevia_;

    AppRuntime& rt_;
    ConferenceViewModel* sala_ = nullptr;
    graficos::Emisor* graficos_ = nullptr;
    /// Los gráficos al aire que se van cuando entra otro paso: la fuente y la generación
    /// con que se pusieron (si después se puso otra cosa ahí, no se toca).
    std::vector<std::pair<std::uint64_t, int>> alAvanzar_;
    /// Los gráficos que puso cada pieza (por su clave), para sacarlos cuando se
    /// termina su vida; y la pieza que puso la música que suena.
    std::map<std::string, std::pair<std::uint64_t, int>> graficosDePieza_;
    std::string musicaDePieza_;
    graficos::Vinculos* vinculos_ = nullptr;
    CarruselViewModel* carruseles_ = nullptr;
    escenas::Conmutador* conmutador_ = nullptr;
    evento::EventoStore store_;
    evento::Evento e_;
    evento::Padron padron_;
    /// Vuelca la lista blanca vieja del lobby al padrón, una sola vez.
    void migrarAutoAceptar();
    /// Va a buscar al gateway los que escanearon su invitación y los acredita.
    /// Corre sola mientras hay un evento abierto.
    void recogerAcreditaciones();
    /// El reloj de eso. Ver `recogerAcreditaciones`.
    QTimer buzon_;
    /// La página web del evento: el reenvío a la sala (cada 10 s, por si se reinició),
    /// cuándo arrancó el guion, la versión del sitio (cambia al regenerar/editar) y lo
    /// último que se mandó (para no repetir).
    QTimer webTimer_;
    std::function<nlohmann::json()> plataformas_;
    std::function<nlohmann::json()> preguntas_;
    std::function<int()> enLaSala_;
    std::function<nlohmann::json()> datosSala_;
    qint64 webArranqueMs_ = 0;
    int webVersion_ = 1;
    QByteArray webUltimo_;
    nlohmann::json webAviso_ = nlohmann::json::object();
    std::map<int, qint64> webInicioReal_; ///< cuándo empezó de verdad cada sección (índice → ms)
    QString carpetaWebAbs() const;
    /// Genera las páginas que falten del sitio (no pisa nada): al crear y al guardar.
    void asegurarWebBase();
    /// La línea del evento abierto (lo que manda) y su lectura como guion de cues
    /// (`e_.bloques` es `proyeccion_.bloques`).
    evento::linea::Linea linea_;
    evento::linea::Proyeccion proyeccion_;
    /// La corrida: la que corre o, parada, la última (lo emitido sigue sirviendo
    /// para el informe y los capítulos). Se olvida al arrancar otra o cambiar de evento.
    std::unique_ptr<evento::linea::Corrida> corrida_;
    std::map<std::string, std::unique_ptr<EjecutorLinea>> ejecutores_;
    Cursor cursor_;
    QString abierto_;
    bool sucio_ = false;
    bool ensayo_ = false;
    QVariantList bitacora_;
    std::vector<evento::Reparo> reparos_;
    /// Cada segundo mientras corre: la cuenta de la vista y el estado vivo (linea-aire.json).
    QTimer reloj_;
    QTimer vigiaEnCurso_; ///< cada minuto: el cambio de día y el evento automático
    bool automatico_ = true;
};

} // namespace ba::app
