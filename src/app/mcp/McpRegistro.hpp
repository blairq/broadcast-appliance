// ============================================================================
// src/app/mcp/McpRegistro.hpp — Lo que ven las familias de McpService::registerTools()
//
// registerTools() (McpService.cpp) registra lo general (las categorías, la botonera, el
// control web, el guion, el tema, las preguntas, el sincronizador, el perfil y el estado)
// y después llama a cada familia, SIEMPRE EN ESTE ORDEN (el de tools/list):
//   registerEscenasTools      McpEscenasTools.cpp      escenas y el bus de preview
//   registerFuentesTools      McpFuentesTools.cpp      las fuentes de cada escena
//   registerMezclaTools       McpMezclaTools.cpp       el audio de la mezcla
//   registerTransmisionTools  McpTransmisionTools.cpp  transmitir y grabar
//   registerInformesTools     McpInformesTools.cpp     los informes de la sesión
//   registerDebateTools       McpDebateTools.cpp       el debate entre dos
//   registerRecintoTools      McpRecintoTools.cpp      el recinto y las bancas
//   registerGrabacionTools    McpGrabacionTools.cpp    disco, diag_* (--test), grabación,
//                                                      destinos, repetición, señal limpia
//   registerConfigTools       McpConfigTools.cpp       los ajustes
// Son funciones llamadas, no registros por inicializadores estáticos (docs/local/
// inventario-autorregistro.md): partir el archivo no cambia qué se registra ni en qué orden.
// Antes era un solo McpService.cpp de 4300 líneas (31 s de compilación, la cola del build).
//
// Uso interno del MCP: lo incluyen McpService.cpp y los .cpp de cada familia.
// ============================================================================
#pragma once

#include "persist/ColaEscritura.hpp"
#include "McpRenombres.hpp"
#include "arranque/EnergiaAire.hpp"
#include "fault/ModoSeguro.hpp"
#include "i18n/I18nTexto.hpp"
#include "diag/Acoples.hpp"
#include "diag/Perfil.hpp"
#include "sala/CupoSala.hpp"
#include "sala/SeguridadSala.hpp"
#include "escenas/Conmutador.hpp"
#include "escenas/RespaldoEntradas.hpp"
#include "mcp/McpService.hpp"
#include "ptz/ControlCamaras.hpp"
#include "Piezas.hpp" // motor::hayFabrica
#include "asamblea/Debate.hpp"
#include "asamblea/Presidencia.hpp"
#include <QRegularExpression>
#include <QDateTime>
#include <QRandomGenerator>

#include "agent/AgentViewModel.hpp"
#include "agent/Despachador.hpp"
#include "agent/Roles.hpp"
#include "agent/Voz.hpp"
#include "mcp/McpTraza.hpp"
#include "ui/PersonasViewModel.hpp"

#include <QBuffer>
#include <QCoreApplication>
#include <QTimer>
#include <QPointer>

#include "fault/FallasProgramadas.hpp"
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QQuickWindow>

#include <algorithm>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstring>
#include <memory>
#include <set>


#include <fstream>

#include "arranque/RuntimeMotor.hpp"
#include "arranque/PlanoControl.hpp"
#include "ajustes/Ajustes.hpp"
#include <QThread>
#include <QThreadPool>
#include "arranque/AppSettings.hpp"
#include "emision/InputSpecs.hpp"
#include "emision/RetornosInvitados.hpp"
#include "emision/SeguridadAire.hpp"
#include "emision/VentanillaMotor.hpp"
#include "audio/Matriz.hpp"
#include "audio/ObjetivoLoudness.hpp"
#include "mcp/McpBotoneraTools.hpp"
#include "mcp/McpControlWebTools.hpp"
#include "mcp/McpEventoTools.hpp"
#include "mcp/McpTemaTools.hpp"
#include "mcp/McpHttp.hpp"
#include "mcp/McpPreguntaTools.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "platform/Paths.hpp"
#include "agent/Notificador.hpp"
#include "ui/AppViewModel.hpp"
#include "ui/AtencionViewModel.hpp"
#include "ui/EmisionViewModel.hpp"
#include "ui/ConferenceClient.hpp"
#include "ui/SystemViewModel.hpp"
#include "ui/ConferenceViewModel.hpp"
#include "ui/EventoViewModel.hpp"
#include "ui/TemaEventoViewModel.hpp"
#include "agent/SubtitulosCC.hpp"
#include "agent/Vocabulario.hpp"
#include "asamblea/Bancas.hpp"
#include "asamblea/SeguirMicrofono.hpp"
#include "asamblea/OrdenDelDia.hpp"
#include "asamblea/InformesResultado.hpp"
#include "asamblea/CuerpoViewModel.hpp"
#include "ui/ListModels.hpp"
#include "ui/OperatorMessages.hpp"
#include "ui/PropertiesModel.hpp"
#include "ui/RecursosViewModel.hpp"
#include "control/BusEventos.hpp"
#include "video/ElementosGst.hpp"
#include "video/FuentesCaptura.hpp"
#include "Capturas.hpp" // del motor: hayCapturaDeVentana
#include "ui/SettingsModel.hpp"
#include "mcp/InterfazMcp.hpp"
#include "platform/Hilo.hpp"
#include "ui/CapturaError.hpp"
