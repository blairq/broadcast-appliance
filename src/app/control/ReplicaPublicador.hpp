// SPDX-License-Identifier: GPL-2.0-or-later
//
// ReplicaPublicador: el modelo de la app (Studio y lo que cuelga de él), visto desde el
// tablero web (/control/app/, src/tablero-web: la MISMA QML del dashboard compilada con
// Qt para WebAssembly). Modelo, sin interfaz.
//
// La idea: en el navegador no hay motor ni GStreamer, pero la vista del dashboard solo
// necesita lo que ve la QML — propiedades con aviso (NOTIFY), modelos de lista con roles y
// las funciones que llaman los botones —. Esto lo publica de forma GENÉRICA, mirando el
// QMetaObject de cada objeto: no hay una lista de propiedades a mano que se desactualice.
// Si alguien agrega una propiedad a un modelo y la usa en una tarjeta, el tablero web la
// ve sin tocar nada acá. Del otro lado (tablero-web/ObjetoReplica) cada objeto es una
// réplica con las mismas propiedades, los mismos roles y las mismas funciones.
//
// PEREZOSO: de cada objeto se manda primero el ESQUEMA (nombres y tipos, sale del
// QMetaObject, no lee nada) y sus valores recién cuando la vista lee alguna de sus
// propiedades («abrir»). Lo que nadie mira en la web no se lee ni se manda. Después, solo
// lo que avisa que cambió, juntado cada kLoteMs.
//
// SEGURIDAD. Pasa por el control web (control/ControlWeb: el aparato se emparejó con PIN/QR
// y lo aprobó una persona). El tablero completo pide el perfil «todo» (["*"]): con él el
// aparato ya puede todo por las herramientas. Además:
//   - objetos que no se publican nunca (credenciales, cuentas, los aparatos del control
//     web, las ventanas y el escritorio de ESTA máquina): `negar`;
//   - propiedades con nombre de secreto (token, password, streamKey, pin, hash…): no salen;
//   - llamar solo lo que el esquema publicó, con tope por segundo, y lo que cambia algo
//     (funciones sin valor de vuelta y escribir propiedades) queda en el registro.
//
// --- Protocolo ({type:"replica", op, …} por el WebSocket del control web) ------------------
//
// aparato → app
//   {op:"hola"}                     empezar (o empezar de nuevo): vuelve el esquema de la raíz.
//   {op:"abrir", o}                 mandar los valores de ese objeto y lo que cambie.
//   {op:"llamar", id, o, m, a:[…], confirmado?}  llamar la función `m` de `o` con esos argumentos.
//   {op:"poner", id?, o, p, v, confirmado?}       escribir la propiedad `p` de `o`.
//   Solo lo de la LISTA PERMITIDA (ReplicaPermitidas.inc, generada desde la QML del dashboard por
//   tools/generar/replica-permitidas.py), cada cosa con su herramienta MCP equivalente: el perfil
//   del aparato y, si toca el aire y hay aire, la misma confirmación que el control web. Lo de
//   afuera se rechaza y queda en el registro.
//   {op:"video", claves:[{clave, ancho, fps}]}  qué videos quiere (control/CuadrosWeb).
//   {op:"webrtc"}                   el enlace del programa por WebRTC (/cast/?token=…).
//   {op:"pausa", si}                la pestaña se escondió (no se manda nada) o volvió (todo de nuevo).
//   Salidas múltiples: cada aparato maneja SU salida (la de ControlWeb, como la superficie
//   HTML). Escribir `Studio.salidas.activa` desde la web la elige (no cambia la de la app) y
//   todo lo que se lee o se hace por esa conexión es «como si» la activa fuera esa.
//   {op:"lote", m:[…]}              varios de los de arriba juntos.
//
// app → aparato (siempre en {op:"lote", m:[…]}, en orden)
//   {op:"raiz", o, perfil, escribe} la raíz (después de su esquema).
//   {op:"esquema", o, props:[[nombre, tipo, escribible]], metodos:[[nombre, conValor]],
//    roles:[…] | null}              tipo: o objeto, l lista, m mapa, s texto, b sí/no, n número, v otro.
//   {op:"valores", o, v:{p: valor}} un objeto en {"$o": id}; tamaños y puntos en {"$t": …}.
//   {op:"filas", o, filas:[[valor por rol]]}  las filas enteras de un modelo.
//   {op:"resultado", id, ok, v | error}
//   {op:"error", error}             no se puede (p. ej. el perfil no alcanza).
//   {op:"confirmar", id, texto}     toca el aire y hay aire: si la persona confirma, el mismo
//                                   llamar/poner con confirmado:true.
//   {op:"webrtc", url, error?}      el enlace del programa por WebRTC.
//   {op:"aviso", texto, nivel}      lo que la app le avisa al operador (Studio.notify).
//   {op:"cuadro", …}                un cuadro de video (control/CuadrosWeb).
// ============================================================================
#pragma once

#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariant>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace ba::app::control {

class ReplicaPublicador : public QObject {
    Q_OBJECT
   public:
    using json = nlohmann::json;
    struct Ganchos {
        /// Mandarle `msg` a esa conexión (por el gateway).
        std::function<void(const std::string& conexion, const json& msg)> enviar;
        /// Dejar en el registro lo que hizo un aparato.
        std::function<void(const std::string& aparato, const std::string& texto)> anotar;
        /// La conexión pidió estos videos ([{clave, ancho, fps}], ya revisados).
        std::function<void(const std::string& conexion, const json& claves)> video;
        /// El enlace del programa por WebRTC (/cast/, el de las pantallas de la red), para
        /// verlo con <video> o medir: listo(url, error).
        std::function<void(std::function<void(const std::string& url, const std::string& error)> listo)> enlaceCast;
        /// ¿Ese aparato puede hacer lo que haría esa herramienta MCP? (ControlWeb::autorizar:
        /// perfil y confirmación de aire) {ok} | {ok:false, error} | {ok:false, confirmar, texto}.
        /// Sin esto, todo lo de la lista se permite (las pruebas).
        std::function<json(const std::string& aparato, const std::string& herramienta, bool lectura, bool confirmado)> autorizar;
        /// Las salidas múltiples: la que maneja el aparato (ControlWeb::salidaDe/setSalida, la
        /// misma que la superficie HTML) y cómo hacer que la app responda «como si» su salida
        /// activa fuera esa (SalidasViewModel::contexto: devuelve con qué volver).
        std::function<std::string(const std::string& aparato)> salidaDe;
        std::function<std::string(const std::string& aparato, const std::string& salida)> setSalida;
        std::function<std::function<void()>(const std::string& salida)> contexto;
        /// El objeto de las salidas (su `activa` escrita desde la web elige la del aparato).
        QString objetoSalidas = QStringLiteral("Studio.salidas");
    };
    /// Una entrada de la lista permitida (ReplicaPermitidas.inc, sacada de la QML del dashboard).
    struct Permitida {
        QString objeto, nombre;
        bool poner = false;     ///< escribir una propiedad (si no, llamar una función)
        std::string mcp;        ///< la herramienta equivalente ("" = solo de la vista)
    };
    /// La lista que trae la app (generada); las pruebas pueden poner otra.
    void setPermitidas(std::vector<Permitida> p) { permitidas_ = std::move(p); }
    static std::vector<Permitida> permitidasDeLaApp();
    /// Cada cuánto se junta lo que cambió (ms).
    static constexpr int kLoteMs = 50;
    /// Cuántas llamadas y escrituras por segundo por conexión.
    /// (El navegador ya junta los cambios de un fader: el último de cada lote gana. Con varios
    /// faders a la vez, 25 lotes por segundo × unos pocos.)
    static constexpr int kLlamadasPorSegundo = 100;
    /// Cuántas lecturas (funciones que devuelven algo) por segundo por conexión.
    static constexpr int kLecturasPorSegundo = 300;

    ReplicaPublicador(QObject* raiz, const QString& nombreRaiz = QStringLiteral("Studio"), QObject* parent = nullptr);
    ~ReplicaPublicador() override;
    Ganchos& ganchos() { return g_; }
    /// Propiedades de la raíz (QObject*) que no se publican nunca (credenciales, ventanas…).
    void negar(const QStringList& propiedadesDeLaRaiz);
    /// Objetos que se vuelven a leer una vez por segundo (lo que depende de la salida de cada
    /// aparato y la app avisa solo por la suya); van solo si cambió.
    void refrescarCadaSegundo(const QStringList& objetos) { refrescoLento_ = objetos; }
    /// Funciones que no se llaman nunca desde la web (en cualquier objeto: «copiar», «quit»…).
    void negarFunciones(const QStringList& nombres);

    /// Un {type:"replica", …} de esa conexión (perfil: el de su aparato).
    void entrada(const std::string& conexion, const std::string& aparato, const json& perfil, const json& msg);
    /// La conexión se fue.
    void cerrada(const std::string& conexion);
    /// Manda ya lo juntado (las pruebas; en la app lo hace el reloj).
    void vaciar();

    /// ¿Ese perfil alcanza para el tablero? (["*"])
    static bool perfilPermite(const json& perfil);
    /// ¿Ese nombre es de una función que HACE algo (aunque devuelva un valor: setMuted → bool)?
    static bool esAccion(const QString& nombre);
    /// ¿Ese nombre de propiedad parece un secreto? (no se publica)
    static bool nombreSecreto(const QString& nombre);
    /// Lo que ve la web de un QVariant sin objetos (números, textos, listas, mapas, tamaños).
    static json simple(const QVariant& v);
    int conexiones() const { return static_cast<int>(cons_.size()); }

   private slots:
    void alAvisar(const QString& texto, const QString& nivel);

   private:
    struct Obj {
        QPointer<QObject> o;
        bool modelo = false;
        std::vector<int> props;       ///< índices de propiedades publicadas
        std::vector<int> roles;       ///< ids de roles (modelos), en el orden del esquema
        std::set<QString> metodos;    ///< nombres que se pueden llamar
        json esquema;
        bool escuchando = false;
    };
    struct Con {
        std::string aparato;
        bool escribe = false;
        std::set<QString> esquemas, abiertos;
        std::map<QString, std::set<int>> sucios;
        std::set<QString> filasSucias;
        json cola = json::array();
        std::deque<std::int64_t> llamadas, lecturas;
        std::set<std::string> rechazosAnotados; ///< una línea en el registro por cosa rechazada
        std::string salida;                     ///< la que maneja este aparato ("" = la por omisión)
        std::shared_ptr<QObject> vista;         ///< la «vista» de este aparato en las suscripciones
        std::map<std::string, std::int64_t> anotado; ///< la última vez que algo fue al registro
        bool pausada = false;                   ///< la pestaña del navegador está escondida
        std::map<std::string, std::string> enviado; ///< «objeto.prop» → lo último que se le mandó
    };
    friend class AvisoReplica;

    QString refDe(QObject* o, const QString& sugerido);
    json armarEsquema(const QString& id, Obj& ob);
    /// `sugerido`: el id si es un objeto que todavía no tiene (el camino: «Studio.aire»).
    json valor(const QVariant& v, Con& c, json& esquemas, const QString& sugerido = {});
    void abrir(Con& c, const QString& id);
    void escuchar(const QString& id, Obj& ob);
    void cambio(const QString& id, const std::vector<int>& props);
    void cambioFilas(const QString& id);
    void programar();
    void encolar(Con& c, json msg, json& esquemas);
    json filas(Obj& ob, Con& c, json& esquemas);
    void llamar(const std::string& cid, Con& c, const json& m);
    void poner(Con& c, const json& m);
    QVariant aVariant(const json& j) const;
    bool tope(Con& c, bool lectura);
    bool anotarAhora(Con& c, const std::string& que);
    /// ¿Se puede? "" si sí; si no, el error (y ya se contestó/anotó); `confirmar`: se pidió confirmación.
    std::string permiso(Con& c, QObject* o, const QString& oid, const QString& nombre, bool poner, bool lectura,
                        const json& m, bool& confirmar);
    QObject* resolver(const QString& camino) const;
    /// Mientras dure lo que devuelve, la app lee y hace en la salida de esa conexión.
    std::function<void()> enSuSalida(const Con& c) const;
    /// Todo lo abierto de la conexión, de nuevo (cambió su salida).
    void todoSucio(Con& c);
    void procesar(const std::string& cid, Con& c, const json& m, int profundidad);

    QPointer<QObject> raiz_;
    QString nombreRaiz_;
    Ganchos g_;
    std::map<QString, Obj> objs_;
    std::unordered_map<QObject*, QString> idDe_;
    QSet<QObject*> negados_;
    QStringList aNegar_;
    bool avisosConectados_ = false; ///< Studio.notify(texto, nivel) → {op:"aviso"} a todos ///< se resuelven al primer «hola» (la raíz ya está armada)
    QSet<QString> funcionesNegadas_;
    std::vector<Permitida> permitidas_ = permitidasDeLaApp();
    std::map<std::string, Con> cons_;
    QTimer reloj_;
    QTimer lento_;
    bool muriendo_ = false; ///< en el destructor: nada vuelve a programar envíos
    QStringList refrescoLento_;
    int siguiente_ = 1;
};

} // namespace ba::app::control
