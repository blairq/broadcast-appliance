// ============================================================================
// src/app/ui/InterfazMcp.cpp — ver mcp/InterfazMcp.hpp
//
// Cada miembro reenvía al de StudioViewModel con el mismo nombre (las señales se emiten
// en él). Es el único lugar del MCP que ve ui/StudioViewModel.hpp.
// ============================================================================
#include "mcp/InterfazMcp.hpp"

#include "arranque/RuntimeConfig.hpp"
#include "ui/StudioViewModel.hpp"

namespace ba::app {

InterfazMcp::InterfazMcp(StudioViewModel& studio) : QObject(&studio), studio_(studio) {}

InterfazMcp::~InterfazMcp() = default;

void InterfazMcp::abrirPanel(const QString& pagina, const QString& panel) { emit studio_.abrirPanel(pagina, panel); }
AccesoRemoto* InterfazMcp::accesoRemoto() { return studio_.accesoRemoto(); }
control::Acciones& InterfazMcp::acciones() { return studio_.acciones(); }
QObject* InterfazMcp::agent() const { return studio_.agent(); }
QObject* InterfazMcp::agenteAutomatizacion() const { return studio_.agenteAutomatizacion(); }
EstadoAireViewModel& InterfazMcp::aire() { return studio_.aire(); }
bool InterfazMcp::alAire() const { return studio_.alAire(); }
agent::AlertEngine& InterfazMcp::alerts() { return studio_.alerts(); }
AppViewModel& InterfazMcp::app() { return studio_.app(); }
AsambleaViewModel& InterfazMcp::asamblea() { return studio_.asamblea(); }
AsistenteViewModel& InterfazMcp::asistente() { return studio_.asistente(); }
AtencionViewModel* InterfazMcp::atencionVm() const { return studio_.atencionVm(); }
QObject* InterfazMcp::audio() const { return studio_.audio(); }
asamblea::Bancas& InterfazMcp::bancas() { return studio_.bancas(); }
video::BancoSuperRes* InterfazMcp::bancoSr() const { return studio_.bancoSr(); }
control::Botonera& InterfazMcp::botonera() { return studio_.botonera(); }
control::BotoneraGateway& InterfazMcp::botoneraGateway() { return studio_.botoneraGateway(); }
control::BusEventos& InterfazMcp::bus() { return studio_.bus(); }
QObject* InterfazMcp::camaras() const { return studio_.camaras(); }
QString InterfazMcp::canvasLabel() const { return studio_.canvasLabel(); }
CapacidadViewModel& InterfazMcp::capacidad() { return studio_.capacidad(); }
control::CarasBotonera& InterfazMcp::carasBotonera() { return studio_.carasBotonera(); }
QObject* InterfazMcp::carruseles() const { return studio_.carruseles(); }
CatalogoVisible* InterfazMcp::catalogoVisible() const { return studio_.catalogoVisible(); }
agent::SubtitulosCC& InterfazMcp::cc() { return studio_.cc(); }
QObject* InterfazMcp::conference() const { return studio_.conference(); }
sala::ConfigSala& InterfazMcp::configSala() { return studio_.configSala(); }
Configuraciones* InterfazMcp::configuraciones() { return studio_.configuraciones(); }
escenas::Conmutador& InterfazMcp::conmutador() { return studio_.conmutador(); }
escenas::Conmutadores& InterfazMcp::conmutadores() { return studio_.conmutadores(); }
ControlViewModel& InterfazMcp::control() { return studio_.control(); }
control::ControlWeb& InterfazMcp::controlWeb() { return studio_.controlWeb(); }
control::ControlWebGateway& InterfazMcp::controlWebGateway() { return studio_.controlWebGateway(); }
double InterfazMcp::cpuPercent() const { return studio_.cpuPercent(); }
CuentasViewModel& InterfazMcp::cuentas() { return studio_.cuentas(); }
asamblea::CuerpoViewModel& InterfazMcp::cuerpo() { return studio_.cuerpo(); }
sala::CupoSala& InterfazMcp::cupoSala() { return studio_.cupoSala(); }
QString InterfazMcp::currentScene() const { return studio_.currentScene(); }
QObject* InterfazMcp::dashboard() const { return studio_.dashboard(); }
std::filesystem::path InterfazMcp::datosRaiz() const { return studio_.datosRaiz(); }
asamblea::ConferenciaDcn& InterfazMcp::dcn() { return studio_.dcn(); }
asamblea::Debate* InterfazMcp::debate() { return studio_.debate(); }
agent::Despachador& InterfazMcp::despachador() { return studio_.despachador(); }
sala::Direcciones& InterfazMcp::direcciones() { return studio_.direcciones(); }
DocumentosViewModel* InterfazMcp::documentos() const { return studio_.documentos(); }
EmisionViewModel& InterfazMcp::emision() { return studio_.emision(); }
graficos::Emisor* InterfazMcp::emisor() { return studio_.emisor(); }
EnergiaAire* InterfazMcp::energia() const { return studio_.energia(); }
EnlaceViewModel& InterfazMcp::enlace() { return studio_.enlace(); }
EnlacesViewModel* InterfazMcp::enlacesVm() const { return studio_.enlacesVm(); }
dispositivos::Dispositivos* InterfazMcp::equipos() const { return studio_.equipos(); }
EscenasViewModel& InterfazMcp::escenas() { return studio_.escenas(); }
EscritorioViewModel& InterfazMcp::escritorio() { return studio_.escritorio(); }
agent::EstadoIA* InterfazMcp::estadoIAModelo() const { return studio_.estadoIAModelo(); }
QObject* InterfazMcp::evento() const { return studio_.evento(); }
QObject* InterfazMcp::extensiones() const { return studio_.extensiones(); }
QObject* InterfazMcp::facebook() const { return studio_.facebook(); }
QObject* InterfazMcp::flujos() const { return studio_.flujos(); }
QObject* InterfazMcp::frames() const { return studio_.frames(); }
bool InterfazMcp::gatewayAlive() const { return studio_.gatewayAlive(); }
escenas::ControlGrillas* InterfazMcp::grillasPersonas() { return studio_.grillasPersonas(); }
Fluidez* InterfazMcp::fluidez() { return studio_.fluidez(); }
HistoricoViewModel* InterfazMcp::historico() const { return studio_.historico(); }
asamblea::InformesResultado* InterfazMcp::informes() { return studio_.informes(); }
QObject* InterfazMcp::inputs() const { return studio_.inputs(); }
QObject* InterfazMcp::matriz() const { return studio_.matriz(); }
QObject* InterfazMcp::medios() const { return studio_.medios(); }
QObject* InterfazMcp::messages() const { return studio_.messages(); }
MezclaViewModel& InterfazMcp::mezcla() { return studio_.mezcla(); }
QObject* InterfazMcp::mezcladorQml() const { return studio_.mezcladorQml(); }
QObject* InterfazMcp::mixer() const { return studio_.mixer(); }
agent::ModelosLocales* InterfazMcp::modelosLocales() const { return studio_.modelosLocales(); }
bool InterfazMcp::modoVivo() const { return studio_.modoVivo(); }
MonitoresModel* InterfazMcp::monitoresModelo() const { return studio_.monitoresModelo(); }
void InterfazMcp::mostrarQr(const QString& texto, const QString& descripcion, const QString& mensaje, const QString& color) { emit studio_.mostrarQr(texto, descripcion, mensaje, color); }
void InterfazMcp::mostrarSeccion(const QString& pagina, const QString& seccion) { emit studio_.mostrarSeccion(pagina, seccion); }
MultiviewViewModel& InterfazMcp::multivistas() { return studio_.multivistas(); }
agent::Notificador& InterfazMcp::notificador() { return studio_.notificador(); }
void InterfazMcp::notify(const QString& message, const QString& level) { emit studio_.notify(message, level); }
audio::ObjetivoLoudness& InterfazMcp::objetivoLoudness() { return studio_.objetivoLoudness(); }
asamblea::OrdenDelDia& InterfazMcp::ordenDelDia() { return studio_.ordenDelDia(); }
QObject* InterfazMcp::pantallas() const { return studio_.pantallas(); }
PdfEscena* InterfazMcp::pdfEscena() const { return studio_.pdfEscena(); }
QObject* InterfazMcp::personas() const { return studio_.personas(); }
medios::Playlist& InterfazMcp::playlist() { return studio_.playlist(); }
plugins::Plugins* InterfazMcp::plugins() const { return studio_.plugins(); }
sala::PreguntasSala& InterfazMcp::preguntas() { return studio_.preguntas(); }
asamblea::Presidencia* InterfazMcp::presidencia() { return studio_.presidencia(); }
double InterfazMcp::programPeakL() const { return studio_.programPeakL(); }
double InterfazMcp::programPeakR() const { return studio_.programPeakR(); }
QObject* InterfazMcp::prompter() const { return studio_.prompter(); }
PruebaAsambleaViewModel& InterfazMcp::pruebaAsamblea() { return studio_.pruebaAsamblea(); }
PruebaSalaViewModel& InterfazMcp::pruebaSala() { return studio_.pruebaSala(); }
QObject* InterfazMcp::qr() const { return studio_.qr(); }
void InterfazMcp::quitRequested() { emit studio_.quitRequested(); }
asamblea::Quorum& InterfazMcp::quorum() { return studio_.quorum(); }
audio::RackManager* InterfazMcp::racks() const { return studio_.racks(); }
RecursosViewModel* InterfazMcp::recursos() const { return studio_.recursos(); }
red::Red* InterfazMcp::redEquipos() const { return studio_.redEquipos(); }
resolucion::ResolucionModel* InterfazMcp::resolucionModel() const { return studio_.resolucionModel(); }
escenas::RespaldoEntradas& InterfazMcp::respaldoEntradas() { return studio_.respaldoEntradas(); }
emision::RetornosInvitados& InterfazMcp::retornos() { return studio_.retornos(); }
QObject* InterfazMcp::riel() const { return studio_.riel(); }
AppRuntime& InterfazMcp::runtime() { return studio_.runtime(); }
SalaViewModel& InterfazMcp::sala() { return studio_.sala(); }
SalidasViewModel& InterfazMcp::salidas() { return studio_.salidas(); }
control::SatelliteServidor& InterfazMcp::satellite() { return studio_.satellite(); }
Seguidor* InterfazMcp::seguidorPtz() const { return studio_.seguidorPtz(); }
asamblea::SeguirMicrofono& InterfazMcp::seguirMicrofono() { return studio_.seguirMicrofono(); }
sala::SeguridadSala& InterfazMcp::seguridad() { return studio_.seguridad(); }
emision::SeguridadAire& InterfazMcp::seguridadAire() { return studio_.seguridadAire(); }
asamblea::SesionRemota* InterfazMcp::sesionRemota() { return studio_.sesionRemota(); }
void InterfazMcp::setSeccionConfig(const QString& s) { studio_.setSeccionConfig(s); }
QObject* InterfazMcp::settings() const { return studio_.settings(); }
QStringList InterfazMcp::sinSenal() const { return studio_.sinSenal(); }
bool InterfazMcp::streamKeyAvailable() const { return studio_.streamKeyAvailable(); }
QString InterfazMcp::streamServer() const { return studio_.streamServer(); }
video::SuperRes* InterfazMcp::superresVideo() const { return studio_.superresVideo(); }
QObject* InterfazMcp::system() const { return studio_.system(); }
QObject* InterfazMcp::temaEvento() const { return studio_.temaEvento(); }
tiempos::TiemposModel* InterfazMcp::tiemposModel() const { return studio_.tiemposModel(); }
TotemsViewModel* InterfazMcp::totemsVm() const { return studio_.totemsVm(); }
agent::Traductor* InterfazMcp::traductorAgent() const { return studio_.traductorAgent(); }
QObject* InterfazMcp::twitch() const { return studio_.twitch(); }
VentanasModel* InterfazMcp::ventanasModelo() const { return studio_.ventanasModelo(); }
VentanillaMotor& InterfazMcp::ventanilla() { return studio_.ventanilla(); }
video::VideoGrafos* InterfazMcp::videoGrafos() const { return studio_.videoGrafos(); }
VideoViewModel* InterfazMcp::videoVista() const { return studio_.videoVista(); }
graficos::Vinculos* InterfazMcp::vinculos() { return studio_.vinculos(); }
agent::Vocabulario& InterfazMcp::vocabulario() { return studio_.vocabulario(); }
QObject* InterfazMcp::voces() const { return studio_.voces(); }
sala::VotacionSala& InterfazMcp::votacion() { return studio_.votacion(); }
asamblea::Votaciones* InterfazMcp::votaciones() { return studio_.votaciones(); }
QObject* InterfazMcp::voz() const { return studio_.voz(); }
QObject* InterfazMcp::vozPrompter() const { return studio_.vozPrompter(); }
QObject* InterfazMcp::youtube() const { return studio_.youtube(); }

} // namespace ba::app
