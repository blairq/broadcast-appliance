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
//   {op:"llamar", id, o, m, a:[…]}  llamar la función `m` de `o` con esos argumentos.
//   {op:"poner", o, p, v}           escribir la propiedad `p` de `o`.
//   {op:"video", claves:[{clave, ancho, fps}]}  qué videos quiere (control/CuadrosWeb).
//   {op:"webrtc"}                   el enlace del programa por WebRTC (/cast/?token=…).
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
    };
    /// Cada cuánto se junta lo que cambió (ms).
    static constexpr int kLoteMs = 50;
    /// Cuántas llamadas y escrituras por segundo por conexión.
    static constexpr int kLlamadasPorSegundo = 40;

    ReplicaPublicador(QObject* raiz, const QString& nombreRaiz = QStringLiteral("Studio"), QObject* parent = nullptr);
    ~ReplicaPublicador() override;
    Ganchos& ganchos() { return g_; }
    /// Propiedades de la raíz (QObject*) que no se publican nunca (credenciales, ventanas…).
    void negar(const QStringList& propiedadesDeLaRaiz);
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
        std::deque<std::int64_t> llamadas;
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
    bool tope(Con& c);
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
    std::map<std::string, Con> cons_;
    QTimer reloj_;
    int siguiente_ = 1;
};

} // namespace ba::app::control
