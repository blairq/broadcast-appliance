// ============================================================================
// src/app/agent/Lanzamiento.hpp — lo que hace falta para lanzar un agente de
// un solo tiro. Lo arma el Despachador y lo resuelve AgentViewModel (que es
// quien conoce a cada motor). Esta aparte para que el despachador se pueda
// probar sin la app entera: en los tests, quien lo resuelve es un script.
// ============================================================================
#pragma once

#include <functional>

#include <QString>
#include <QVariantList>

#include "term/Pty.hpp"

namespace ba::app::agent {

struct Lanzamiento {
    QString rol;       ///< id del rol (espacio de trabajo Runtime/agentes/<rol>)
    QString proveedor; ///< id de proveedor; vacio = el del asistente
    QString modelo;    ///< vacio = el de fabrica
    QString prompt;    ///< instrucciones del rol (CLAUDE.md / AGENTS.md)
    QString encargo;   ///< lo que tiene que hacer esta vez
};

/// Los motores, vistos desde el despachador.
struct Motores {
    /// Programa, argumentos y entorno para correr ese lanzamiento; `program`
    /// vacio y el motivo en `error` si no se puede.
    std::function<term::Pty::Options(const Lanzamiento&, QString* error)> lanzar;
    /// Los proveedores que se pueden elegir (para la interfaz).
    std::function<QVariantList()> proveedores;
    /// Los modelos de un proveedor.
    std::function<QStringList(const QString& proveedor)> modelosDe;
};

} // namespace ba::app::agent
