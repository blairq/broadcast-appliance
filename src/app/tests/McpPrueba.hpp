// ============================================================================
// src/app/tests/McpPrueba.hpp — lo común de los tests del servidor MCP sin la app
//
// test_mcp (el núcleo, HTTP, el puente y los roles), test_mcp_salidas (las salidas y el límite
// de frecuencia) y test_mcp_herramientas (cada herramienta del registro por el despachador):
// eran un solo test_mcp.cpp, partido para que tocar uno no recompile los otros y corran a la
// vez. El CHECK cuenta en g_failures (uno por programa).
// ============================================================================
#pragma once

#include "mcp/McpAire.hpp"
#include "mcp/McpCore.hpp"
#include "mcp/McpHttp.hpp"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>

#include <cstdio>
#include <string>

namespace ba::app::mcp::prueba {

using namespace ba::app::mcp;

inline int g_failures = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_failures;                                                                                    \
        }                                                                                                    \
    } while (0)

/// Llamada sincrona al nucleo (las herramientas de prueba contestan dentro del
/// event loop; se lo gira hasta tener respuesta).
inline json call(McpCore& core, const json& msg, const std::string& session = {}) {
    json out = "sin respuesta";
    bool got = false;
    core.handle(
        msg,
        [&](json r) {
            out = std::move(r);
            got = true;
        },
        CallContext{session});
    for (int i = 0; i < 200 && !got; ++i) {
        QEventLoop loop;
        QTimer::singleShot(5, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return out;
}

inline json req(int id, const std::string& method, json params = json::object()) {
    return json{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};
}

inline std::string textOf(const json& result) {
    std::string t;
    for (const auto& c : result.value("content", json::array()))
        if (c.value("type", "") == "text") t += c.value("text", "");
    return t;
}

} // namespace ba::app::mcp::prueba
