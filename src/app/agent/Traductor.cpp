// src/app/agent/Traductor.cpp — ver Traductor.hpp

#include "agent/Traductor.hpp"

#include "agent/FlujosTexto.hpp"
#include "agent/ModelosLocales.hpp"
#include "platform/Paths.hpp"
#include "plugins/Hijo.hpp"

#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QPointer>
#include <QProcessEnvironment>
#include <QTimer>

#include <algorithm>
#include <fstream>

namespace fs = std::filesystem;

namespace ba::app::agent {

namespace {
using json = nlohmann::json;

QString q(const fs::path& p) { return QString::fromStdWString(p.wstring()); }
QString q(const std::string& s) { return QString::fromStdString(s); }

QVariant qv(const json& j) {
    if (j.is_array() || j.is_object()) return QJsonDocument::fromJson(QByteArray::fromStdString(j.dump())).toVariant();
    return QJsonDocument::fromJson(QByteArray::fromStdString(json::array({j}).dump())).toVariant().toList().value(0);
}
json dejson(const QVariant& v) {
    const json j = json::parse(QJsonDocument::fromVariant(QVariantList{v}).toJson(QJsonDocument::Compact).toStdString(), nullptr, false);
    return j.is_array() && !j.empty() ? j[0] : json();
}
json leerJson(const fs::path& p) {
    std::ifstream f(p);
    if (!f) return json();
    json j = json::parse(f, nullptr, false);
    return j.is_discarded() ? json() : j;
}
void escribirJson(const fs::path& p, const json& j) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    const fs::path tmp = p.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << j.dump(1);
    }
    fs::rename(tmp, p, ec);
}
/// Las variantes en castellano de un término (una o varias), la primera es su clave.
std::vector<std::string> variantes(const json& e) {
    std::vector<std::string> v;
    const json es = e.value("es", json());
    if (es.is_string()) v.push_back(es.get<std::string>());
    else if (es.is_array())
        for (const auto& x : es)
            if (x.is_string() && !x.get<std::string>().empty()) v.push_back(x.get<std::string>());
    return v;
}
std::string clave(const json& e) {
    const auto v = variantes(e);
    return v.empty() ? std::string() : QString::fromStdString(v[0]).toLower().toStdString();
}
const char* kIdiomas[] = {"es", "en", "pt", "fr", "de", "ru", "zh"};
constexpr int kMaxCaidas = 5;
} // namespace

const std::map<std::string, std::string>& Traductor::nombresIdiomas() {
    static const std::map<std::string, std::string> m{{"es", "castellano"}, {"en", "inglés"},  {"pt", "portugués (Brasil)"},
                                                      {"fr", "francés"},    {"de", "alemán"},  {"ru", "ruso"},
                                                      {"zh", "chino mandarín"}};
    return m;
}

Traductor::Traductor(fs::path dataRoot, ModelosLocales& modelos, FlujosTexto& flujos, QObject* parent)
    : QObject(parent), root_(std::move(dataRoot)), modelos_(modelos), flujos_(flujos) {
    QFile f(":/agent/glosario-traduccion.json");
    if (f.open(QIODevice::ReadOnly)) {
        const json g = json::parse(f.readAll().toStdString(), nullptr, false);
        if (g.is_object()) fabrica_ = g.value("terminos", json::array());
    }
    leer();
    leerGlosario();
    connect(&flujos_, &FlujosTexto::frase, this, &Traductor::alFrase);
    connect(&modelos_, &ModelosLocales::descargaCambio, this, [this]() {
        if (!modelos_.descargando()) {
            emit cambio();
            seguirBajando();
        }
    });
}

Traductor::~Traductor() {
    if (hijo_) {
        hijo_->disconnect(this);
        hijo_->escribir("{\"op\":\"salir\"}\n");
        hijo_->terminar();
    }
}

// --- Configuración --------------------------------------------------------------------

void Traductor::leer() {
    const json j = leerJson(root_ / "Config" / "traducciones.json");
    traducciones_ = j.is_object() ? j.value("traducciones", json::array()) : json::array();
}

void Traductor::guardar() const { escribirJson(root_ / "Config" / "traducciones.json", {{"traducciones", traducciones_}}); }

json Traductor::lista() const {
    json out = json::array();
    for (json t : traducciones_) {
        std::string err;
        pasos(t.value("idioma_desde", std::string("es")), t.value("a", std::string()), &err);
        const json f = faltan(t.value("idioma_desde", std::string("es")), t.value("a", std::string()));
        t["faltan"] = f;
        t["estado"] = !err.empty() ? err
                      : !f.empty() ? "falta bajar " + std::to_string(f.size()) + " paquete(s)"
                      : !t.value("activa", true) ? "pausada"
                      : motor_.startsWith("error") ? motor_.toStdString()
                                                    : "lista";
        const auto u = ultima_.find(t.value("id", std::string()));
        if (u != ultima_.end()) t["ultima"] = u->second;
        out.push_back(t);
    }
    return out;
}

std::string Traductor::agregar(const std::string& desde, const std::string& idiomaDesde, const std::string& a, const std::string& nombre,
                               const json& opciones, json* hecha) {
    const std::string de = idiomaDesde.empty() ? "es" : idiomaDesde;
    if (desde.empty()) return "falta la salida de texto a traducir (`desde`)";
    if (!nombresIdiomas().count(a)) return "idioma «" + a + "» desconocido (hay: es, en, pt, fr, de, ru, zh)";
    if (!nombresIdiomas().count(de)) return "idioma de origen «" + de + "» desconocido";
    std::string err;
    pasos(de, a, &err);
    if (!err.empty()) return err;
    const std::string n = nombre.empty() ? desde + " · " + a : nombre;
    if (n == desde) return "la salida traducida no puede llamarse igual que la original";
    for (const auto& t : traducciones_)
        if (t.value("nombre", "") == n) return "ya hay una traducción que publica en «" + n + "»";
    json t{{"id", "t" + std::to_string(QDateTime::currentMSecsSinceEpoch())},
           {"desde", desde},
           {"idioma_desde", de},
           {"a", a},
           {"nombre", n},
           {"activa", opciones.value("activa", true)},
           {"rotulo", opciones.value("rotulo", std::string())},
           {"archivo", opciones.value("archivo", false)}};
    traducciones_.push_back(t);
    guardar();
    if (hecha) *hecha = t;
    emit cambio();
    emit flujos_.cambio();
    return {};
}

std::string Traductor::ajustar(const std::string& id, const json& cambios) {
    for (auto& t : traducciones_) {
        if (t.value("id", "") != id) continue;
        for (const char* k : {"activa", "rotulo", "archivo", "nombre", "a", "idioma_desde", "espera_ms"})
            if (cambios.contains(k)) t[k] = cambios[k];
        guardar();
        emit cambio();
        emit flujos_.cambio();
        return {};
    }
    return "no hay una traducción «" + id + "»";
}

std::string Traductor::quitar(const std::string& id) {
    const auto n = traducciones_.size();
    traducciones_.erase(std::remove_if(traducciones_.begin(), traducciones_.end(), [&](const json& t) { return t.value("id", "") == id; }),
                        traducciones_.end());
    if (traducciones_.size() == n) return "no hay una traducción «" + id + "»";
    guardar();
    emit cambio();
    emit flujos_.cambio();
    return {};
}

json Traductor::salidas() const {
    json out = json::array();
    for (const auto& t : traducciones_)
        out.push_back({{"flujo", t.value("nombre", "")},
                       {"fuente", t.value("desde", "")},
                       {"origen", "Traducción al " + nombresIdiomas().at(t.value("a", std::string("en"))) + " de «" + t.value("desde", std::string()) + "»"},
                       {"modo", "traduccion"},
                       {"salidas", {{"archivo", t.value("archivo", false)}, {"rotulo", t.value("rotulo", std::string())}}}});
    return out;
}

json Traductor::salidaDe(const std::string& flujo) const {
    for (const auto& s : salidas())
        if (s.value("flujo", "") == flujo) return s;
    return nullptr;
}

// --- Paquetes -----------------------------------------------------------------------------

json Traductor::pasos(const std::string& de, const std::string& a, std::string* error) const {
    json directos = json::object(); // "es>en" -> {repo, archivo, instalado}
    for (const QVariant& v : modelos_.catalogo()) {
        const QVariantMap m = v.toMap();
        if (m.value("tipo").toString() != "traduccion") continue;
        directos[(m.value("de").toString() + ">" + m.value("a").toString()).toStdString()] =
            json{{"repo", m.value("repo").toString().toStdString()},
                 {"archivo", m.value("archivo").toString().toStdString()},
                 {"instalado", m.value("instalado").toBool()}};
    }
    json out = json::array();
    if (de == a) {
        if (error) *error = "el idioma de origen y el de destino son el mismo";
        return out;
    }
    const auto armar = [&](const std::string& x, const std::string& y) {
        json p = directos.contains(x + ">" + y) ? directos[x + ">" + y] : json();
        if (p.is_null()) return false;
        p["de"] = x;
        p["a"] = y;
        out.push_back(p);
        return true;
    };
    if (armar(de, a)) return out;
    out = json::array();
    if (de != "en" && a != "en" && armar(de, "en") && armar("en", a)) return out;
    if (error) *error = "no hay cómo traducir de " + de + " a " + a;
    return json::array();
}

json Traductor::faltan(const std::string& de, const std::string& a) const {
    json out = json::array();
    std::string err;
    for (const auto& p : pasos(de, a, &err))
        if (!p.value("instalado", false)) out.push_back(p.value("repo", ""));
    return out;
}

std::string Traductor::bajar(const std::string& de, const std::string& a) {
    std::string err;
    pasos(de, a, &err);
    if (!err.empty()) return err;
    for (const auto& r : faltan(de, a))
        if (std::find(porBajar_.begin(), porBajar_.end(), r.get<std::string>()) == porBajar_.end()) porBajar_.push_back(r);
    if (porBajar_.empty()) return {};
    seguirBajando();
    return {};
}

void Traductor::seguirBajando() {
    if (modelos_.descargando()) return;
    while (!porBajar_.empty()) {
        const std::string r = porBajar_.front();
        porBajar_.pop_front();
        if (!modelos_.carpetaInstalada(q(r)).empty()) continue;
        modelos_.descargar(q(r));
        return;
    }
}

// --- El motor -------------------------------------------------------------------------------

void Traductor::arrancarMotor() {
    if (hijo_ && hijo_->corriendo()) return;
    const fs::path raiz = ba::platform::bundleRoot();
#if defined(_WIN32)
    const fs::path python = raiz / "python" / "python.exe";
#else
    const fs::path python = raiz / "python" / "bin" / "python3";
#endif
    const fs::path script = raiz / "traductor" / "traductor.py";
    std::error_code ec;
    if (!fs::exists(python, ec) || !fs::exists(script, ec)) {
        motor_ = "error: el programa no trae el Python o el traductor";
        emit cambio();
        return;
    }
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const QString& k : env.keys())
        if (k.startsWith("PYTHON")) env.remove(k);
    hijo_ = std::make_unique<plugins::Hijo>();
    connect(hijo_.get(), &plugins::Hijo::salida, this, &Traductor::leerSalida);
    connect(hijo_.get(), &plugins::Hijo::termino, this, [this](int, bool) { alSalir(); });
    QString err;
    if (!hijo_->iniciar(q(python), {"-I", "-B", q(script)}, env, q(raiz), false, &err)) {
        motor_ = "error: " + err;
        hijo_.reset();
        emit cambio();
        return;
    }
    motor_ = "arrancando";
    emit cambio();
}

void Traductor::leerSalida() {
    if (!hijo_) return;
    pendiente_ += hijo_->leerSalida();
    hijo_->leerErrores(); // diagnóstico: no se acumula
    int i;
    while ((i = pendiente_.indexOf('\n')) >= 0) {
        const QByteArray l = pendiente_.left(i);
        pendiente_.remove(0, i + 1);
        const json m = json::parse(l.toStdString(), nullptr, false);
        if (!m.is_object()) continue;
        if (m.value("ev", "") == "listo") {
            motor_ = "listo";
            caidas_ = 0;
            emit cambio();
            mandar();
            continue;
        }
        const long long id = m.value("id", json(-1)).is_number() ? m["id"].get<long long>() : -1;
        auto it = enCurso_.find(id);
        if (it == enCurso_.end()) continue;
        Listo cb = std::move(it->second);
        enCurso_.erase(it);
        cb(m);
    }
}

void Traductor::alSalir() {
    hijo_.reset();
    pendiente_.clear();
    auto pend = std::move(enCurso_);
    enCurso_.clear();
    for (auto& [id, cb] : pend) cb({{"error", "el traductor se cerró"}});
    if (++caidas_ > kMaxCaidas) {
        motor_ = "error: el traductor se cayó varias veces seguidas (ver Logs)";
        for (auto& p : cola_) p.listo({{"error", motor_.toStdString()}});
        cola_.clear();
        emit cambio();
        return;
    }
    motor_ = "apagado";
    emit cambio();
    if (!cola_.empty()) QTimer::singleShot(500, this, [this]() { arrancarMotor(); });
}

void Traductor::mandar() {
    if (motor_ != "listo" || !hijo_) return arrancarMotor();
    // Una a la vez: el motor es secuencial y así una frase no espera a otra que ya no importa.
    while (!cola_.empty() && enCurso_.empty()) {
        Pedido p = std::move(cola_.front());
        cola_.pop_front();
        const long long id = ++n_;
        p.mensaje["id"] = id;
        enCurso_[id] = [this, cb = std::move(p.listo)](json r) {
            cb(std::move(r));
            QTimer::singleShot(0, this, [this]() { mandar(); });
        };
        hijo_->escribir(QByteArray::fromStdString(p.mensaje.dump() + "\n"));
    }
}

void Traductor::traducir(const QString& texto, const std::string& de, const std::string& a, Listo listo) {
    const std::string desde = de.empty() ? "es" : de;
    std::string err;
    const json ps = pasos(desde, a, &err);
    if (!err.empty()) return listo({{"error", err}});
    json cadena = json::array();
    for (const auto& p : ps) {
        const fs::path carpeta = modelos_.carpetaInstalada(q(p.value("repo", "")));
        if (carpeta.empty())
            return listo({{"error", "falta bajar " + p.value("repo", std::string()) + " (Traducción: «Bajar», o traduccion_bajar)"}});
        cadena.push_back({{"de", p["de"]}, {"a", p["a"]}, {"paquete", (carpeta / p.value("archivo", std::string())).string()}});
    }
    // Si se juntan muchas (alguien habla rapidísimo), se descartan las más viejas.
    while (cola_.size() > 8) {
        cola_.front().listo({{"error", "descartada: llegaron otras más nuevas"}});
        cola_.pop_front();
    }
    cola_.push_back({{{"op", "traducir"}, {"texto", texto.toStdString()}, {"de", desde}, {"a", a}, {"pasos", cadena}, {"glosario", glosarioActivo()}},
                     std::move(listo)});
    mandar();
}

namespace {
/// Termina una oración: punto, signo de pregunta o de exclamación, puntos suspensivos.
bool cierra(const QString& t) {
    const QString s = t.trimmed();
    if (s.isEmpty()) return false;
    const QChar c = s.back();
    return c == '.' || c == '?' || c == '!' || c == QChar(0x2026) || c == QChar(0x3002) || c == ':' || c == ';';
}
constexpr int kLargoMaximo = 280; ///< caracteres: más que esto no se espera
} // namespace

void Traductor::alFrase(const QString& flujo, const QString& texto) {
    const std::string f = flujo.toStdString();
    for (const auto& t : traducciones_) {
        if (t.value("desde", "") != f || !t.value("activa", true)) continue;
        const std::string id = t.value("id", "");
        Junta& j = juntas_[id];
        j.texto = j.texto.isEmpty() ? texto.trimmed() : j.texto + " " + texto.trimmed();
        if (!j.timer) {
            j.timer = new QTimer(this);
            j.timer->setSingleShot(true);
            connect(j.timer, &QTimer::timeout, this, [this, id]() { soltar(id); });
        }
        if (cierra(j.texto)) {
            j.timer->stop();
            soltar(id);
        } else if (j.texto.size() > kLargoMaximo) {
            // Demasiado largo sin punto: se traduce hasta la última coma y el resto espera.
            const int c = j.texto.lastIndexOf(',');
            const QString resto = c > 40 ? j.texto.mid(c + 1).trimmed() : QString();
            if (c > 40) j.texto = j.texto.left(c + 1);
            soltar(id);
            juntas_[id].texto = resto;
            if (!resto.isEmpty()) juntas_[id].timer->start(t.value("espera_ms", 1200));
        } else {
            j.timer->start(t.value("espera_ms", 1200));
        }
    }
}

void Traductor::soltar(const std::string& id) {
    auto jt = juntas_.find(id);
    if (jt == juntas_.end() || jt->second.texto.trimmed().isEmpty()) return;
    const QString texto = jt->second.texto;
    jt->second.texto.clear();
    for (const auto& t : traducciones_) {
        if (t.value("id", "") != id) continue;
        const std::string nombre = t.value("nombre", "");
        const QString flujo = QString::fromStdString(t.value("desde", std::string()));
        QPointer<Traductor> yo(this);
        traducir(texto, t.value("idioma_desde", std::string("es")), t.value("a", std::string()), [yo, id, nombre, flujo](json r) {
            if (!yo) return;
            if (r.contains("texto") && !r["texto"].get<std::string>().empty()) {
                yo->flujos_.publicar(q(nombre), q(r["texto"].get<std::string>()), "Traducción de «" + flujo + "»");
                yo->ultima_[id] = {{"texto", r["texto"]}, {"ms", r.value("ms", 0)}, {"hora", QDateTime::currentDateTime().toString("HH:mm:ss").toStdString()}};
            } else {
                yo->ultima_[id] = {{"error", r.value("error", std::string("sin texto"))}};
            }
            emit yo->cambio();
        });
    }
}

// --- Glosario ---------------------------------------------------------------------------------

fs::path Traductor::archivoGlosario() const { return root_ / "Config" / "glosario-traduccion.json"; }

void Traductor::leerGlosario() {
    const json j = leerJson(archivoGlosario());
    operador_ = j.is_object() ? j : json::object();
    for (const char* k : {"terminos", "apagados", "grupos_apagados"})
        if (!operador_.contains(k) || !operador_[k].is_array()) operador_[k] = json::array();
}

void Traductor::guardarGlosario() const {
    json g = operador_;
    g["_comentario"] =
        "El glosario del operador para la traducción en vivo. Se suma al de fábrica y lo pisa por la primera forma en castellano "
        "(`es`). Cada término: {\"es\": [\"forma\", \"otra forma\"], \"en\": \"...\", \"pt\": \"...\", \"fr\": \"...\", \"de\": \"...\", "
        "\"ru\": \"...\", \"zh\": \"...\", \"grupo\": \"...\"}. `apagados`: términos de fábrica que no se usan; `grupos_apagados`: "
        "grupos enteros. Se puede editar a mano: la app lo relee al cambiar algo desde la interfaz o MCP.";
    escribirJson(archivoGlosario(), g);
}

json Traductor::glosario() const {
    std::set<std::string> apagados, gruposApagados, delOperador;
    for (const auto& x : operador_["apagados"]) apagados.insert(QString::fromStdString(x.get<std::string>()).toLower().toStdString());
    for (const auto& x : operador_["grupos_apagados"]) gruposApagados.insert(x.get<std::string>());
    json out = json::array();
    for (json e : operador_["terminos"]) {
        delOperador.insert(clave(e));
        e["origen"] = "operador";
        e["activo"] = !gruposApagados.count(e.value("grupo", std::string("operador")));
        if (!e.contains("grupo")) e["grupo"] = "operador";
        out.push_back(e);
    }
    for (json e : fabrica_) {
        if (delOperador.count(clave(e))) continue; // lo pisa el del operador
        e["origen"] = "fabrica";
        e["activo"] = !apagados.count(clave(e)) && !gruposApagados.count(e.value("grupo", std::string()));
        out.push_back(e);
    }
    return out;
}

json Traductor::glosarioActivo() const {
    json out = json::array();
    for (const auto& e : glosario())
        if (e.value("activo", true)) {
            json x = e;
            x.erase("origen");
            x.erase("activo");
            x.erase("grupo");
            out.push_back(x);
        }
    return out;
}

std::string Traductor::glosarioAgregar(const json& entrada) {
    leerGlosario(); // por si lo editaron a mano
    json e = entrada;
    if (e.contains("es") && e["es"].is_string()) e["es"] = json::array({e["es"]});
    if (variantes(e).empty()) return "falta `es`: el término en castellano (o una lista de formas)";
    bool alguno = false;
    for (const char* k : kIdiomas)
        if (std::string(k) != "es" && e.contains(k) && e[k].is_string() && !e[k].get<std::string>().empty()) alguno = true;
    if (!alguno) return "falta al menos una traducción (en, pt, fr, de, ru, zh)";
    json limpio = json::object();
    for (const char* k : kIdiomas)
        if (e.contains(k)) limpio[k] = e[k];
    if (e.contains("grupo") && e["grupo"].is_string()) limpio["grupo"] = e["grupo"];
    auto& t = operador_["terminos"];
    const std::string c = clave(limpio);
    t.erase(std::remove_if(t.begin(), t.end(), [&](const json& x) { return clave(x) == c; }), t.end());
    t.push_back(limpio);
    // Si era uno de fábrica apagado, vuelve a valer (ahora con lo del operador).
    auto& ap = operador_["apagados"];
    ap.erase(std::remove_if(ap.begin(), ap.end(), [&](const json& x) { return QString::fromStdString(x.get<std::string>()).toLower().toStdString() == c; }),
             ap.end());
    guardarGlosario();
    emit glosarioCambio();
    return {};
}

std::string Traductor::glosarioQuitar(const std::string& es) {
    leerGlosario();
    const std::string c = QString::fromStdString(es).toLower().toStdString();
    auto& t = operador_["terminos"];
    const auto n = t.size();
    t.erase(std::remove_if(t.begin(), t.end(), [&](const json& x) {
                const auto v = variantes(x);
                return std::any_of(v.begin(), v.end(), [&](const std::string& s) { return QString::fromStdString(s).toLower().toStdString() == c; });
            }),
            t.end());
    bool deFabrica = false;
    for (const auto& e : fabrica_) {
        const auto v = variantes(e);
        if (std::any_of(v.begin(), v.end(), [&](const std::string& s) { return QString::fromStdString(s).toLower().toStdString() == c; })) {
            operador_["apagados"].push_back(clave(e));
            deFabrica = true;
        }
    }
    if (t.size() == n && !deFabrica) return "no hay un término «" + es + "» en el glosario";
    guardarGlosario();
    emit glosarioCambio();
    return {};
}

std::string Traductor::glosarioGrupo(const std::string& grupo, bool activo) {
    leerGlosario();
    auto& g = operador_["grupos_apagados"];
    g.erase(std::remove(g.begin(), g.end(), json(grupo)), g.end());
    if (!activo) g.push_back(grupo);
    guardarGlosario();
    emit glosarioCambio();
    return {};
}

// --- Interfaz ---------------------------------------------------------------------------------

QVariantList Traductor::traduccionesQml() const { return qv(lista()).toList(); }

QVariantList Traductor::idiomasQml() const {
    QVariantList out;
    for (const char* k : kIdiomas) out << QVariantMap{{"codigo", k}, {"nombre", q(nombresIdiomas().at(k))}};
    return out;
}

QVariantList Traductor::glosarioQml() const { return qv(glosario()).toList(); }

QVariantList Traductor::gruposQml() const {
    std::map<std::string, std::pair<int, bool>> g;
    for (const auto& e : glosario()) {
        auto& x = g[e.value("grupo", std::string("operador"))];
        ++x.first;
        x.second = x.second || e.value("activo", true);
    }
    std::set<std::string> apagados;
    for (const auto& x : operador_["grupos_apagados"]) apagados.insert(x.get<std::string>());
    QVariantList out;
    static const std::map<std::string, std::string> nombres{
        {"general", "Generales de cabina"}, {"legislativo-chubut", "Legislatura del Chubut"}, {"operador", "Agregados por el operador"}};
    for (const auto& [k, v] : g) {
        const auto n = nombres.find(k);
        out << QVariantMap{{"grupo", q(k)}, {"nombre", q(n != nombres.end() ? n->second : k)}, {"activo", !apagados.count(k)}, {"cuantos", v.first}};
    }
    return out;
}

QString Traductor::archivoGlosarioTexto() const { return q(archivoGlosario()); }

QStringList Traductor::salidasDisponibles() const {
    QStringList out;
    for (const QVariant& v : flujos_.flujos()) out << v.toMap().value("nombre").toString();
    out.removeDuplicates();
    return out;
}

QString Traductor::agregarUi(const QString& desde, const QString& a, const QString& nombre) {
    return q(agregar(desde.toStdString(), "es", a.toStdString(), nombre.toStdString(), json::object(), nullptr));
}
QString Traductor::ajustarUi(const QString& id, const QVariantMap& cambios) { return q(ajustar(id.toStdString(), dejson(cambios))); }
QString Traductor::quitarUi(const QString& id) { return q(quitar(id.toStdString())); }
QString Traductor::bajarUi(const QString& de, const QString& a) { return q(bajar(de.isEmpty() ? "es" : de.toStdString(), a.toStdString())); }

void Traductor::probarUi(const QString& texto, const QString& a) {
    QPointer<Traductor> yo(this);
    traducir(texto, "es", a.toStdString(), [yo, a](json r) {
        if (!yo) return;
        const bool ok = r.contains("texto");
        emit yo->probado(a, ok ? q(r["texto"].get<std::string>()) + "  (" + QString::number(r.value("ms", 0)) + " ms)"
                               : q(r.value("error", std::string("falló"))),
                         ok);
    });
}

QString Traductor::glosarioAgregarUi(const QVariantMap& entrada) { return q(glosarioAgregar(dejson(entrada))); }
QString Traductor::glosarioQuitarUi(const QString& es) { return q(glosarioQuitar(es.toStdString())); }
QString Traductor::glosarioGrupoUi(const QString& grupo, bool activo) { return q(glosarioGrupo(grupo.toStdString(), activo)); }

} // namespace ba::app::agent
