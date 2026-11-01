// ============================================================================
// src/app/escenas/RefEscena.hpp — una escena se nombra por su ID, no por su nombre
//
// Cada escena tiene un id estable («esc-1a2b3c4d», SceneConfig::id) que no cambia al
// renombrarla. El motor la conoce por el id (EscenasYFuentes: renombrar no le toca el
// aire), y lo que se guarda apuntando a una escena (la línea de tiempo, las transiciones
// de entrada, el DSK, los atajos, el debate, el respaldo de entradas, al-aire.json)
// guarda el id y, al lado, el nombre para mostrar.
//
// Lo de antes (solo el nombre) se migra al leerlo: el id sale del nombre. Lo que pide una
// persona o un modelo sigue diciendo el NOMBRE (el MCP no cambia): quien lo recibe lo
// resuelve acá. El id manda; el nombre guardado es para leer y para migrar. Quien cambia
// el nombre guardado a mano (otra escena) tiene que sacar el id viejo: si no, el id lo pisa.
//
// Sin Qt ni motor (test_ref_escena).
// ============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::app::escenas {

inline constexpr const char* kPrefijoId = "esc-";

/// Las escenas que hay: (id, nombre), en el orden del operador.
class TablaEscenas {
   public:
    TablaEscenas() = default;
    explicit TablaEscenas(std::vector<std::pair<std::string, std::string>> escenas) : escenas_(std::move(escenas)) {}
    void agregar(std::string id, std::string nombre) { escenas_.emplace_back(std::move(id), std::move(nombre)); }

    /// El id de lo pedido: un id que existe, o el nombre de una escena; "" si ninguna.
    std::string id(const std::string& idONombre) const {
        if (idONombre.empty()) return {};
        for (const auto& [i, n] : escenas_)
            if (i == idONombre) return i;
        for (const auto& [i, n] : escenas_)
            if (n == idONombre) return i;
        return {};
    }
    /// El nombre de lo pedido (por id o por nombre); "" si no está.
    std::string nombre(const std::string& idONombre) const {
        const std::string i = id(idONombre);
        for (const auto& [x, n] : escenas_)
            if (x == i) return n;
        return {};
    }
    bool hay(const std::string& idONombre) const { return !id(idONombre).empty(); }
    bool vacia() const { return escenas_.empty(); }
    const std::vector<std::pair<std::string, std::string>>& todas() const { return escenas_; }

   private:
    std::vector<std::pair<std::string, std::string>> escenas_;
};

/// Un id nuevo («esc-» y 8 hexadecimales) que `usado` no tenga.
inline std::string nuevoIdEscena(const std::function<bool(const std::string&)>& usado) {
    static std::mt19937_64 azar{std::random_device{}()};
    static const char* const hex = "0123456789abcdef";
    for (;;) {
        std::uint64_t r = azar();
        std::string id = kPrefijoId;
        for (int k = 0; k < 8; ++k, r >>= 4) id += hex[r & 0xF];
        if (!usado || !usado(id)) return id;
    }
}

/// Lo que apunta a una escena guardado en un objeto: el nombre en `clave` y el id en
/// `clave + "_id"`. Pone el id si falta (por el nombre, o por un id que vino en el lugar
/// del nombre) y el nombre al día por el id. Un id que ya no existe con un nombre que sí
/// (otra corrida que no llegó a guardar sus ids) toma el de ese nombre. true si cambió algo.
/// `claveId` vacía: `clave + "_id"`.
inline bool seguirRef(nlohmann::json& obj, const TablaEscenas& t, const std::string& clave = "escena", std::string claveId = {}) {
    if (!obj.is_object()) return false;
    if (claveId.empty()) claveId = clave + "_id";
    const auto texto = [&obj](const std::string& k) {
        const auto it = obj.find(k);
        return it != obj.end() && it->is_string() ? it->get<std::string>() : std::string();
    };
    const std::string n = texto(clave), i = texto(claveId);
    std::string id = t.id(i).empty() ? std::string() : i;
    if (id.empty()) id = t.id(n);
    if (id.empty()) return false; // ni el id ni el nombre son de una escena que haya: queda como está
    bool cambio = false;
    if (id != i) {
        obj[claveId] = id;
        cambio = true;
    }
    if (const std::string nombre = t.nombre(id); nombre != n) {
        obj[clave] = nombre;
        cambio = true;
    }
    return cambio;
}

/// Una lista de nombres (escenas o fuentes, como el contenido de una capa DSK) con los
/// ids al lado, en otra lista del mismo largo ("" = no es una escena): seguirRef,
/// elemento por elemento. Unos ids de otro largo (de otra lista) se rehacen por el nombre.
inline bool seguirLista(nlohmann::json& nombres, nlohmann::json& ids, const TablaEscenas& t) {
    if (!nombres.is_array()) return false;
    bool cambio = false;
    if (!ids.is_array() || ids.size() != nombres.size()) {
        ids = nlohmann::json::array();
        for (std::size_t i = 0; i < nombres.size(); ++i) ids.push_back("");
        cambio = true;
    }
    for (std::size_t i = 0; i < nombres.size(); ++i) {
        if (!nombres[i].is_string()) continue;
        nlohmann::json r{{"escena", nombres[i]}, {"escena_id", ids[i].is_string() ? ids[i] : nlohmann::json("")}};
        if (seguirRef(r, t)) {
            nombres[i] = r["escena"];
            ids[i] = r["escena_id"];
            cambio = true;
        }
    }
    return cambio;
}

/// Lo que se le pide al motor de esa lista: el id de cada escena que hay, el nombre de lo demás.
inline std::vector<std::string> listaParaPedir(const nlohmann::json& nombres, const nlohmann::json& ids, const TablaEscenas& t) {
    std::vector<std::string> out;
    if (!nombres.is_array()) return out;
    for (std::size_t i = 0; i < nombres.size(); ++i) {
        if (!nombres[i].is_string()) continue;
        const std::string id = ids.is_array() && i < ids.size() && ids[i].is_string() ? ids[i].get<std::string>() : std::string();
        out.push_back(!id.empty() && t.hay(id) ? id : nombres[i].get<std::string>());
    }
    return out;
}

/// Un objeto con algo POR escena ({<escena>: {...}}, como las transiciones de entrada de
/// transiciones.json): cada valor bajo el id de su escena (los de antes estaban bajo el
/// nombre), con el nombre de ahora al lado (`escena`); lo de una escena que no está, afuera.
/// true si cambió algo.
inline bool seguirClaves(nlohmann::json& porEscena, const TablaEscenas& t) {
    if (!porEscena.is_object()) {
        porEscena = nlohmann::json::object();
        return true;
    }
    if (t.vacia()) return false; // sin escenas a la vista no se sabe cuáles se fueron
    nlohmann::json nuevas = nlohmann::json::object();
    for (const auto& [k, v] : porEscena.items()) {
        const std::string id = t.id(k);
        if (id.empty() || !v.is_object()) continue;
        nlohmann::json x = v;
        x["escena"] = t.nombre(id);
        nuevas[id] = x;
    }
    if (nuevas == porEscena) return false;
    porEscena = std::move(nuevas);
    return true;
}

/// Quien reemplaza lo guardado (`nuevo` sobre `viejo`, p. ej. los datos que devuelve un
/// editor): si cambió el nombre y no el id, el id es de la escena de antes y se va (el
/// nombre nuevo lo vuelve a poner con seguirRef). Si no, el id pisaría lo editado.
inline void soltarIdViejo(const nlohmann::json& viejo, nlohmann::json& nuevo, const std::string& clave = "escena",
                          std::string claveId = {}) {
    if (!viejo.is_object() || !nuevo.is_object()) return;
    if (claveId.empty()) claveId = clave + "_id";
    const auto it = nuevo.find(claveId);
    if (it == nuevo.end()) return;
    if (nuevo.value(clave, nlohmann::json()) != viejo.value(clave, nlohmann::json()) && *it == viejo.value(claveId, nlohmann::json()))
        nuevo.erase(claveId);
}

/// A qué escena apunta lo guardado, para pedírsela al motor (que entiende id y nombre):
/// el id si es de una escena que hay; si no, el nombre (lo de antes, o una escena que falta).
inline std::string apunta(const nlohmann::json& obj, const TablaEscenas& t, const std::string& clave = "escena") {
    if (!obj.is_object()) return {};
    const std::string i = obj.value(clave + "_id", std::string());
    if (!i.empty() && t.hay(i)) return i;
    return obj.value(clave, std::string());
}

/// Lo mismo con el id y el nombre sueltos.
inline std::string apunta(const std::string& id, const std::string& nombre, const TablaEscenas& t) {
    return !id.empty() && t.hay(id) ? id : nombre;
}

} // namespace ba::app::escenas
