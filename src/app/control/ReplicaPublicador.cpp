// SPDX-License-Identifier: GPL-2.0-or-later
// src/app/control/ReplicaPublicador.cpp — ver el .hpp
#include "control/ReplicaPublicador.hpp"

#include <QAbstractItemModel>
#include <QDateTime>
#include <QJSValue>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QPointF>
#include <QRectF>
#include <QRegularExpression>
#include <QSizeF>
#include <QUrl>

#include <chrono>
#include <cmath>

namespace ba::app::control {

using json = nlohmann::json;

namespace {

std::int64_t ahoraMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

QString textoDe(const json& j, const char* k) {
    return j.is_object() && j.contains(k) && j[k].is_string() ? QString::fromStdString(j[k].get<std::string>()) : QString();
}

/// El tipo de una propiedad para la réplica (su valor inicial del otro lado).
const char* tipoCorto(const QMetaType& t) {
    if (t.flags() & QMetaType::PointerToQObject) return "o";
    switch (t.id()) {
    case QMetaType::QVariantList:
    case QMetaType::QStringList:
        return "l";
    case QMetaType::QVariantMap:
    case QMetaType::QVariantHash:
        return "m";
    case QMetaType::QString:
    case QMetaType::QUrl:
    case QMetaType::QColor:
        return "s";
    case QMetaType::Bool:
        return "b";
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Double:
    case QMetaType::Float:
    case QMetaType::Long:
    case QMetaType::Short:
        return "n";
    default:
        break;
    }
    if (t.flags() & QMetaType::IsEnumeration) return "n";
    return "v";
}

/// Los métodos de la clase base (QObject, o el modelo de Qt) no se publican: la réplica
/// tiene los suyos (rowCount, deleteLater…).
int primerMetodoPropio(const QObject* o) {
    if (qobject_cast<const QAbstractItemModel*>(o) != nullptr) return QAbstractItemModel::staticMetaObject.methodCount();
    return QObject::staticMetaObject.methodCount();
}

} // namespace

/// Un aviso (NOTIFY) de un objeto publicado: marca sus propiedades como cambiadas.
class AvisoReplica : public QObject {
    Q_OBJECT
   public:
    AvisoReplica(ReplicaPublicador* p, QString id, std::vector<int> props, QObject* parent)
        : QObject(parent), p_(p), id_(std::move(id)), props_(std::move(props)) {}
   public slots:
    void disparo() { p_->cambio(id_, props_); }

   private:
    ReplicaPublicador* p_;
    QString id_;
    std::vector<int> props_;
};

ReplicaPublicador::ReplicaPublicador(QObject* raiz, const QString& nombreRaiz, QObject* parent)
    : QObject(parent), raiz_(raiz), nombreRaiz_(nombreRaiz) {
    reloj_.setSingleShot(true);
    reloj_.setInterval(kLoteMs);
    connect(&reloj_, &QTimer::timeout, this, &ReplicaPublicador::vaciar);
    funcionesNegadas_ = {QStringLiteral("deleteLater"), QStringLiteral("destroyed"), QStringLiteral("objectNameChanged")};
}

ReplicaPublicador::~ReplicaPublicador() = default;

void ReplicaPublicador::negar(const QStringList& props) { aNegar_ += props; }

void ReplicaPublicador::negarFunciones(const QStringList& nombres) {
    for (const QString& n : nombres) funcionesNegadas_.insert(n);
}

bool ReplicaPublicador::perfilPermite(const json& perfil) {
    if (perfil.is_string()) return perfil.get<std::string>() == "*";
    if (!perfil.is_array()) return false;
    for (const auto& x : perfil)
        if (x.is_string() && x.get<std::string>() == "*") return true;
    return false;
}

bool ReplicaPublicador::nombreSecreto(const QString& nombre) {
    static const QRegularExpression re(
        QStringLiteral("(token|secret|password|passwd|contrase|streamkey|apikey|api_key|hash|credencial)"),
        QRegularExpression::CaseInsensitiveOption);
    // El PIN, sin pescar «pintar» ni «spinner»: «pin», «pinTunel», «hashPin», «PIN».
    static const QRegularExpression rePin(QStringLiteral("(^pin$|^pin[A-Z_]|Pin$|Pin[A-Z_]|PIN)"));
    return re.match(nombre).hasMatch() || rePin.match(nombre).hasMatch();
}

json ReplicaPublicador::simple(const QVariant& v) {
    if (!v.isValid() || v.isNull()) {
        // Un texto vacío es «» y no null (la vista hace .length, .startsWith…).
        if (v.metaType().id() == QMetaType::QString) return "";
        if (!v.isValid()) return nullptr;
    }
    switch (v.metaType().id()) {
    case QMetaType::Bool:
        return v.toBool();
    case QMetaType::Int:
    case QMetaType::Short:
    case QMetaType::Long:
    case QMetaType::LongLong:
        return v.toLongLong();
    case QMetaType::UInt:
    case QMetaType::ULongLong:
        return v.toULongLong();
    case QMetaType::Double:
    case QMetaType::Float: {
        const double d = v.toDouble();
        return std::isfinite(d) ? json(d) : json(nullptr);
    }
    case QMetaType::QString:
        return v.toString().toStdString();
    case QMetaType::QByteArray:
        return v.toByteArray().toStdString();
    case QMetaType::QUrl:
        return v.toUrl().toString().toStdString();
    case QMetaType::QStringList: {
        json a = json::array();
        for (const QString& s : v.toStringList()) a.push_back(s.toStdString());
        return a;
    }
    case QMetaType::QDateTime:
        return {{"$t", "fecha"}, {"ms", v.toDateTime().toMSecsSinceEpoch()}};
    case QMetaType::QSize:
    case QMetaType::QSizeF: {
        const QSizeF s = v.toSizeF();
        return {{"$t", "tam"}, {"w", s.width()}, {"h", s.height()}};
    }
    case QMetaType::QPoint:
    case QMetaType::QPointF: {
        const QPointF p = v.toPointF();
        return {{"$t", "punto"}, {"x", p.x()}, {"y", p.y()}};
    }
    case QMetaType::QRect:
    case QMetaType::QRectF: {
        const QRectF r = v.toRectF();
        return {{"$t", "rect"}, {"x", r.x()}, {"y", r.y()}, {"w", r.width()}, {"h", r.height()}};
    }
    default:
        break;
    }
    if (v.metaType().flags() & QMetaType::IsEnumeration) return v.toLongLong();
    if (v.canConvert<QString>()) return v.toString().toStdString(); // QColor → «#rrggbb»
    return nullptr;
}

json ReplicaPublicador::valor(const QVariant& vOriginal, Con& c, json& esquemas, const QString& sugerido) {
    QVariant v = vOriginal;
    if (v.metaType() == QMetaType::fromType<QJSValue>()) v = v.value<QJSValue>().toVariant();
    if (v.metaType() == QMetaType::fromType<QVariant>()) v = v.value<QVariant>();
    if (v.metaType().flags() & QMetaType::PointerToQObject) {
        QObject* o = v.value<QObject*>();
        if (o == nullptr || negados_.contains(o)) return nullptr;
        const QString id = refDe(o, sugerido);
        if (id.isEmpty()) return nullptr;
        if (!c.esquemas.count(id)) {
            c.esquemas.insert(id);
            esquemas.push_back(objs_[id].esquema);
        }
        return {{"$o", id.toStdString()}};
    }
    switch (v.metaType().id()) {
    case QMetaType::QVariantList: {
        json a = json::array();
        for (const QVariant& x : v.toList()) a.push_back(valor(x, c, esquemas));
        return a;
    }
    case QMetaType::QVariantMap: {
        json m = json::object();
        const QVariantMap mapa = v.toMap();
        for (auto it = mapa.cbegin(); it != mapa.cend(); ++it)
            if (!nombreSecreto(it.key())) m[it.key().toStdString()] = valor(it.value(), c, esquemas);
        return m;
    }
    case QMetaType::QVariantHash: {
        json m = json::object();
        const QVariantHash mapa = v.toHash();
        for (auto it = mapa.cbegin(); it != mapa.cend(); ++it)
            if (!nombreSecreto(it.key())) m[it.key().toStdString()] = valor(it.value(), c, esquemas);
        return m;
    }
    default:
        return simple(v);
    }
}

QString ReplicaPublicador::refDe(QObject* o, const QString& sugerido) {
    if (o == nullptr) return {};
    if (const auto it = idDe_.find(o); it != idDe_.end()) return it->second;
    const QString id = sugerido.isEmpty() ? QStringLiteral("#%1").arg(siguiente_++) : sugerido;
    Obj& ob = objs_[id];
    ob.o = o;
    ob.modelo = qobject_cast<QAbstractItemModel*>(o) != nullptr;
    idDe_[o] = id;
    ob.esquema = armarEsquema(id, ob);
    connect(o, &QObject::destroyed, this, [this, o, id]() {
        idDe_.erase(o);
        objs_.erase(id);
        for (auto& [cid, c] : cons_) {
            c.esquemas.erase(id);
            c.abiertos.erase(id);
            c.sucios.erase(id);
            c.filasSucias.erase(id);
        }
    });
    return id;
}

json ReplicaPublicador::armarEsquema(const QString& id, Obj& ob) {
    const QMetaObject* mo = ob.o->metaObject();
    json props = json::array();
    std::set<int> avisos; // señales de aviso de propiedades: no se publican como funciones
    for (int i = QObject::staticMetaObject.propertyCount(); i < mo->propertyCount(); ++i) {
        const QMetaProperty p = mo->property(i);
        if (!p.isReadable() || nombreSecreto(QString::fromLatin1(p.name()))) continue;
        ob.props.push_back(i);
        if (p.hasNotifySignal()) avisos.insert(p.notifySignalIndex());
        props.push_back(json::array({p.name(), tipoCorto(p.metaType()), p.isWritable() && !p.isConstant()}));
    }
    json metodos = json::array();
    std::map<std::string, bool> vistos;
    for (int i = primerMetodoPropio(ob.o); i < mo->methodCount(); ++i) {
        const QMetaMethod m = mo->method(i);
        if (m.access() != QMetaMethod::Public || avisos.count(i)) continue;
        if (m.methodType() == QMetaMethod::Constructor) continue;
        const QString nombre = QString::fromLatin1(m.name());
        if (funcionesNegadas_.contains(nombre) || nombre.startsWith(QLatin1Char('_'))) continue;
        // Una señal que nadie avisa como propiedad se puede emitir (Studio.notify(…)).
        const bool conValor = m.returnMetaType().id() != QMetaType::Void && m.returnMetaType().isValid();
        vistos[nombre.toStdString()] = vistos[nombre.toStdString()] || conValor;
        ob.metodos.insert(nombre);
    }
    for (const auto& [n, v] : vistos) metodos.push_back(json::array({n, v}));
    json roles = nullptr;
    if (ob.modelo) {
        roles = json::array();
        const auto* modelo = qobject_cast<QAbstractItemModel*>(ob.o);
        const QHash<int, QByteArray> nombres = modelo->roleNames();
        std::vector<int> ids(nombres.keyBegin(), nombres.keyEnd());
        std::sort(ids.begin(), ids.end());
        for (int r : ids) {
            ob.roles.push_back(r);
            roles.push_back(nombres.value(r).toStdString());
        }
    }
    return {{"op", "esquema"}, {"o", id.toStdString()}, {"props", props}, {"metodos", metodos}, {"roles", roles}};
}

void ReplicaPublicador::escuchar(const QString& id, Obj& ob) {
    if (ob.escuchando || !ob.o) return;
    ob.escuchando = true;
    const QMetaObject* mo = ob.o->metaObject();
    std::map<int, std::vector<int>> porSenal;
    for (int i : ob.props) {
        const QMetaProperty p = mo->property(i);
        if (p.hasNotifySignal()) porSenal[p.notifySignalIndex()].push_back(i);
    }
    for (auto& [senal, props] : porSenal) {
        auto* aviso = new AvisoReplica(this, id, props, ob.o);
        QMetaObject::connect(ob.o, senal, aviso, aviso->metaObject()->indexOfSlot("disparo()"));
    }
    if (auto* modelo = qobject_cast<QAbstractItemModel*>(ob.o)) {
        const auto f = [this, id]() { cambioFilas(id); };
        connect(modelo, &QAbstractItemModel::rowsInserted, this, f);
        connect(modelo, &QAbstractItemModel::rowsRemoved, this, f);
        connect(modelo, &QAbstractItemModel::rowsMoved, this, f);
        connect(modelo, &QAbstractItemModel::dataChanged, this, f);
        connect(modelo, &QAbstractItemModel::modelReset, this, f);
        connect(modelo, &QAbstractItemModel::layoutChanged, this, f);
    }
}

void ReplicaPublicador::cambio(const QString& id, const std::vector<int>& props) {
    bool alguno = false;
    for (auto& [cid, c] : cons_) {
        if (!c.abiertos.count(id)) continue;
        c.sucios[id].insert(props.begin(), props.end());
        alguno = true;
    }
    if (alguno) programar();
}

void ReplicaPublicador::cambioFilas(const QString& id) {
    bool alguno = false;
    for (auto& [cid, c] : cons_) {
        if (!c.abiertos.count(id)) continue;
        c.filasSucias.insert(id);
        alguno = true;
    }
    if (alguno) programar();
}

void ReplicaPublicador::programar() {
    if (!reloj_.isActive()) reloj_.start();
}

void ReplicaPublicador::encolar(Con& c, json msg, json& esquemas) {
    for (auto& e : esquemas) c.cola.push_back(std::move(e));
    esquemas = json::array();
    c.cola.push_back(std::move(msg));
    programar();
}

json ReplicaPublicador::filas(Obj& ob, Con& c, json& esquemas) {
    json fs = json::array();
    auto* modelo = qobject_cast<QAbstractItemModel*>(ob.o);
    if (modelo == nullptr) return fs;
    const int n = modelo->rowCount();
    for (int r = 0; r < n; ++r) {
        const QModelIndex i = modelo->index(r, 0);
        json fila = json::array();
        for (int rol : ob.roles) fila.push_back(valor(modelo->data(i, rol), c, esquemas));
        fs.push_back(std::move(fila));
    }
    return fs;
}

void ReplicaPublicador::abrir(Con& c, const QString& id) {
    const auto it = objs_.find(id);
    if (it == objs_.end() || !it->second.o || !c.esquemas.count(id) || c.abiertos.count(id)) return;
    Obj& ob = it->second;
    escuchar(id, ob);
    c.abiertos.insert(id);
    json esquemas = json::array();
    json v = json::object();
    const QMetaObject* mo = ob.o->metaObject();
    for (int i : ob.props) {
        const QMetaProperty p = mo->property(i);
        v[p.name()] = valor(p.read(ob.o), c, esquemas, id + QLatin1Char('.') + QLatin1String(p.name()));
    }
    encolar(c, {{"op", "valores"}, {"o", id.toStdString()}, {"v", std::move(v)}}, esquemas);
    if (ob.modelo) encolar(c, {{"op", "filas"}, {"o", id.toStdString()}, {"filas", filas(ob, c, esquemas)}}, esquemas);
}

bool ReplicaPublicador::tope(Con& c) {
    const std::int64_t ahora = ahoraMs();
    while (!c.llamadas.empty() && ahora - c.llamadas.front() > 1000) c.llamadas.pop_front();
    if (static_cast<int>(c.llamadas.size()) >= kLlamadasPorSegundo) return false;
    c.llamadas.push_back(ahora);
    return true;
}

QVariant ReplicaPublicador::aVariant(const json& j) const {
    if (j.is_null()) return {};
    if (j.is_boolean()) return j.get<bool>();
    if (j.is_number_integer()) return QVariant::fromValue<qlonglong>(j.get<long long>());
    if (j.is_number()) return j.get<double>();
    if (j.is_string()) return QString::fromStdString(j.get<std::string>());
    if (j.is_array()) {
        QVariantList l;
        for (const auto& x : j) l.push_back(aVariant(x));
        return l;
    }
    if (j.is_object()) {
        if (j.contains("$o") && j["$o"].is_string()) {
            const auto it = objs_.find(QString::fromStdString(j["$o"].get<std::string>()));
            return it != objs_.end() ? QVariant::fromValue<QObject*>(it->second.o.data()) : QVariant();
        }
        QVariantMap m;
        for (auto it = j.begin(); it != j.end(); ++it) m.insert(QString::fromStdString(it.key()), aVariant(it.value()));
        return m;
    }
    return {};
}

void ReplicaPublicador::llamar(const std::string& cid, Con& c, const json& m) {
    const json id = m.contains("id") ? m["id"] : json(nullptr);
    json esquemas = json::array();
    const auto mal = [&](const std::string& e) {
        encolar(c, {{"op", "resultado"}, {"id", id}, {"ok", false}, {"error", e}}, esquemas);
    };
    const QString oid = textoDe(m, "o"), nombre = textoDe(m, "m");
    const auto it = objs_.find(oid);
    if (it == objs_.end() || !it->second.o || !c.esquemas.count(oid)) return mal("no existe ese objeto");
    Obj& ob = it->second;
    if (!ob.metodos.count(nombre)) return mal("no se puede llamar esa función desde la web");
    if (!tope(c)) return mal("demasiadas llamadas");
    const json args = m.contains("a") && m["a"].is_array() ? m["a"] : json::array();
    const QMetaObject* mo = ob.o->metaObject();
    // La sobrecarga con esa cantidad de argumentos (los argumentos por omisión son otra).
    int elegido = -1;
    for (int i = primerMetodoPropio(ob.o); i < mo->methodCount(); ++i) {
        const QMetaMethod mm = mo->method(i);
        if (mm.access() == QMetaMethod::Public && QString::fromLatin1(mm.name()) == nombre &&
            mm.parameterCount() == static_cast<int>(args.size())) {
            elegido = i;
            break;
        }
    }
    if (elegido < 0) return mal("no hay una versión de esa función con esos argumentos");
    const QMetaMethod mm = mo->method(elegido);
    if (mm.parameterCount() > 10) return mal("demasiados argumentos");
    const bool conValor = mm.returnMetaType().isValid() && mm.returnMetaType().id() != QMetaType::Void;
    // Escribir y hacer, sí; mirar (lo que devuelve algo), también si es solo «ver».
    if (!c.escribe && !conValor) return mal("este aparato solo puede mirar");
    std::vector<QVariant> vals(args.size());
    void* argv[11] = {nullptr};
    QVariant ret;
    if (conValor) {
        ret = QVariant(mm.returnMetaType());
        argv[0] = ret.data();
    }
    for (std::size_t k = 0; k < args.size(); ++k) {
        const QMetaType pt = mm.parameterMetaType(static_cast<int>(k));
        QVariant a = aVariant(args[k]);
        if (pt.id() == QMetaType::QVariant) {
            vals[k] = QVariant::fromValue(a);
        } else if (pt == QMetaType::fromType<QJSValue>()) {
            return mal("esa función no se puede llamar desde la web");
        } else if (!a.isValid()) {
            vals[k] = QVariant(pt);
        } else if (a.metaType() != pt && !a.convert(pt)) {
            return mal("argumento " + std::to_string(k + 1) + " de otro tipo");
        } else {
            vals[k] = a;
        }
        argv[k + 1] = vals[k].data();
    }
    QPointer<QObject> o = ob.o;
    if (!conValor && g_.anotar)
        g_.anotar(c.aparato, "tablero web: " + oid.toStdString() + "." + nombre.toStdString() + "(" + args.dump().substr(0, 200) + ")");
    QMetaObject::metacall(o.data(), QMetaObject::InvokeMetaMethod, elegido, argv);
    // Lo que devuelve (puede traer objetos: van con su esquema).
    if (cons_.find(cid) == cons_.end()) return;
    QVariant r = conValor ? ret : QVariant();
    if (r.metaType() == QMetaType::fromType<QVariant>()) r = *static_cast<const QVariant*>(r.constData());
    json v = valor(r, c, esquemas);
    encolar(c, {{"op", "resultado"}, {"id", id}, {"ok", true}, {"v", std::move(v)}}, esquemas);
}

void ReplicaPublicador::poner(Con& c, const json& m) {
    if (!c.escribe || !tope(c)) return;
    const QString oid = textoDe(m, "o"), p = textoDe(m, "p");
    const auto it = objs_.find(oid);
    if (it == objs_.end() || !it->second.o || !c.esquemas.count(oid) || nombreSecreto(p)) return;
    Obj& ob = it->second;
    const QMetaObject* mo = ob.o->metaObject();
    const int i = mo->indexOfProperty(p.toUtf8().constData());
    if (i < 0 || std::find(ob.props.begin(), ob.props.end(), i) == ob.props.end()) return;
    const QMetaProperty mp = mo->property(i);
    if (!mp.isWritable() || mp.isConstant()) return;
    QVariant v = aVariant(m.contains("v") ? m["v"] : json(nullptr));
    if (mp.metaType().id() != QMetaType::QVariant && v.isValid() && v.metaType() != mp.metaType() && !v.convert(mp.metaType()))
        return;
    if (g_.anotar) g_.anotar(c.aparato, "tablero web: " + oid.toStdString() + "." + p.toStdString() + " = " + m.value("v", json()).dump().substr(0, 200));
    mp.write(ob.o, v);
    // Si el modelo no lo aceptó (o lo corrigió), que la réplica vuelva a lo de verdad.
    cambio(oid, {i});
}

void ReplicaPublicador::procesar(const std::string& cid, Con& c, const json& m, int profundidad) {
    const std::string op = m.value("op", std::string());
    if (op == "lote" && profundidad == 0 && m.contains("m") && m["m"].is_array()) {
        int n = 0;
        for (const auto& x : m["m"]) {
            if (++n > 400) break;
            if (cons_.find(cid) == cons_.end()) return;
            if (x.is_object()) procesar(cid, c, x, 1);
        }
    } else if (op == "abrir") {
        abrir(c, textoDe(m, "o"));
    } else if (op == "llamar") {
        llamar(cid, c, m);
    } else if (op == "poner") {
        poner(c, m);
    } else if (op == "video") {
        json claves = json::array();
        if (m.contains("claves") && m["claves"].is_array())
            for (const auto& k : m["claves"]) {
                if (!k.is_object() || !k.contains("clave") || !k["clave"].is_string() || claves.size() >= 24) continue;
                const std::string clave = k["clave"].get<std::string>();
                if (clave.empty() || clave.size() > 80) continue;
                claves.push_back({{"clave", clave},
                                  {"ancho", std::clamp(k.value("ancho", 640), 64, 1920)},
                                  {"fps", std::clamp(k.value("fps", 15), 1, 30)}});
            }
        if (g_.video) g_.video(cid, claves);
    } else if (op == "webrtc" && c.escribe && g_.enlaceCast) {
        QPointer<ReplicaPublicador> yo(this);
        g_.enlaceCast([yo, cid](const std::string& url, const std::string& error) {
            if (!yo) return;
            const auto it = yo->cons_.find(cid);
            if (it == yo->cons_.end()) return;
            json e = json::array();
            yo->encolar(it->second, {{"op", "webrtc"}, {"url", url}, {"error", error}}, e);
        });
    }
}

void ReplicaPublicador::entrada(const std::string& cid, const std::string& aparato, const json& perfil, const json& msg) {
    if (!raiz_ || !msg.is_object()) return;
    const std::string op = msg.value("op", std::string());
    if (op == "hola") {
        // Empezar de nuevo (una página recargada usa la misma conexión del gateway solo si
        // no se cortó: igual se rehace todo).
        if (!perfilPermite(perfil)) {
            if (g_.enviar)
                g_.enviar(cid, {{"type", "replica"},
                                {"op", "lote"},
                                {"m", json::array({{{"op", "error"},
                                                    {"error", "El tablero completo necesita el perfil «todo»: cambialo en la app, "
                                                              "en Servicios → Control web."}}})}});
            return;
        }
        // Lo negado se resuelve recién acá: al armar el publicador la raíz todavía no estaba entera.
        for (const QString& p : std::as_const(aNegar_))
            if (QObject* o = raiz_->property(p.toUtf8().constData()).value<QObject*>()) negados_.insert(o);
        aNegar_.clear();
        if (!avisosConectados_) {
            avisosConectados_ = true;
            const int i = raiz_->metaObject()->indexOfSignal("notify(QString,QString)");
            if (i >= 0)
                QMetaObject::connect(raiz_, i, this, metaObject()->indexOfSlot("alAvisar(QString,QString)"));
        }
        Con& c = cons_[cid] = Con{};
        c.aparato = aparato;
        c.escribe = true;
        const QString id = refDe(raiz_, nombreRaiz_);
        c.esquemas.insert(id);
        json esquemas = json::array({objs_[id].esquema});
        encolar(c, {{"op", "raiz"}, {"o", id.toStdString()}, {"perfil", perfil}, {"escribe", c.escribe}}, esquemas);
        abrir(c, id);
        vaciar();
        return;
    }
    const auto it = cons_.find(cid);
    if (it == cons_.end()) return; // sin «hola», nada
    procesar(cid, it->second, msg, 0);
}

void ReplicaPublicador::alAvisar(const QString& texto, const QString& nivel) {
    for (auto& [cid, c] : cons_) {
        json e = json::array();
        encolar(c, {{"op", "aviso"}, {"texto", texto.toStdString()}, {"nivel", nivel.toStdString()}}, e);
    }
}

void ReplicaPublicador::cerrada(const std::string& cid) {
    cons_.erase(cid);
    if (g_.video) g_.video(cid, json::array());
}

void ReplicaPublicador::vaciar() {
    reloj_.stop();
    for (auto& [cid, c] : cons_) {
        json esquemas = json::array();
        // Lo que cambió desde la última vez (cada objeto, con todas sus propiedades sucias juntas).
        for (auto& [id, props] : c.sucios) {
            const auto it = objs_.find(id);
            if (it == objs_.end() || !it->second.o) continue;
            json v = json::object();
            for (int i : props) {
                const QMetaProperty p = it->second.o->metaObject()->property(i);
                v[p.name()] = valor(p.read(it->second.o), c, esquemas, id + QLatin1Char('.') + QLatin1String(p.name()));
            }
            for (auto& e : esquemas) c.cola.push_back(std::move(e));
            esquemas = json::array();
            c.cola.push_back({{"op", "valores"}, {"o", id.toStdString()}, {"v", std::move(v)}});
        }
        c.sucios.clear();
        for (const QString& id : c.filasSucias) {
            const auto it = objs_.find(id);
            if (it == objs_.end() || !it->second.o) continue;
            json fs = filas(it->second, c, esquemas);
            for (auto& e : esquemas) c.cola.push_back(std::move(e));
            esquemas = json::array();
            c.cola.push_back({{"op", "filas"}, {"o", id.toStdString()}, {"filas", std::move(fs)}});
        }
        c.filasSucias.clear();
        if (c.cola.empty()) continue;
        json lote{{"type", "replica"}, {"op", "lote"}, {"m", std::move(c.cola)}};
        c.cola = json::array();
        if (g_.enviar) g_.enviar(cid, lote);
    }
}

} // namespace ba::app::control

#include "ReplicaPublicador.moc"
