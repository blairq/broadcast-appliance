// ============================================================================
// src/app/Registrar.cpp — ver Registrar.hpp
// ============================================================================
#include "Registrar.hpp"

#include <cstdarg>
#include <cstdio>
#include <string>

#include "diag/Caidas.hpp"
#include "diag/Registro.hpp"

#if BA_APP_HAVE_QT_OBS
#include <QLoggingCategory>
#include <QString>
#include <QtGlobal>
#define BA_REG_QT 1
#endif

#if BA_HAVE_LIBOBS
#include <obs.h>
#include <util/base.h>
#define BA_REG_OBS 1
#endif

namespace ba::app {
namespace {

using ba::diag::Nivel;

#if BA_REG_QT
/// El mensaje de Qt trae el archivo y la línea cuando es de QML, y ahí es
/// justamente donde hace falta: un aviso de QML sin el archivo no se puede
/// arreglar. Se pega al final para que la descripción quede primero.
std::string conOrigen(const QMessageLogContext& c, const QString& msg) {
    std::string s = msg.toStdString();
    if (c.file != nullptr && c.line > 0) {
        // Solo el nombre del archivo: la ruta entera del bundle no aporta y
        // hace la línea tres veces más larga.
        std::string archivo(c.file);
        const auto barra = archivo.find_last_of("/\\");
        if (barra != std::string::npos) archivo = archivo.substr(barra + 1);
        s += "  (" + archivo + ":" + std::to_string(c.line) + ")";
    }
    return s;
}

void handlerDeQt(QtMsgType tipo, const QMessageLogContext& c, const QString& msg) {
    Nivel n = Nivel::Info;
    switch (tipo) {
        case QtDebugMsg: n = Nivel::Depuracion; break;
        case QtInfoMsg: n = Nivel::Info; break;
        case QtWarningMsg: n = Nivel::Aviso; break;
        case QtCriticalMsg: n = Nivel::Error; break;
        case QtFatalMsg: n = Nivel::Fatal; break;
    }
    // Lo de QML se separa de lo del resto de Qt, que es la division que de
    // verdad se usa al buscar un problema. No sirve mirar la categoria: los
    // avisos del motor de QML vienen en la categoria "default". Lo que sí los
    // distingue es que el archivo es un .qml.
    const std::string cat = c.category != nullptr ? c.category : "";
    const std::string archivo = c.file != nullptr ? c.file : "";
    const bool esQml = archivo.size() > 4 && archivo.compare(archivo.size() - 4, 4, ".qml") == 0;
    ba::diag::registro().linea(n, esQml ? "qml" : "qt", 0, conOrigen(c, msg));
}
#endif

#if BA_REG_OBS
/// libobs habla en info todo el tiempo (cada módulo que carga, cada formato
/// que prueba). Se registra igual, pero como depuración: así el nivel normal
/// no queda tapado por el arranque del motor y, cuando hace falta, está.
void handlerDeObs(int nivelObs, const char* formato, va_list args, void*) {
    char buf[4096];
    std::vsnprintf(buf, sizeof(buf), formato, args);
    Nivel n = Nivel::Depuracion;
    switch (nivelObs) {
        case LOG_ERROR: n = Nivel::Error; break;
        case LOG_WARNING: n = Nivel::Aviso; break;
        case LOG_INFO: n = Nivel::Depuracion; break;
        default: n = Nivel::Traza; break; // LOG_DEBUG
    }
    ba::diag::registro().linea(n, "obs", 0, buf);
}
#endif

} // namespace

void registrarTodo(const std::filesystem::path& dataRoot, const char* componente) {
    // Antes que nada: si esto se cae, que deje la pila escrita. Ver
    // core/diag/Caidas.hpp — paso una vez que A murio con SIGSEGV y en el
    // registro no quedo ni una linea.
    ba::diag::instalarManejadorDeCaidas(componente);

    auto& reg = ba::diag::registro();
    reg.setComponentePorDefecto(componente);
    // Con el launcher, el archivo lo escribe la captura de stderr; suelto, se
    // abre uno propio. Ver Registro::abrirSegunEntorno.
    reg.abrirSegunEntorno(dataRoot / "Logs", std::string(componente) + ".log");

#if BA_REG_QT
    qInstallMessageHandler(handlerDeQt);
    // Los avisos de QML vienen apagados por defecto en un build de release de
    // Qt. Se prenden a propósito: son los que dicen que la interfaz se rompió.
    QLoggingCategory::setFilterRules("qt.qml.binding.removal.info=false\n"
                                     "qml.debug=false\n"
                                     "*.warning=true\n");
#endif
#if BA_REG_OBS
    base_set_log_handler(handlerDeObs, nullptr);
#endif
    reg.info("registro abierto (" + std::string(componente) + ")");
}

} // namespace ba::app
