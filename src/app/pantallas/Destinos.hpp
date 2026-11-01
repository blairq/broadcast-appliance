// ============================================================================
// src/app/pantallas/Destinos.hpp — las pantallas de la red y lo que muestran (modelo)
//
// Lo que sabe la página Pantallas (ui/PantallasViewModel) sin Qt, sin red y sin hilos:
//   - los DESTINOS: cada televisor (descubierto por mDNS o agregado a mano por su IP), lo
//     que sabemos de cómo está y lo que tiene FIJADO (lo que tiene que mostrar siempre);
//   - los CONTENIDOS: el catálogo de lo que se puede proyectar (un mensaje, un QR, la hora,
//     el teleprompter, la votación, el tally, el programa, una escena) y cómo se arma la URL
//     de cada uno con la dirección con que el televisor nos ve;
//   - los CARRUSELES: listas de pasos que rotan en una pantalla;
//   - las reglas: qué se guarda (la IP solo de las agregadas a mano), cuándo se le vuelve a
//     mandar lo fijado (con espera creciente: a un televisor apagado no se lo martilla),
//     qué canales de escena del motor se están usando.
// Hablarle a un aparato (que bloquea), elegir la IP de la interfaz que lo tiene en su red y
// pedirle canales al motor lo hace la vista; lo que vuelve se le pasa acá (`alConsultar`,
// `alMostrar`…). La hora viene como argumento. Con test (test_pantallas_destinos).
// ============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "pantallas/Buscar.hpp"
#include "pantallas/Cast.hpp"

namespace ba::app::pantallas {

// --- El catálogo de lo que se puede proyectar ------------------------------------------------
// Está acá y no en el QML porque también lo necesita el MCP: una sola lista, no dos que se
// desincronizan. Los textos se marcan para traducir (contexto «pantallas») y se traducen al
// mostrarlos (`texto`).
struct Campo {
    const char* clave;
    const char* etiqueta;
    const char* ejemplo;
    bool largo; ///< si va en un cuadro de varias líneas
    /// De dónde salen las opciones, si es una lista y no texto libre:
    /// "escenas" = las escenas armadas (la interfaz las tiene en Studio.scenes);
    /// "qrs" = los QR para elegir (Studio.qr.paraElegir: los del aparato y los guardados).
    const char* opciones = "";
};
struct Contenido {
    const char* id;
    const char* titulo;
    const char* ayuda;
    std::vector<Campo> campos;
    bool disponible;
    const char* porque; ///< si no está disponible
};
const std::vector<Contenido>& contenidos();
const Contenido* contenido(const std::string& id);
/// Un texto del catálogo en el idioma del operador ("" si no hay).
std::string texto(const char* s);
/// Lo que va en vivo (el programa, una escena) entra con un permiso de un solo uso que
/// acuña el gateway al mandarlo: su URL no se arma acá ni entra en un carrusel.
inline bool esEnVivo(const std::string& tipo) { return tipo == "programa" || tipo == "escena"; }

/// Un destino, tal como lo guardamos y lo que sabemos de él.
struct Destino {
    std::string id, nombre, modelo, host;
    int puerto = kPuertoCast;
    /// Agregada a mano por su dirección. SOLO en este caso se guarda la IP: es lo único que
    /// la identifica. Una descubierta se identifica por su nombre de servicio mDNS, que no
    /// cambia aunque el DHCP sí.
    bool fija = false;
    /// El operador le puso nombre: la búsqueda no se lo pisa. Es OTRA cosa que `fija` —las
    /// dos estaban juntas, y renombrar una descubierta hacía que se guardara su IP, que es
    /// justo lo que no hay que hacer—.
    bool nombrePropio = false;
    // --- lo que sabemos de cómo está (no se guarda) ---
    std::string estado = "?";              ///< ? | buscando | libre | mostrando | apagada | error | ocupada
    std::string contenido;                 ///< el tipo que le mandamos
    nlohmann::json ultimosDatos = nlohmann::json::object(); ///< con qué se lo mandamos (para poder fijarlo)
    std::string que;                       ///< legible: "QR — https://…"
    std::string error;
    double volumen = -1;
    bool silenciado = false;
    bool trabajando = false;
    std::int64_t vistaMs = 0;
    // --- Lo que tiene que mostrar SIEMPRE (se guarda) ---
    std::string fijadoTipo;
    nlohmann::json fijadoDatos = nlohmann::json::object();
    // Reintentos del restaurado: un televisor apagado no se martilla.
    int fallos = 0;
    std::int64_t proximoIntentoMs = 0;
    /// -1 no se sabe, 0 no sabe mostrar páginas, 1 sí.
    int puedeMostrar = -1;
    /// El canal de escena que usa (BA.Escena.<k>) si muestra una escena; -1 si no.
    int canalEscena = -1;
    /// Se le pidió un canal al motor y todavía no contestó.
    bool pidiendoCanal = false;
};

/// Un paso de un carrusel: qué mostrar y cuánto.
struct Paso {
    std::string tipo;
    nlohmann::json datos = nlohmann::json::object();
    int segundos = 15;
};
struct Carrusel {
    std::string id, nombre;
    std::string transicion = "fundido";
    std::vector<Paso> pasos;
};

class Destinos {
   public:
    // --- Lo guardado (Config/pantallas.json: las pantallas y los carruseles juntos) ---
    nlohmann::json aJson() const;
    void deJson(const nlohmann::json& raiz);

    const std::vector<Destino>& lista() const { return destinos_; }
    const std::vector<Carrusel>& carruseles() const { return carruseles_; }
    Destino* porId(const std::string& id);
    const Destino* porId(const std::string& id) const;
    Carrusel* carrusel(const std::string& id);
    const Carrusel* carrusel(const std::string& id) const;
    /// Cuántas están mostrando algo.
    int mostrando() const;
    /// ¿Alguna está mostrando el programa (o mandándoselo)? Mientras tanto la vista que lo
    /// baja al gateway no puede descansar.
    bool muestraPrograma() const;
    /// Los canales de escena que usan las pantallas (los que no se sueltan).
    std::set<int> canalesUsados() const;
    /// Alguna espera que el motor le dé un canal: mientras tanto no se suelta ninguno.
    bool pidiendoCanal() const;

    // --- Lo que hace el operador: "" o por qué no (en el idioma del operador) ---
    std::string agregar(const std::string& nombre, const std::string& host, int puerto, std::string* id = nullptr);
    std::string olvidar(const std::string& id);
    std::string renombrar(const std::string& id, const std::string& nombre);
    /// Si se puede fijar (o mostrar) ese tipo en esa pantalla: el contenido existe y está
    /// disponible, o el carrusel existe.
    std::string sePuedeFijar(const std::string& id, const std::string& tipo) const;
    std::string fijar(const std::string& id, const std::string& tipo, const nlohmann::json& datos);
    std::string fijarLoQueMuestra(const std::string& id);
    std::string desfijar(const std::string& id);

    // --- Carruseles ---
    std::string carruselNuevo(const std::string& nombre, std::int64_t ahora);
    std::string carruselBorrar(const std::string& id);
    std::string carruselRenombrar(const std::string& id, const std::string& nombre);
    std::string carruselTransicion(const std::string& id, const std::string& transicion);
    std::string pasoAgregar(const std::string& carrusel, const std::string& tipo, const nlohmann::json& datos, int segundos);
    std::string pasoQuitar(const std::string& carrusel, int indice);
    std::string pasoMover(const std::string& carrusel, int indice, int aIndice);
    std::string pasoSegundos(const std::string& carrusel, int indice, int segundos);
    /// La pantalla a la que van las URLs de un carrusel sin pantalla concreta: la primera
    /// que lo esté mostrando y, si no, cualquiera (para que las URLs tengan una IP razonable).
    std::string destinoDeCarrusel(const std::string& id) const;

    // --- Lo que vuelve de los aparatos ---
    /// Una búsqueda en la red: las encontradas se suman (o se les refresca la dirección y,
    /// si el operador no le puso uno, el nombre). Desde ahora, retoman lo suyo sin esperar.
    void alBuscar(const std::vector<Encontrado>& encontradas, std::int64_t ahora);
    /// Lo que contestó al preguntarle cómo está (pantallas/Cast: consultar).
    void alConsultar(const std::string& id, const Estado& r, std::int64_t ahora);
    /// Se le mandó una URL: true si hay que avisar del error (uno por cada fijada que falla
    /// seguido sería una lluvia de avisos: solo el primero).
    bool alMostrar(const std::string& id, bool ok, const std::string& error, const std::string& tipo,
                   const std::string& que, std::int64_t ahora);
    void alParar(const std::string& id, bool ok, const std::string& error, std::int64_t ahora);
    void alSilenciar(const std::string& id, bool ok, double volumen, bool silenciado);

    /// Las que tienen algo fijado y no lo están mostrando (ni están ocupadas con otra cosa,
    /// ni trabajando, ni esperando su próximo intento): {id, tipo, datos}. Les corre el
    /// próximo intento, cada vez más lejos con los fallos (hasta cinco minutos).
    struct Restaurar {
        std::string id, tipo;
        nlohmann::json datos;
    };
    std::vector<Restaurar> paraRestaurar(std::int64_t ahora);

   private:
    std::vector<Destino> destinos_;
    std::vector<Carrusel> carruseles_;
};

// --- Las URLs -----------------------------------------------------------------------------
/// %XX de todo lo que no sea letra, número o -._~ (como QUrl::toPercentEncoding).
std::string codificar(const std::string& s);
/// La URL de un contenido para un televisor que nos ve en `base` ("http://<ip>:<puerto>"). ""
/// si no se puede (no existe, no está disponible, falta la dirección). `url` va tal cual;
/// lo que va en vivo dice que lo pide el gateway.
std::string url(const std::string& tipo, const nlohmann::json& datos, const std::string& base);
/// Lo legible de un contenido: su título y su primer dato con algo (cortado a `largo` letras).
std::string que(const std::string& tipo, const nlohmann::json& datos, std::size_t largo);

// --- Los canales de escena del motor ---------------------------------------------------------
/// Lo que se mira de un canal de escena del motor (BA.Escena.<k>), en el dueño del motor.
struct CanalesDelMotor {
    int cantidad = 0;
    std::function<std::string(int)> escena;   ///< la escena que compone ("" = libre)
    std::function<bool(int)> conDuenos;       ///< lo usan otros (las vistas de los remotos)
};
/// Los canales que hay que soltar: con escena, sin otros dueños y que ninguna pantalla usa.
std::vector<std::pair<int, std::string>> aSoltar(const CanalesDelMotor& c, const std::set<int>& usados);
/// El canal para mostrar `escena`: el que ya la tiene (dos pantallas con la misma escena la
/// componen una sola vez) o uno libre; -1 si no queda ninguno.
int canalPara(const CanalesDelMotor& c, const std::string& escena);

} // namespace ba::app::pantallas
