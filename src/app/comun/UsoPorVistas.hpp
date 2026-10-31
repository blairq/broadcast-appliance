// ============================================================================
// src/app/comun/UsoPorVistas.hpp — quién está usando un modelo (el contrato único)
//
// Un modelo que trabaja solo mientras alguien lo mira (consulta al gateway, mide a
// 30 Hz, pide una vista al motor) lleva la cuenta POR VISTA: `usar(vista, si[, que])`.
// Nunca un booleano compartido: con dos vistas abiertas (la página y su ventana, dos
// paneles del mismo modelo), la que se cierra le apagaba el trabajo a la otra. Una
// vista que se destruye sin avisar se suelta sola.
//
// `que` separa usos distintos del mismo modelo (la clave de una vista de video, el
// guion o la multipista de la línea): cada uno con su cuenta. En QML lo usa el control
// `Suscripcion { objetivo; activa: visible }`.
// ============================================================================
#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

#include <functional>
#include <utility>

namespace ba::app {

class UsoPorVistas {
   public:
    /// `alCambiar(que)`: cuando `que` pasa de nadie a alguien o al revés. Corre en el
    /// hilo de `dueno` (también al destruirse una vista).
    UsoPorVistas(QObject* dueno, std::function<void(const QString& que)> alCambiar)
        : dueno_(dueno), alCambiar_(std::move(alCambiar)) {}
    ~UsoPorVistas() {
        for (const QMetaObject::Connection& c : conexiones_) QObject::disconnect(c);
    }
    UsoPorVistas(const UsoPorVistas&) = delete;
    UsoPorVistas& operator=(const UsoPorVistas&) = delete;

    /// La vista empieza (si) o deja de usar `que`. Repetir lo mismo no cuenta dos veces.
    void usar(QObject* vista, bool si, const QString& que = {}) {
        if (vista == nullptr) return;
        QSet<QString>& usos = usos_[vista];
        const bool antes = activo(que);
        if (si) {
            usos.insert(que);
            if (!conexiones_.contains(vista))
                conexiones_.insert(vista, QObject::connect(vista, &QObject::destroyed, dueno_,
                                                           [this](QObject* o) { soltarTodo(o); }));
        } else {
            usos.remove(que);
            if (usos.isEmpty()) olvidar(vista);
        }
        if (antes != activo(que) && alCambiar_) alCambiar_(que);
    }
    /// Si alguna vista usa `que`.
    bool activo(const QString& que = {}) const { return cuantas(que) > 0; }
    /// Cuántas vistas usan `que`.
    int cuantas(const QString& que = {}) const {
        int n = 0;
        for (auto it = usos_.cbegin(); it != usos_.cend(); ++it) n += it.value().contains(que) ? 1 : 0;
        return n;
    }
    /// Si alguna vista usa algo (lo que sea).
    bool alguien() const { return !usos_.isEmpty(); }

   private:
    void soltarTodo(QObject* vista) {
        const QSet<QString> usos = usos_.value(vista);
        QList<QString> apagados;
        for (const QString& q : usos)
            if (cuantas(q) == 1) apagados << q;
        olvidar(vista);
        if (alCambiar_)
            for (const QString& q : apagados) alCambiar_(q);
    }
    void olvidar(QObject* vista) {
        usos_.remove(vista);
        if (auto it = conexiones_.find(vista); it != conexiones_.end()) {
            QObject::disconnect(it.value());
            conexiones_.erase(it);
        }
    }

    QObject* dueno_;
    std::function<void(const QString&)> alCambiar_;
    QHash<QObject*, QSet<QString>> usos_;
    QHash<QObject*, QMetaObject::Connection> conexiones_;
};

} // namespace ba::app
