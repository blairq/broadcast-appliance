// SPDX-License-Identifier: GPL-2.0-or-later
//
// SesionRemota: la banca virtual de cada integrante (docs/funciones/SESION-REMOTA.md).
// Un integrante entra desde el navegador (en la sala o afuera, da lo mismo) con
// su link personal y su PIN; la presidencia o el operador lo ve por la cámara y
// lo habilita; habilitado, cuenta como presente (y en las fases siguientes pide
// la palabra, habla y vota).
//
// Esta clase es la fuente de verdad del lado de la app:
//   * las credenciales: link + PIN por integrante, guardados SOLO como hash con
//     sal (Config/sesion-remota.json). El link y el PIN se muestran una vez, al
//     generarlos;
//   * el estado de cada uno: conectado, habilitado (por quién), dónde está
//     (sala/afuera), desde cuándo;
//   * el registro de la sesión (quién entró, quién lo habilitó, intentos
//     fallidos, bloqueos), que va anexado al acta.
//
// El borde de red es el gateway (src/gateway/Sesion.hpp): esta clase no habla
// con él directamente; pide lo que hace falta por las señales (`enviar`,
// `cortar`, `credencialesCambio`) y recibe sus avisos por `evento()`.
#pragma once

#include <QByteArray>
#include <QObject>
#include <QStringList>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace ba::app::asamblea {

class Cuerpo;

class SesionRemota : public QObject {
    Q_OBJECT
    /// [{id, nombre, banca, credencial, conectado, estado, lugar, ip, desde_ms, habilitado_por, bloqueado}]
    Q_PROPERTY(QVariantList participantes READ participantesUi NOTIFY cambio)
    /// Habilitados y conectados: los que cuentan como presentes por acá.
    Q_PROPERTY(int presentes READ presentes NOTIFY cambio)
    Q_PROPERTY(int conectados READ conectados NOTIFY cambio)
    /// El origen público para los links (lo da el gateway): "https://…".
    Q_PROPERTY(QString origen READ origenUi NOTIFY cambio)
    /// La votación en los celulares (ver votacion()).
    Q_PROPERTY(QVariantMap votacionActual READ votacionUi NOTIFY cambio)
    Q_PROPERTY(int votoSegundos READ votoSegundos WRITE setVotoSegundos NOTIFY cambio)

   public:
    using json = nlohmann::json;
    SesionRemota(const Cuerpo& cuerpo, std::filesystem::path datos, QObject* parent = nullptr);

    // --- Credenciales -------------------------------------------------------------
    /// Genera (o regenera: el link anterior deja de andar) el link y el PIN de un
    /// integrante. Devuelve {link, pin, id} UNA vez, o {error}.
    json generar(const std::string& integrante);
    bool revocar(const std::string& integrante);
    /// Usuario y contraseña (para entrar sin el link, tipeando): la contraseña se
    /// genera (12 caracteres fáciles de dictar) y se muestra UNA vez. {usuario, clave} o {error}.
    json generarClave(const std::string& integrante, const std::string& usuario = {});
    /// Los datos de la conexión: {usuario?, email?, telefono?, nota?}. "" o el error.
    std::string setDatos(const std::string& integrante, const json& datos);
    bool tieneCredencial(const std::string& integrante) const;
    /// La lista que va al gateway: [{id, nombre, banca, sal, hash_token, hash_pin}].
    json paraGateway() const;
    void setOrigen(const std::string& origen);
    std::string origen() const { return origen_; }

    // --- Estado ---------------------------------------------------------------------
    /// Un aviso del gateway (evento SSE "sesion"): conexion, entrada, intento, bloqueo.
    void evento(const json& ev);
    /// Habilita (cuenta como presente) o deshabilita. `quien`: operador, presidencia, mcp…
    std::string habilitar(const std::string& integrante, bool si, const std::string& quien);
    /// Lo saca de la sesión (corta su conexión). No revoca el link.
    std::string expulsar(const std::string& integrante, const std::string& quien, const std::string& motivo);
    json participantes() const;
    int presentes() const;
    int conectados() const;
    /// Los integrantes habilitados y conectados (para no contarlos dos veces).
    std::vector<std::string> presentesIds() const;
    /// Las bancas de los habilitados y conectados.
    std::vector<std::string> presentesBancas() const;

    // --- La palabra (fase R2) ------------------------------------------------------------
    /// Darle o quitarle la palabra (lo llama la fila de oradores, asamblea/Bancas):
    /// le avisa al celular, con su tiempo; en la fase R3 también abre su audio.
    void darPalabra(const std::string& integrante, bool palabra, int segundos);
    /// La fila cambió: a cada remoto que está en la fila le dice su turno; a todos, quién habla.
    void fila(const std::vector<std::string>& cola, const std::string& alAire);
    /// «ultimo_minuto» | «terminado»: aviso en su celular.
    void avisoTiempo(const std::string& integrante, const std::string& aviso);
    /// Un mensaje cualquiera a su celular (el debate, por ejemplo).
    void mandar(const std::string& integrante, const json& msg) { emit enviar(QString::fromStdString(integrante), msg); }
    /// El primero que tiene la palabra ("" si nadie); en un debate entre remotos son dos.
    std::string conPalabra() const { return palabras_.empty() ? std::string() : palabras_.front().id; }
    bool tienePalabra(const std::string& id) const;
    /// Los que tienen la palabra, en orden: el primero va al canal 1 (BA.Remoto.1), el segundo al 2.
    std::vector<std::string> conPalabraTodos() const;
    /// «Apretar para hablar» desde la consola: le avisa al integrante que la presidencia
    /// le habla (su navegador contesta la conexión directa de la consola).
    void ptt(const std::string& integrante, bool activo, const std::string& consola);

    // --- Lo que ve el remoto en su celular -------------------------------------------------
    /// Además de su propia cámara, hasta dos canales por WebRTC que elige en el celular:
    /// [{id: "a"|"b", nombre, fuente: "programa" | <nombre de escena>}]. Vacío = solo el
    /// programa. Queda guardado. "" o el error.
    std::string setVistas(const json& vistas);
    json vistas() const;
    /// Mandarle algo a todos los conectados (el punto del orden del día, por ejemplo).
    void info(const json& campos);

    // --- La votación (fase R4) ------------------------------------------------------------
    /// Abre la votación en los celulares de los habilitados: cartel rojo con el tema,
    /// las opciones [{id, texto}] y la cuenta regresiva (`segundos` 0 = sin tiempo).
    /// `origen`: "central" (la abrió la CCU) o "app" (sin central). "" o el error.
    std::string abrirVotacion(int numero, const std::string& tema, const json& opciones, int segundos,
                              const std::string& origen);
    void pausarVotacion(bool pausa);
    /// Cierra: nadie más vota. Devuelve los votos remotos.
    json cerrarVotacion();
    /// {estado: sin|abierta|pausada|cerrada, numero, tema, opciones, hasta_ms, origen,
    ///  votos: [{id, nombre, banca, opcion, ms}], totales: {opcion: n}}.
    json votacion() const;
    /// El resultado de la sala: el voto nominal de la central (por banca, como lo da
    /// ConferenciaDcn) más los votos remotos, UN voto por integrante (vale el
    /// último; si la central no trae hora, el remoto pisa al de la central solo si
    /// votó después de que la central lo mostró). Cada voto con su origen.
    json resultadoCombinado(const json& nominalCentral) const;
    /// Le avisa a los celulares el resultado (al cerrar).
    void mostrarResultado(const json& totales);
    /// Los próximos temas (del orden del día), para la pantalla del celular.
    void setProximos(const json& lista);
    /// El tiempo para votar en el celular (segundos; 0 = sin cuenta regresiva). Se guarda.
    int votoSegundos() const { return votoSegundos_; }
    void setVotoSegundos(int s);
    /// Las opciones de un menú de votación de la central (si_no, si_no_abstencion…).
    static json opcionesDeMenu(const std::string& menu);

    // --- La consola web de la presidencia -------------------------------------------------
    /// Un acceso para la consola (/presidencia/): {link, pin, id} UNA vez, o {error}.
    json generarConsola(const std::string& nombre);
    bool revocarConsola(const std::string& id);
    /// [{id, nombre, conectada, creado_ms}]
    json consolas() const;
    bool hayConsolas() const { return !consolasConectadas_.empty(); }

    // --- Registro (va al acta) ---------------------------------------------------------
    json registro() const { return registro_; }
    std::string anexoActa() const;
    void limpiarRegistro();
    /// Segundos que se guarda la habilitación de alguien que se cayó (una recarga,
    /// un corte de red): si vuelve antes, no hay que habilitarlo de nuevo.
    int graciaS() const { return graciaS_; }

    // --- Para la interfaz --------------------------------------------------------------
    QVariantList participantesUi() const;
    QString origenUi() const { return QString::fromStdString(origen_); }
    Q_INVOKABLE QVariantMap generarUi(const QString& integrante);
    Q_INVOKABLE bool revocarUi(const QString& integrante);
    Q_INVOKABLE QVariantMap generarClaveUi(const QString& integrante, const QString& usuario);
    Q_INVOKABLE QString setDatosUi(const QString& integrante, const QVariantMap& datos);
    Q_INVOKABLE QString habilitarUi(const QString& integrante, bool si);
    Q_INVOKABLE QString expulsarUi(const QString& integrante, const QString& motivo);
    /// La foto de su cámara (la última que mandó): la pide la interfaz al gateway
    /// y, cuando llega (`fotoLista`), la toma con `fotoUrl` (data:image/jpeg…).
    Q_INVOKABLE void pedirFoto(const QString& integrante);
    QVariantMap votacionUi() const;
    /// Sin la central: abrir con un menú (si_no, si_no_abstencion…) o cerrar.
    Q_INVOKABLE QString abrirVotacionUi(const QString& tema, const QString& menu, int segundos);
    Q_INVOKABLE void pausarVotacionUi(bool pausa) { pausarVotacion(pausa); }
    Q_INVOKABLE void cerrarVotacionUi();
    Q_PROPERTY(QVariantList consolasUi READ consolasUi NOTIFY cambio)
    QVariantList consolasUi() const;
    Q_INVOKABLE QVariantMap generarConsolaUi(const QString& nombre);
    Q_INVOKABLE bool revocarConsolaUi(const QString& id) { return revocarConsola(id.toStdString()); }
    Q_INVOKABLE QString fotoUrl(const QString& integrante) const;
    /// Los canales que el remoto puede mirar en su celular (ver setVistas).
    Q_PROPERTY(QVariantList vistasUi READ vistasUi NOTIFY cambio)
    QVariantList vistasUi() const;
    Q_INVOKABLE QString setVistasUi(const QVariantList& vistas);
    void ponerFoto(const QString& integrante, const QByteArray& jpeg);

   signals:
    void cambio();
    /// Hay que reenviarle al gateway la lista de credenciales.
    void credencialesCambio();
    /// Mandarle un mensaje a un participante ("*" = a todos).
    void enviar(const QString& integrante, const nlohmann::json& msg);
    void cortar(const QString& integrante, const QString& motivo);
    void fotoPedida(const QString& integrante);
    void fotoLista(const QString& integrante);
    /// Algo para avisarle al operador (un bloqueo, alguien que espera).
    void aviso(const QString& texto, const QString& nivel);
    /// Un remoto pidió la palabra (o canceló su pedido) desde el celular.
    void pedidoPalabra(const QString& integrante, bool pedir);
    /// La consola de la presidencia pide algo (habilitar, dar la palabra, votar…).
    void accionConsola(const QString& consola, const nlohmann::json& msg);
    /// Se conectó o se fue una consola (para mandarle el estado).
    void consolaConexion(const QString& consola, bool conectada);
    /// R3: quién manda audio y video a la sala (el remoto con la palabra; "" = nadie).
    /// Quiénes mandan audio y video a la sala (hasta dos), en orden de canal.
    void mediosA(const QStringList& integrantes);
    /// Volvió a entrar (o recargó la página): lo que no es de acá (el debate) se le reenvía.
    void volvio(const QString& integrante);

   private:
    struct Estado {
        bool conectado = false;
        bool habilitado = false;
        std::string habilitadoPor, lugar, ip;
        std::int64_t desdeMs = 0;      // conectado desde
        std::int64_t cayoMs = 0;       // se desconectó (para la gracia)
        std::int64_t bloqueadoHasta = 0;
    };
    void cargar();
    void guardar() const;
    void anotar(const std::string& que, const std::string& integrante, const json& extra = {});
    json integrante(const std::string& id) const;
    void mandarEstado(const std::string& id);

    const Cuerpo& cuerpo_;
    std::filesystem::path archivo_;
    std::filesystem::path archivoRegistro_;
    json credenciales_ = json::object(); // id -> {sal, hash_token, hash_pin, creado_ms}
    std::map<std::string, Estado> estado_;
    json registro_ = json::array();
    std::string origen_;
    int graciaS_ = 120;
    int votoSegundos_ = 60;
    std::map<QString, QByteArray> fotos_;
    struct Palabra {
        std::string id;
        int segundos = 0;       ///< lo que se le dio (para reenviárselo si vuelve)
        long long desdeMs = 0;
    };
    std::vector<Palabra> palabras_; ///< hasta dos (un debate); el resto, uno
    void emitirMedios();
    json vistas_ = json::array();
    json consolas_ = json::object();          // id -> {nombre, sal, hash_token, hash_pin, creado_ms}
    std::set<std::string> consolasConectadas_;
    std::map<std::string, int> turnos_; // lo último que se le dijo a cada uno
    json ultimaInfo_ = json::object();
    struct Votacion {
        std::string estado = "sin";  // sin | abierta | pausada | cerrada
        int numero = 0;
        std::string tema, origen;
        json opciones = json::array();
        std::int64_t hastaMs = 0, abiertaMs = 0;
        std::map<std::string, std::pair<std::string, std::int64_t>> votos; // id -> (opcion, ms)
    } vot_;
    json mensajeVotacion(const std::string& id) const;
    void mandarVotacion();
};

} // namespace ba::app::asamblea
