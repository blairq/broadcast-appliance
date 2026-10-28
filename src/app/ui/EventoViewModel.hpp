// ============================================================================
// src/app/ui/EventoViewModel.hpp — el guion del evento, para la interfaz
//
// Le da a QML el guion (`evento.Evento`) y el recorrido en vivo
// (`evento.Guion`), y traduce cada cue a lo que hay que hacerle al sistema.
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

#include <nlohmann/json.hpp>
#include <QJsonArray>
#include <QVariantList>
#include <QVariantMap>

#include "evento/Evento.hpp"
#include "evento/EventoStore.hpp"
#include "evento/Padron.hpp"

namespace ba::app {
class CarruselViewModel;

class AppRuntime;
class ConferenceViewModel;
namespace graficos {
class Emisor;
class Vinculos;
} // namespace graficos

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
    /// El guion esta corriendo DE VERDAD (no en ensayo). Con esto prendido,
    /// cualquier cosa que se apriete por error sale al aire: la interfaz avisa
    /// antes de lo peligroso y el asistente no puede tocar la configuracion
    /// que reinicia el gateway.
    Q_PROPERTY(bool enVivo READ enVivo NOTIFY correChanged)
    Q_PROPERTY(QVariantMap actual READ actual NOTIFY correChanged)
    Q_PROPERTY(QVariantMap siguiente READ siguiente NOTIFY correChanged)
    /// {desvioSeg, enCueSeg, restanteSeg, cueNro, cuesTotal}: el reloj, que
    /// cambia cada segundo (aparte para no redibujar el guion entero).
    Q_PROPERTY(QVariantMap reloj READ reloj NOTIFY relojChanged)
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
    bool enVivo() const;
    QVariantMap actual() const;
    QVariantMap siguiente() const;
    QVariantMap reloj() const;
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
    Q_INVOKABLE QString enlaceDeCue(const QString& tipo) const;
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
    Q_INVOKABLE void parar();
    void setEnsayo(bool on);

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
    /// Ejecuta el cue donde esta parado el guion. En ensayo no toca nada.
    void ejecutarActual();
    /// Saca (con su salida) los gráficos que puso el guion y «se van al avanzar»
    /// (títulos, placas): se llama antes de pasar a otra cue.
    void sacarAlAvanzar();
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
    /// OJO con los nombres: en libobs una escena TAMBIEN es una fuente, y los
    /// nombres son unicos entre todas. Si la fuente se llamara igual que su
    /// escena, libobs le agrega un « 2» por su cuenta y despues nada la
    /// encuentra por nombre.
    static constexpr const char* kEscenaVideo = "Video del evento";
    static constexpr const char* kVideo = "Video";
    /// A donde volver cuando el video termina (todavia no se usa: ver el
    /// comentario de `alTerminar` en el cue).
    QString escenaPrevia_;

    AppRuntime& rt_;
    ConferenceViewModel* sala_ = nullptr;
    graficos::Emisor* graficos_ = nullptr;
    /// Los gráficos al aire que se van cuando el guion avanza: la fuente y la generación
    /// con que se pusieron (si después se puso otra cosa ahí, no se toca).
    std::vector<std::pair<std::uint64_t, int>> alAvanzar_;
    graficos::Vinculos* vinculos_ = nullptr;
    CarruselViewModel* carruseles_ = nullptr;
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
    std::unique_ptr<evento::Guion> guionCorriendo_;
    /// El último que se paró: su recorrido sigue sirviendo para el informe y los
    /// capítulos (se piden al terminar). Se olvida al arrancar otro o cambiar de evento.
    std::unique_ptr<evento::Guion> ultimoGuion_;
    const evento::Guion* guionDelInforme() const { return guionCorriendo_ ? guionCorriendo_.get() : ultimoGuion_.get(); }
    QString abierto_;
    bool sucio_ = false;
    bool ensayo_ = false;
    QVariantList bitacora_;
    std::vector<evento::Reparo> reparos_;
    QTimer reloj_;
    QTimer vigiaEnCurso_; ///< cada minuto: el cambio de día y el evento automático
    bool automatico_ = true;
};

} // namespace ba::app
