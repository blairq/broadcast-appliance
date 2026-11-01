// src/app/rostros/QuienEs.cpp — ver QuienEs.hpp
#include "rostros/QuienEs.hpp"

#if BA_HAVE_ROSTROS // la libreta y el parecido son de ba_core, con el reconocimiento

#include <algorithm>

#include "ajustes/Ajustes.hpp"

namespace ba::rostros {

using nlohmann::json;

// --- El anotado automático, y por qué estos números -------------------------
//
// QUE HACE: con el interruptor puesto, cada cara que pasa por la cámara
// elegida y no cae en ninguna ficha queda anotada SOLA, con su foto y sin
// nombre, para que después alguien le ponga quién es. Y a las que sí reconoce
// les suma la pose nueva, con lo que reconoce mejor cada vez que la ve.
//
// QUE NO HACE: no le pone nombre a nadie ni saca nada al aire. Una ficha sin
// nombre no rotula.
//
// TRES VECES ANTES DE ANOTAR, y no una: un reflejo, una mano o media cara que
// entra y sale dan una detección suelta. Anotando la primera, la libreta se
// llena de fichas de nada y el operador termina borrando más de lo que
// completa. Tres pasadas seguidas ya es alguien que está ahí.
int QuienEs::vecesParaAnotar() { return ba::ajustes::entero("rostros.vecesParaAnotar", 3); }
/// Si esa cara no volvió en este rato, se olvida y empieza de nuevo.
///
/// DOCE SEGUNDOS Y NO CUATRO, por el lobby: ahí la ronda rota entre las
/// posiciones de espera, así que a la misma persona la vuelve a mirar cada
/// varios segundos. Con cuatro, se olvidaba de ella entre vuelta y vuelta y no
/// llegaba nunca a las tres pasadas: el anotado automático no anotaba a nadie.
std::int64_t QuienEs::olvidoPendienteMs() { return ba::ajustes::entero64("rostros.olvidoS", 12) * 1000; }
/// Tope de la libreta para el anotado automático. Llegando acá se apaga solo y
/// lo dice: una libreta de miles de desconocidos no la revisa nadie, y cada
/// búsqueda las recorre a todas.
int QuienEs::maxFichasAuto() { return ba::ajustes::entero("rostros.maxFichas", 500); }
/// Cuánto parecido hace falta para ABRIRLE LA PUERTA a alguien sin preguntar.
///
/// MAS QUE PARA PONERLE UN NOMBRE, y esa diferencia es la decisión. El 0.45 de
/// `kMismaPersona()` está elegido para un rótulo: si se equivoca, alguien lo ve
/// en pantalla y lo corrige. Dejar entrar a la sala no se corrige igual —ya
/// está adentro, con cámara y micrófono, y puede quedar al aire—. Para eso se
/// pide un margen que no deja lugar a un parecido de familia: medido, la misma
/// persona da 0.72-0.90 y dos personas distintas ~0.13.
///
/// Y ADEMAS la cara tiene que servir: un reconocimiento hecho sobre una cara
/// borrosa o de perfil puede dar cualquier número, y ese número no puede ser
/// el que abre la puerta.
float QuienEs::abrirLaPuerta() { return static_cast<float>(ba::ajustes::numero("rostros.admitirSolo", 0.60)); }
/// «La vi recién» se guarda cada tanto, no en cada vuelta: escribir la libreta
/// entera —vectores incluidos— dos veces por segundo es todo disco para nada.
/// Un alta o una cara nueva sí se guardan en el acto.
std::int64_t QuienEs::guardarVistasMs() { return ba::ajustes::entero64("rostros.guardarVistasS", 30) * 1000; }

Coincidencia QuienEs::quien(const std::vector<float>& vector) const {
    Coincidencia q = libreta_.buscar(vector);
    // El umbral es el de un rótulo (kMismaPersona): errar hacia «no sé» antes que hacia
    // «es fulano». Debajo, no se sabe quién es (el parecido queda, para mostrarlo).
    if (q.parecido < kMismaPersona()) {
        q.id.clear();
        q.nombre.clear();
    }
    return q;
}

QuienEs::Resultado QuienEs::resolver(const std::vector<Visto>& caras, bool registrar, std::int64_t ahora) {
    Resultado r;
    // Las pendientes que no volvieron a aparecer se olvidan acá y no dentro
    // del lazo: si no se ve ninguna cara, igual hay que soltarlas.
    if (registrar)
        pendientes_.erase(std::remove_if(pendientes_.begin(), pendientes_.end(),
                                         [&](const Pendiente& p) { return ahora - p.ultimoMs > olvidoPendienteMs(); }),
                          pendientes_.end());
    bool vista = false; // solo «la vi recién»: guardar cada tanto
    for (std::size_t i = 0; i < caras.size(); ++i) {
        const Visto& visto = caras[i];
        Coincidencia q = libreta_.buscar(visto.vector);
        bool seguro = q.parecido >= kMismaPersona();
        if (registrar && !visto.vector.empty()) {
            if (seguro) {
                libreta_.vistaAhora(q.id, ahora);
                vista = true;
                // La pose de ahora, sumada a la ficha: es lo que hace que la
                // próxima vez la reconozca de costado, con otra luz o con
                // anteojos. La libreta descarta las que no aportan.
                if (libreta_.sumarCara(q.id, visto.vector) == Personas::Alta::Sumada) r.alta = true;
            } else if (!r.lleno) {
                const std::string nuevo = anotarDesconocida(visto.vector, ahora, r.lleno);
                if (!nuevo.empty()) {
                    q.id = nuevo;
                    q.nombre.clear();
                    q.parecido = 1.0F;
                    seguro = true;
                    r.alta = true;
                    r.fotos.emplace_back(nuevo, i);
                }
            }
        }
        r.filas.push_back({seguro ? q.id : std::string(), seguro ? q.nombre : std::string(), q.parecido, visto.cara});
    }
    r.guardarVistas = !r.alta && vista && ahora - ultimoGuardado_ > guardarVistasMs();
    return r;
}

std::string QuienEs::anotarDesconocida(const std::vector<float>& v, std::int64_t ahora, bool& lleno) {
    for (auto it = pendientes_.begin(); it != pendientes_.end(); ++it) {
        Pendiente& p = *it;
        if (parecido(p.v, v) < kMismaPersona()) continue;
        p.v = v; // el último visto: la cara suele ir mejorando al acercarse
        p.ultimoMs = ahora;
        if (++p.veces < vecesParaAnotar()) return {};
        if (static_cast<int>(libreta_.todas().size()) >= maxFichasAuto()) {
            // Se apaga el anotado (lo hace quien lo usa): lo que estaba a medio ver no sigue.
            lleno = true;
            pendientes_.clear();
            return {};
        }
        // SIN NOMBRE A PROPOSITO. El sistema puede decir «este que está acá es
        // el mismo de recién»; quién es lo dice una persona. Una ficha sin
        // nombre no rotula nada: es una fila esperando que la completen.
        Persona nueva;
        nueva.vistaMs = ahora;
        const std::string id = libreta_.guardarFicha(nueva);
        libreta_.sumarCara(id, v);
        pendientes_.erase(it);
        return id;
    }
    pendientes_.push_back(Pendiente{v, 1, ahora});
    return {};
}

bool QuienEs::vistas(const std::vector<std::string>& ids, std::int64_t ahora) {
    if (ids.empty()) return false;
    for (const std::string& id : ids) libreta_.vistaAhora(id, ahora);
    return ahora - ultimoGuardado_ > guardarVistasMs();
}

int QuienEs::laMasGrande(const std::vector<Visto>& v) {
    int mejor = -1;
    for (int i = 0; i < static_cast<int>(v.size()); ++i)
        if (mejor < 0 || v[static_cast<std::size_t>(i)].cara.alto > v[static_cast<std::size_t>(mejor)].cara.alto) mejor = i;
    return mejor;
}

Coincidencia QuienEs::identificar(const Visto& v, std::int64_t ahora) {
    const Coincidencia q = quien(v.vector);
    if (!q.id.empty()) libreta_.vistaAhora(q.id, ahora);
    return q;
}

std::optional<float> QuienEs::comparar(const Visto& v, const std::string& id) const {
    const Persona* f = libreta_.porId(id);
    if (f == nullptr) return std::nullopt;
    float mejor = 0.0f;
    for (const auto& c : f->caras) mejor = std::max(mejor, parecido(v.vector, c));
    return mejor;
}

QuienEs::Alta QuienEs::alta(const Visto& v, const std::string& nombre, const std::string& cargo) {
    Alta a;
    // Si ya es de alguien con nombre, no se crea otra ficha: dos fichas para la misma
    // cara reconocen a cualquiera de las dos.
    const Coincidencia c = libreta_.buscar(v.vector);
    const bool esDeAlguien = !c.id.empty() && c.parecido >= kMismaPersona();
    if (esDeAlguien && !c.nombre.empty()) {
        a.id = c.id;
        return a;
    }
    Persona p;
    if (esDeAlguien) p = *libreta_.porId(c.id); // la anotó sola, sin nombre: se completa
    p.nombre = nombre;
    p.cargo = cargo;
    a.id = libreta_.guardarFicha(p);
    libreta_.sumarCara(a.id, v.vector);
    a.cambio = true;
    const Persona* f = libreta_.porId(a.id);
    a.sinFoto = f != nullptr && f->foto.empty();
    return a;
}

std::optional<std::string> QuienEs::etiquetas(const std::string& feed, const json& caras) {
    // A la tarjeta, QUIÉN ES cada cara (por su número): su overlay muestra el
    // nombre y el cargo, y el resto de la cadena los recibe en las metas y los
    // eventos de cada cara. Con cada resultado que trae vectores; solo si cambió.
    json out = json::array();
    bool hayVectores = false;
    for (const auto& c : caras.is_array() ? caras : json::array()) {
        if (!c.contains("vector") || !c["vector"].is_array()) continue;
        hayVectores = true;
        const Coincidencia q = quien(c["vector"].get<std::vector<float>>());
        const Persona* p = q.id.empty() ? nullptr : libreta_.porId(q.id);
        if (p == nullptr || p->nombre.empty()) continue;
        out.push_back({{"id", c.value("id", 0)}, {"nombre", p->nombre}, {"cargo", p->cargo}, {"persona", q.id}});
    }
    const std::string texto = out.dump(-1, ' ', false, json::error_handler_t::replace);
    if (!hayVectores || etiquetasDe_[feed] == texto) return std::nullopt;
    etiquetasDe_[feed] = texto;
    return texto;
}

std::vector<std::size_t> QuienEs::paraAcreditar(const Resultado& r, const std::vector<Visto>& caras) const {
    // El mismo margen que para abrir la puerta de la sala, y por la misma razón:
    // acreditar al que no es deja a dos personas mal anotadas —una que no vino y
    // otra que sí— y nadie lo descubre hasta que hay que contar el quórum.
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < r.filas.size() && i < caras.size(); ++i)
        if (!r.filas[i].id.empty() && r.filas[i].parecido >= abrirLaPuerta() && caras[i].sirve) out.push_back(i);
    return out;
}

QuienEs::Turno QuienEs::proximoDelLobby(const std::vector<std::pair<std::string, int>>& gente) {
    Turno t;
    // Lo que se sabía de quien ya no está esperando deja de valer en el acto:
    // dejarlo puesto haría que la baldosa del próximo que ocupe su lugar
    // muestre el nombre del anterior.
    for (auto it = enLobby_.begin(); it != enLobby_.end(); ++it) {
        const std::string uid = it.key();
        if (std::none_of(gente.begin(), gente.end(), [&](const auto& g) { return g.first == uid; })) t.idos.push_back(uid);
    }
    for (const std::string& uid : t.idos) {
        enLobby_.erase(uid);
        nombradas_.erase(uid);
    }
    if (gente.empty()) return t;
    // Por turno, uno por vuelta: el costo es el mismo haya uno esperando o haya diez.
    turnoLobby_ = (turnoLobby_ + 1) % static_cast<int>(gente.size());
    t.uid = gente[static_cast<std::size_t>(turnoLobby_)].first;
    t.lugar = gente[static_cast<std::size_t>(turnoLobby_)].second;
    return t;
}

void QuienEs::enLobby(const std::string& uid, int lugar, const Resultado& r, const std::vector<Visto>& caras, int cw,
                      int ch) {
    // UNA cara por posición, la más grande: si se ven dos, la que importa es la de
    // quien está sentado frente a la cámara, no la de quien pasa por atrás. Se guarda
    // por la PERSONA, no por el número de posición: las posiciones se reciclan y lo que
    // se supo de una se le pegaría al siguiente que la ocupe.
    const int mejor = laMasGrande(caras);
    if (mejor < 0 || static_cast<std::size_t>(mejor) >= r.filas.size()) {
        enLobby_.erase(uid);
        return;
    }
    const Fila& f = r.filas[static_cast<std::size_t>(mejor)];
    const Visto& v = caras[static_cast<std::size_t>(mejor)];
    const Persona* p = f.id.empty() ? nullptr : libreta_.porId(f.id);
    enLobby_[uid] = {{"id", f.id},
                     {"nombre", f.nombre},
                     {"parecido", f.parecido},
                     {"x", f.cara.x},
                     {"y", f.cara.y},
                     {"ancho", f.cara.ancho},
                     {"alto", f.cara.alto},
                     {"cw", cw},
                     {"ch", ch},
                     // Lo que hace falta para PEDIRLE algo: si la cara sirve y, si no,
                     // qué decirle. Sin esto la baldosa solo sabría decir «no sé».
                     {"sirve", v.sirve},
                     {"sugerencia", v.sugerencia},
                     {"cargo", p != nullptr ? p->cargo : std::string()},
                     {"caras", p != nullptr ? static_cast<int>(p->caras.size()) : 0},
                     {"foto", p != nullptr ? p->foto : std::string()},
                     // El lugar va ADENTRO: la clave es la persona, así que sin esto se
                     // perdía de qué cámara salió.
                     {"uid", uid},
                     {"lugar", lugar}};
}

bool QuienEs::podriaPasar(const std::string& uid, float parecido, bool sirve) const {
    // Una sola vez por persona que espera: si el operador la rechaza a mano,
    // volver a hacerla pasar en la vuelta siguiente sería pelearle al operador.
    if (yaPasaron_.count(uid) > 0) return false;
    return parecido >= abrirLaPuerta() && sirve;
}

std::optional<Persona> QuienEs::nombrar(const std::string& uid, const std::string& fichaId, bool sirve,
                                        const std::string& nombre, const std::string& cargo, bool* propia) const {
    if (fichaId.empty() || !sirve || nombre.empty()) return std::nullopt;
    const Persona* f = libreta_.porId(fichaId);
    if (f == nullptr) return std::nullopt;
    // Solo una ficha SIN nombre (la anotó sola el reconocimiento), o la que
    // nombró esto mismo para esta persona (le corrigieron el nombre en el
    // lobby). Una ficha que alguien nombró a mano no se toca: el nombre que
    // escribió un operador vale más que el que tipeó alguien en su celular.
    const auto ya = nombradas_.find(uid);
    const bool suya = ya != nombradas_.end() && ya->second == fichaId;
    if (propia != nullptr) *propia = suya;
    if (!f->nombre.empty() && !suya) return std::nullopt;
    if (f->nombre == nombre && (cargo.empty() || f->cargo == cargo)) return std::nullopt;
    Persona p = *f;
    p.nombre = nombre;
    if (!cargo.empty()) p.cargo = cargo;
    return p;
}

} // namespace ba::rostros

#endif // BA_HAVE_ROSTROS
