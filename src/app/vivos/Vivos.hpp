// ============================================================================
// src/app/vivos/Vivos.hpp — las cuentas de cada plataforma y los vivos de cada salida
//
// docs/funciones/streaming-cuentas.md. Lo que el operador (y el asistente) arma:
//
//   - VARIAS CUENTAS POR PLATAFORMA (YouTube, Twitch, Facebook). Son del APARATO, no del
//     evento: la lista (sin secretos) y el token de cada una van cifrados al almacén de
//     credenciales (Credenciales/, security/CredentialStore). La de siempre es la
//     «principal» (su token es el de antes: «youtube-token»…, así una instalación vieja
//     sigue con su cuenta sin migrar nada); cada cuenta más tiene el suyo
//     («youtube-token-cuenta-2»). La app de la plataforma (el cliente OAuth) es una sola.
//     Los ids no se reusan (un destino de otro evento no termina apuntando a otra cuenta).
//   - Los DESTINOS de cada salida (Config/destinos.json, del EVENTO): además de los RTMP/SRT
//     de siempre (URL + clave cifrada «stream-<id>»), uno puede ser (plataforma, cuenta,
//     vivo): `plataforma`, `cuenta`, `vivo` (el id del vivo en la plataforma; en Twitch
//     vacío: el vivo es el canal) y `vivo_titulo`. Al asignarlo se le pide a la plataforma
//     la ingesta y queda como cualquier destino (URL y clave cifrada): arranca, se corta y
//     vuelve tras un corte de luz igual que los demás, sin red para la clave.
//   - Que dos destinos no manden al MISMO vivo (se pisarían): `chocaCon` antes de guardar y
//     `choques` para avisar lo que ya está guardado así.
//   - El ESTADO de cada destino para el operador y el MCP: detenido, conectando, en vivo,
//     reintentando; la salud que informa la plataforma y el bitrate que sale.
//
// Sin Qt ni red: el registro, los destinos y el servicio se prueban con una plataforma de
// mentira (tests/test_vivos.cpp). Las plataformas de verdad (los clientes HTTPS) están en
// vivos/PlataformasVivo.*, la vista en ui/VivosViewModel y el MCP en mcp/McpVivosTools.
// ============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::app::vivos {

using json = nlohmann::json;

/// La cuenta de siempre de cada plataforma (la de la página de la plataforma).
inline constexpr const char* kPrincipal = "principal";
/// Dónde se guarda la lista de cuentas (cifrada, en Credenciales/).
inline constexpr const char* kCredencialLista = "cuentas-plataformas";

/// Las plataformas con cuentas: youtube, twitch, facebook.
const std::vector<std::string>& plataformas();
bool esPlataforma(const std::string& p);
/// Los destinos genéricos (sin cuenta): rtmp, srt, rist.
bool esGenerica(const std::string& p);
/// "YouTube", "Twitch", "Facebook", "RTMP", "SRT", "RIST" (o lo que vino).
std::string nombrePlataforma(const std::string& p);

/// La credencial con el token de una cuenta: la principal, la de siempre («youtube-token»);
/// las demás, «<plataforma>-token-<cuenta>».
std::string credencialDeCuenta(const std::string& plataforma, const std::string& cuenta);

/// Una cuenta guardada (sin secretos).
struct Cuenta {
    std::string plataforma;
    std::string id;     ///< "principal", "cuenta-2"…
    std::string nombre; ///< el que le puso el operador
    std::string canal;  ///< el canal, usuario o Página que dijo la plataforma al conectarla
    bool conectadaAntes = false; ///< estuvo conectada: si ya no lo está, se venció o la revocaron
};

/// El estado de una cuenta, para el operador y el MCP:
/// sin_app (falta la app de la plataforma), sin_conectar, conectando, conectada,
/// vencida (estuvo conectada y el token ya no sirve), sin_permiso (conectada, pero el
/// permiso no alcanza: hay que reautorizar).
std::string estadoCuenta(bool configurada, bool conectada, bool conectando, bool sinPermiso, bool conectadaAntes);
/// El texto de ese estado (traducido).
std::string textoEstadoCuenta(const std::string& estado);

/// La lista de cuentas de todas las plataformas.
class RegistroCuentas {
   public:
    /// Dónde vive la lista: `leer` (null si no hay) y `escribir` (false si no se pudo).
    struct Almacen {
        std::function<json()> leer;
        std::function<bool(const json&)> escribir;
    };
    explicit RegistroCuentas(Almacen a) : a_(std::move(a)) {}

    /// Lee la lista y la deja al día: cada plataforma con su principal (una instalación de
    /// antes de las cuentas tiene solo esa: su token es el de siempre). Lo roto se descarta.
    void cargar();
    /// Las de una plataforma (la principal primera) o de todas ("").
    std::vector<Cuenta> de(const std::string& plataforma = {}) const;
    const Cuenta* buscar(const std::string& plataforma, const std::string& id) const;
    /// Una cuenta más (sin conectar): su id, en `idOut`. "" o por qué no.
    std::string agregar(const std::string& plataforma, const std::string& nombre, std::string* idOut);
    std::string renombrar(const std::string& plataforma, const std::string& id, const std::string& nombre);
    /// La saca de la lista (la principal no: esa se desconecta). "" o por qué no. El token lo
    /// borra quien llama (la plataforma revoca y olvida).
    std::string quitar(const std::string& plataforma, const std::string& id);
    /// Se conectó (o se releyó): el canal que dijo la plataforma. Guarda si cambió.
    void anotarConexion(const std::string& plataforma, const std::string& id, const std::string& canal);
    /// Se desconectó a propósito: deja de contar como «vencida».
    void anotarDesconexion(const std::string& plataforma, const std::string& id);
    json aJson() const;

   private:
    bool guardar();
    Cuenta* buscarMut(const std::string& plataforma, const std::string& id);

    Almacen a_;
    std::vector<Cuenta> cuentas_;
    std::map<std::string, int> siguiente_; ///< el próximo número de cada plataforma (no se reusan)
};

// --- Destinos (Config/destinos.json) ---------------------------------------------------

/// La plataforma de un destino guardado: la suya o, uno de antes (sin campo), la del
/// esquema de su URL (rtmp, srt, rist).
std::string plataformaDe(const json& destino);
/// Qué vivo identifica a un destino: «youtube:<cuenta>:<vivo>», «twitch:<cuenta>»,
/// «facebook:<cuenta>:<vivo>» o, uno genérico, «url:<url sin la / final>#<huella de su
/// clave>». "" si no se puede saber (sin URL).
std::string vivoDe(const json& destino, const std::string& huellaClave);
/// Una huella de una clave (para comparar sin guardarla): "" si no hay clave.
std::string huella(const std::string& clave);

/// Dos o más destinos guardados que mandan al mismo vivo.
struct Choque {
    std::string vivo;
    std::vector<std::string> destinos;
    std::vector<std::string> salidas; ///< sin repetir, en el orden de los destinos
};
/// Los choques de lo guardado. `huellaDe(id)`: la huella de la clave guardada del destino.
std::vector<Choque> choques(const json& destinos, const std::function<std::string(const std::string&)>& huellaDe);
/// Antes de guardar `nuevo` (con su `salida`; con `id` si reemplaza a uno): "" o el aviso
/// si otro destino ya manda a ese vivo. `huellaNuevo`: la de la clave nueva (o, si no trae,
/// la guardada). `nombreSalida(id)`: para el aviso.
std::string chocaCon(const json& destinos, const json& nuevo, const std::string& huellaNuevo,
                     const std::function<std::string(const std::string&)>& huellaDe,
                     const std::function<std::string(const std::string&)>& nombreSalida);

/// El estado de un destino: detenido, conectando (arrancó y todavía no salió nada),
/// en_vivo, reintentando (se cortó y reconecta).
std::string estadoDestino(bool activo, bool reconectando, std::int64_t bytes, double kbps);
std::string textoEstadoDestino(const std::string& estado);

/// Lo que pide asignar un destino a una salida.
struct Asignacion {
    std::string id;         ///< "" = uno nuevo; si no, reemplaza a ese
    std::string salida;     ///< el id de la salida (ya resuelto)
    std::string plataforma; ///< youtube | twitch | facebook | rtmp | srt | rist ("" = por la URL)
    std::string cuenta;     ///< las de cuenta ("" = la principal)
    std::string vivo;       ///< youtube/facebook: el id del vivo
    std::string vivoTitulo;
    std::string nombre;     ///< "" = uno armado (cuenta · vivo)
    std::string url;        ///< los genéricos
    std::string clave;      ///< los genéricos (va cifrada; nunca a destinos.json)
    bool respaldo = false;
};
/// El destino para guardar (sin la clave, que va aparte). "" o por qué no.
std::string armarDestino(const Asignacion& a, json& out);

// --- Las plataformas ---------------------------------------------------------------------

struct Resultado {
    bool ok = false;
    json datos = json::object();
    std::string error;
    static Resultado bien(json d = json::object()) { return {true, std::move(d), {}}; }
    static Resultado mal(std::string e) { return {false, json::object(), std::move(e)}; }
};
using Listo = std::function<void(const Resultado&)>;

/// Una plataforma con cuentas: lo que hace falta para listar, crear y enlazar vivos. Las
/// respuestas llegan en el hilo de quien pidió (las reales, en el de la interfaz).
class Plataforma {
   public:
    virtual ~Plataforma() = default;
    virtual std::string id() const = 0;
    /// true si los vivos son cosas aparte (YouTube, Facebook); false si el vivo es el canal
    /// (Twitch: una clave por cuenta).
    virtual bool vivosAparte() const = 0;
    /// Está la app de la plataforma (el cliente OAuth).
    virtual bool configurada() const = 0;
    virtual bool conectada(const std::string& cuenta) const = 0;
    virtual bool conectando(const std::string& cuenta) const = 0;
    virtual bool sinPermiso(const std::string& cuenta) const = 0;
    /// El canal, usuario o Página conectada ("" si no hay).
    virtual std::string canal(const std::string& cuenta) const = 0;
    /// Conectar (o reautorizar): abre el navegador; `listo` al terminar.
    virtual void conectar(const std::string& cuenta, Listo listo) = 0;
    virtual void cancelarConexion(const std::string& cuenta) = 0;
    /// Revoca y borra el token de esa cuenta.
    virtual void desconectar(const std::string& cuenta) = 0;
    /// Suelta todo lo de una cuenta que se quitó (su cliente).
    virtual void olvidar(const std::string& cuenta) = 0;
    /// Los vivos de la cuenta: {items: [{id, titulo, estado, privacidad, inicio, url}]}.
    /// Twitch: uno solo, el canal (id "").
    virtual void vivos(const std::string& cuenta, Listo listo) = 0;
    /// Uno nuevo: datos {titulo, descripcion, privacidad, inicio (ISO 8601), latencia}.
    /// -> {vivo: {id, titulo, …}}.
    virtual void crearVivo(const std::string& cuenta, const json& datos, Listo listo) = 0;
    /// La ingesta del vivo (en YouTube, si no tiene stream, se crea y se enlaza):
    /// {url, clave, estado_stream?, salud?}. La clave solo para guardarla cifrada.
    virtual void ingesta(const std::string& cuenta, const std::string& vivo, Listo listo) = 0;
    /// La salud que informa la plataforma: {estado_stream, salud, estado_vivo, espectadores?}.
    virtual void salud(const std::string& cuenta, const std::string& vivo, Listo listo) = 0;
};

/// Las cuentas, los destinos de plataforma y su estado: una sola lógica para la página, la
/// página de Salidas y el MCP.
class ServicioVivos {
   public:
    struct Partes {
        RegistroCuentas& cuentas;
        /// La plataforma por su id (nullptr si no hay).
        std::function<Plataforma*(const std::string&)> plataforma;
        /// Los destinos guardados (destinos.json, con `salida`).
        std::function<json()> destinos;
        /// La huella de la clave guardada de un destino ("" si no tiene).
        std::function<std::string(const std::string& destino)> huellaDe;
        /// Guarda el destino (y su clave, si trae «clave»): `listo(error, id)`.
        std::function<void(const json& destino, std::function<void(const std::string& error, const std::string& id)>)> guardar;
        /// El nombre de una salida (para los avisos); "" si no existe.
        std::function<std::string(const std::string& salida)> nombreSalida;
    };
    explicit ServicioVivos(Partes p) : p_(std::move(p)) {}

    /// Las cuentas con su estado: [{plataforma, id, nombre, canal, estado, estado_texto,
    /// principal}]. "" = de todas.
    json cuentas(const std::string& plataforma = {}) const;
    /// "" si existe y está conectada; si no, por qué (para el operador).
    std::string cuentaLista(const std::string& plataforma, const std::string& cuenta) const;

    /// Asigna un destino a una salida (ver Asignacion): valida, mira que nadie más mande
    /// a ese vivo, pide la ingesta a la plataforma y lo guarda. `listo(error, id)`.
    void asignar(Asignacion a, std::function<void(const std::string& error, const std::string& id)> listo);

    /// El estado de cada destino: `conEstado` es lo de AppRuntime::destinos (con activo,
    /// reconectando, bytes) y `kbps` {id: kbps} lo que sale. -> [{id, nombre, salida,
    /// plataforma, plataforma_nombre, cuenta, cuenta_nombre, vivo, vivo_titulo, estado,
    /// estado_texto, kbps, activo, reconectando, respaldo, salud {…}, choque}]. `salida` ""
    /// = de todas.
    json estado(const json& conEstado, const json& kbps, const std::string& salida = {}) const;
    /// Pide la salud de los destinos de plataforma que están saliendo (y olvida la de los
    /// que no); `alCambiar` cuando llega alguna distinta.
    void sondearSalud(const json& conEstado, std::function<void()> alCambiar);
    /// La salud guardada de un destino (vacío si no hay).
    json saludDe(const std::string& destino) const;

   private:
    Partes p_;
    std::map<std::string, json> salud_; ///< por destino
    /// Las respuestas que llegan después de que el servicio se fue no lo tocan.
    std::shared_ptr<int> vida_ = std::make_shared<int>(0);
};

} // namespace ba::app::vivos
