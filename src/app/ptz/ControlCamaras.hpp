// src/app/ptz/ControlCamaras.hpp — el PTZ de cada entrada de video (Studio.camaras)
//
// Un solo frontend para todas las cámaras: la página «Cámaras PTZ», las
// herramientas MCP (ptz_*) y el asistente hablan con esto, y esto con el
// backend de cada entrada (PtzBackend):
//
//   * Todas las entradas de video (hardware, red, NDI) tienen PTZ DIGITAL
//     (recorte y zoom en la GPU, filtro ba_ptz_digital), si está prendido en
//     esa entrada (de fábrica sí: sin zoom no cuesta nada).
//   * Si la cámara se mueve de verdad, se le elige un backend de hardware
//     (VISCA, ONVIF, Panasonic) y aparecen sus controles: lo que el backend
//     dice que puede (capacidades).
//   * Con los dos, el joystick mueve uno u otro («modo»: optico | digital).
//   * «Mejorar imagen» (filtro ba_mejora_imagen) también es por entrada:
//     sigue lo general (Configuración → Cámaras y entradas), o se fuerza, o se apaga.
//
// Se guarda en Config/camaras.json (por nombre de entrada: si la entrada se
// borra y se vuelve a agregar con el mismo nombre, recupera su PTZ). Las
// claves de las cámaras van cifradas en Credenciales/, nunca en el json.
//
// Los filtros se ponen y se sacan solos: cada 1,5 s (y cuando cambian las
// entradas) se revisa que cada entrada tenga lo que le corresponde. Así una
// entrada nueva ya tiene su PTZ, y un cambio en Configuración rige enseguida.
//
// La implementación va en tres archivos (y lo de adentro, en ControlCamarasCamara.hpp):
// ControlCamaras.cpp (lo que se muestra, entradas, órdenes, configuración, disco),
// ControlCamarasPosiciones.cpp (posiciones, presets, miniaturas) y ControlCamarasFoto.cpp
// (la foto del panel y apuntar haciendo click en ella).

#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace ba::app {
class AppRuntime;
}

namespace ba::app::ptz {

class PtzBackend;
class PtzDigital;
class OnvifBusqueda;
struct Posicion;

class ControlCamaras : public QObject {
    Q_OBJECT
    /// [{entrada, tipoEntrada, backend, conf, tieneClave, digital, mejora,
    ///   mejoraActiva, modo, capacidades, capacidadesDigital, listo, estado,
    ///   presetsDigitales}]
    Q_PROPERTY(QVariantList camaras READ camaras NOTIFY cambio)
    /// Los backends de hardware que se pueden elegir y sus campos.
    Q_PROPERTY(QVariantList backends READ backends CONSTANT)
    Q_PROPERTY(bool buscando READ buscando NOTIFY busquedaCambio)
    /// Cámaras ONVIF encontradas en la red: [{direccion, host, nombre, modelo}]
    Q_PROPERTY(QVariantList encontradas READ encontradas NOTIFY busquedaCambio)
    /// «Mejorar imagen» general (Configuración → Cámaras y entradas).
    Q_PROPERTY(bool mejoraGeneral READ mejoraGeneral NOTIFY cambio)

   public:
    ControlCamaras(AppRuntime& rt, std::filesystem::path dataRoot, QObject* parent = nullptr);
    ~ControlCamaras() override;

    QVariantList camaras() const;
    static QVariantList backends();
    bool buscando() const;
    QVariantList encontradas() const;
    bool mejoraGeneral() const;

    // --- Mover (lo del joystick) -------------------------------------------
    /// Velocidades -1..1 (pan + derecha, tilt + arriba, zoom + acercar).
    // --- Interlock: no mover la cámara que está AL AIRE -----------------------
    /// La cámara (su entrada) está en la escena al aire.
    Q_INVOKABLE bool alAire(const QString& entrada) const;
    /// {activo, seguir_suave_permitido} (Config/ptz-interlock.json).
    Q_INVOKABLE QVariantMap interlock() const;
    Q_INVOKABLE void setInterlock(const QVariantMap& cambios);
    /// Si mover la cámara de `entrada` necesita que alguien confirme: "" si se
    /// puede mover ya, o el porqué (está al aire y el interlock está prendido).
    /// Lo consultan todos antes de mover (las páginas, que preguntan; el MCP,
    /// que lo devuelve como error salvo `forzar`). `forzar`: ya se confirmó.
    /// `modo`: "" (movimiento común) o "seguir_suave" (ptz/Interlock.hpp).
    Q_INVOKABLE QString chequearMovimiento(const QString& entrada, bool forzar = false,
                                           const QString& modo = QString()) const;
    Q_INVOKABLE QString mover(const QString& entrada, double pan, double tilt, double zoom);
    Q_INVOKABLE QString parar(const QString& entrada);
    Q_INVOKABLE QString enfocar(const QString& entrada, double velocidad);
    Q_INVOKABLE QString autoFoco(const QString& entrada, bool automatico);
    /// El preset `numero` es la POSICIÓN de ese número (Posiciones): ir a ella, o
    /// regrabarla (si no hay, se crea «Preset N»). Sin posición con ese número, ir
    /// usa el preset propio de la cámara, si tiene.
    Q_INVOKABLE QString irPreset(const QString& entrada, int numero);
    Q_INVOKABLE QString guardarPreset(const QString& entrada, int numero);
    Q_INVOKABLE QString home(const QString& entrada);
    /// Encuadre digital directo: centro (x, y 0..1) y zoom (1..8).
    Q_INVOKABLE QString encuadre(const QString& entrada, double x, double y, double zoom, double segundos);
    /// Apuntar con el mouse sobre la imagen de la entrada (lo que se ve, ya
    /// recortado): (u, v) 0..1 es el punto que va al centro; con un cuadro
    /// (ancho, alto 0..1 de la imagen), además se acerca hasta que el cuadro
    /// llene la imagen. ancho = 0: solo centrar. Lo hace lo que mueve el
    /// joystick: el digital, exacto; una cámara motorizada, aproximado (un
    /// pulso de movimiento y otro de zoom, porque los protocolos no dicen
    /// cuántos grados hay en el cuadro).
    Q_INVOKABLE QString apuntar(const QString& entrada, double u, double v, double ancho, double alto);
    /// {x, y, zoom, moviendo} del PTZ digital (vacío si está apagado).
    Q_INVOKABLE QVariantMap posicion(const QString& entrada) const;

    // --- Configurar ------------------------------------------------------------
    /// backend: "ninguno" | "visca" | "onvif" | "panasonic"; conf: host,
    /// puerto, transporte, direccion, usuario, clave, perfil. Una clave vacía
    /// deja la que estaba guardada. Devuelve "" o el error.
    Q_INVOKABLE QString configurar(const QString& entrada, const QString& backend, const QVariantMap& conf);
    Q_INVOKABLE QString setDigital(const QString& entrada, bool prendido);
    /// Los PTZ que maneja un plugin: el plugin arrancó, se cayó o volvió.
    void refrescarPlugins();
    /// Qué mueve ahora el PTZ de la entrada: "optico", "digital" o "" (nada).
    QString modoActivo(const QString& entrada);
    /// Todo lo que el protocolo dijo que la cámara puede, con las respuestas crudas.
    QVariantMap funcionesDe(const QString& entrada);
    /// "general" | "si" | "no".
    Q_INVOKABLE QString setMejora(const QString& entrada, const QString& modo);
    /// Qué mueve el joystick cuando hay hardware: "optico" | "digital".
    Q_INVOKABLE QString setModo(const QString& entrada, const QString& modo);
    Q_INVOKABLE void buscarOnvif();
    /// Revisa entradas y filtros ya (también corre solo cada 1,5 s).
    Q_INVOKABLE void sincronizar();

    // --- Posiciones con nombre (ptz/Posiciones) --------------------------------
    /// [{id, nombre, comentario, definicion, digital{x,y,zoom}, camara{backend,memoria|token},
    /// velocidad?, segundos?, miniatura?, miniatura_ruta?, miniatura_url?, creada, modificada}]
    Q_INVOKABLE QVariantList posiciones(const QString& entrada) const;
    /// Registra donde está ahora la cámara (cabeza y/o recorte) con ese
    /// nombre. {ok, error, aviso, id}.
    /// `numero`: su número de preset (0 = el primero libre).
    Q_INVOKABLE QVariantMap crearPosicion(const QString& entrada, const QString& nombre,
                                          const QString& comentario, double velocidad = 0, double segundos = -1,
                                          int numero = 0);
    /// cambios: {nombre?, comentario?, numero? (su número de preset, libre), velocidad? (0..1, 0 = la de la cámara),
    /// segundos? (-1 = sin fijar), regrabar?: bool (vuelve a registrar donde
    /// está ahora, con foto nueva), miniatura?: bool (solo la foto)}. {ok, error, aviso}.
    Q_INVOKABLE QVariantMap editarPosicion(const QString& entrada, const QString& idONombre,
                                           const QVariantMap& cambios);
    /// Borra la posición (y libera su memoria en la cámara).
    Q_INVOKABLE QString borrarPosicion(const QString& entrada, const QString& idONombre);
    /// Va a la posición: la cámara a su memoria (con su velocidad) y el
    /// recorte a su encuadre, en `segundos` (-1 = los de la posición, o 1).
    /// {ok, error, aviso}.
    Q_INVOKABLE QVariantMap irAPosicion(const QString& entrada, const QString& idONombre,
                                        double segundos = -1);
    /// De una posición: {existe, id, nombre, digital (tiene encuadre digital), x, y,
    /// zoom, motorizada (tiene memoria en una cámara con motor)}. Para la fuente
    /// «Banca con la palabra»: con digital, recorta la toma entera sin mover nada.
    Q_INVOKABLE QVariantMap datosPosicion(const QString& entrada, const QString& idONombre) const;
    /// Las posiciones con ese nombre (sin tildes ni mayúsculas) en TODAS las cámaras:
    /// [{entrada, id, nombre}]. Para «enfocar Presidencia» sin decir qué cámara.
    Q_INVOKABLE QVariantList buscarPosicion(const QString& nombre) const;
    /// La cámara solo tiene PTZ digital (sin motor).
    Q_INVOKABLE bool soloDigital(const QString& entrada) const;
    /// Deja de seguir con el seguimiento de caras (lo pone quien lo maneja): al ir a
    /// una posición a pedido, el seguimiento no tiene que pelearle.
    void setDejarDeSeguir(std::function<void(const QString&)> fn) { dejarDeSeguir_ = std::move(fn); }

    /// Saca la foto de la posición: lo que muestra la entrada AHORA, 320 px de
    /// ancho, en Miniaturas/ptz/. "" o el error. Se llama sola al crear o
    /// regrabar (un rato después, cuando el recorte ya llegó).
    Q_INVOKABLE QString capturarMiniatura(const QString& entrada, const QString& idONombre);

    // --- La foto del panel PTZ (cámaras motorizadas) ----------------------------
    /// Saca una foto de lo que muestra la entrada y anota dónde está la cabeza
    /// (en una memoria interna de la cámara, si tiene): tocar la foto lleva la
    /// cámara ahí (apuntarEnFoto). {ok, error, aviso}.
    Q_INVOKABLE QVariantMap sacarFoto(const QString& entrada);
    /// {hay, url, conMemoria (puede volver a donde se sacó), movida (se movió desde
    /// entonces), cuando}.
    Q_INVOKABLE QVariantMap foto(const QString& entrada) const;
    /// Apunta al punto (u, v) de la FOTO (y, con ancho/alto, encuadra ese cuadro):
    /// si la cámara no se movió desde la foto, ya; si se movió, primero vuelve a
    /// donde estaba (su memoria) y después apunta. "" o el error.
    Q_INVOKABLE QString apuntarEnFoto(const QString& entrada, double u, double v, double ancho, double alto);

    /// Para MCP: el estado de una entrada (o de todas), como json.
    QJsonObject estadoJson(const QString& entrada = {}) const;

   signals:
    void cambio();
    void busquedaCambio();
    /// Lo que salió mal en algo que terminó después (la foto de una posición).
    void aviso(const QString& texto);

   private:
    struct Camara;
    Camara* camara(const QString& entrada, QString* error);
    QString guardarMiniatura(const QString& entrada, const QString& idONombre, const QImage& img);
    /// El backend que recibe el joystick y los presets.
    PtzBackend* activo(Camara& c) const;
    /// Mueve (o para, con todo en 0) sin cortar el pulso de `apuntar`.
    void pulsar(const QString& entrada, double pan, double tilt, double zoom);
    void crearHardware(Camara& c);
    void aplicarFiltros(Camara& c, bool mejoraGeneral);
    QVariantMap fila(const Camara& c) const;
    void leer();
    void guardarPronto();
    void guardar();
    std::string nombreClave(const std::string& entrada) const;
    /// Toma donde está ahora la cámara y lo pone en `p` (la parte de la
    /// cámara puede llegar después: ONVIF contesta el token). "" o el error;
    /// `aviso` lo que no se pudo.
    QString registrar(Camara& c, Posicion& p, QString& aviso);
    /// La foto, un rato después (el recorte digital tarda en llegar).
    void miniaturaPronto(const QString& entrada, const QString& id);
    std::filesystem::path carpetaMiniaturas() const;
    /// La foto de un preset (de la cámara o digital): Miniaturas/ptz/<clave>-preset-<cam|dig>-<n>.jpg.
    QString nombreFotoPreset(const Camara& c, bool digital, int n) const;
    /// Los presets de antes (los digitales del filtro y los de la cámara con foto)
    /// pasan a ser posiciones con su número. true si migró alguno.
    bool migrarPresets(Camara& c, const QJsonObject& digitales);
    /// Los presets que la cámara trae con nombre (ONVIF) y todavía no son posiciones.
    void importarPresetsDeLaCamara(Camara& c);
    /// Un nombre que no esté en la cámara: `base`, o «base (2)»...
    QString nombreLibre(const Camara& c, const QString& base) const;

    std::function<void(const QString&)> dejarDeSeguir_;
    bool interlockActivo_ = true;
    bool seguirSuavePermitido_ = true;
    AppRuntime& rt_;
    std::filesystem::path archivo_;
    QString credenciales_;
    std::map<std::string, std::unique_ptr<Camara>> camaras_; ///< todas las conocidas (por nombre)
    std::map<std::string, int>
        pulsos_; ///< por entrada: el pulso óptico vigente (uno nuevo cancela el anterior)
    std::map<std::string, QString> tipoEntrada_; ///< las presentes ahora: hardware | red | ndi
    std::unique_ptr<OnvifBusqueda> busqueda_;
    QTimer revision_;
    QTimer guardado_;
};

} // namespace ba::app::ptz
