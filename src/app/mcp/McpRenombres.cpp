// Nombres VIEJOS de herramientas renombradas en bloque desde el perfil de
// entrenamiento: van a Tool::antes de la de hoy, como los que cada herramienta
// declara al definirse. Lo guardado con el nombre viejo (recetas, botoneras,
// disparadores) y los modelos ya publicados que lo escriben siguen andando. Lo
// escribe tools/dev/renombrar-herramientas.py (perfil vivo6); no aparecen
// en tools/list ni en el buscador.
#include "McpRenombres.hpp"

#include <QDebug>

namespace ba::app::mcp {

void registrarRenombres(McpCore& core) {
    static const std::pair<const char*, const char*> kRenombres[] = {
        {"banca_terminar", "recinto_orador_terminar"},
        {"bancas_listar", "recinto_bancas_ver"},
        {"capture_multiview", "foto_todas_escenas"},
        {"capture_people", "foto_todos_invitados"},
        {"capture_person", "foto_invitado"},
        {"capture_program", "foto_programa"},
        {"capture_source", "foto_fuente"},
        {"chat_history", "invitados_chat_leer"},
        {"chat_recientes", "publico_chat_leer"},
        {"dsk_listar", "dsk_capas_ver"},
        {"fuente_crear", "crear_fuente"},
        {"imagen_analizar", "imagen_preguntar"},
        {"laminas_listar", "carruseles_listar"},
        {"laminas_ver", "carrusel_laminas_ver"},
        {"media_use", "medio_poner"},
        {"pregunta_desde_chat", "chat_a_preguntas"},
        {"presidencia_silenciar", "presidencia_callar_todos"},
        {"ptz_home", "ptz_inicio"},
        {"ptz_rostros", "ptz_caras_ubicar"},
        {"quorum_estado", "quorum_presentes"},
        {"rostro_ver", "persona_identificar"},
        {"scene_take", "preview_al_aire"},
        {"share_view", "compartida_revisar"},
        {"source_add", "crear_fuente"},
        {"source_list", "escena_fuentes_ver"},
        {"speakers_status", "remotos_fila_ver"},
        {"votacion_ver", "votacion_recuento"},
    };
    for (const auto& [viejo, nuevo] : kRenombres)
        if (!core.agregarNombreAnterior(nuevo, viejo))
            qWarning("McpRenombres: no existe la herramienta %s (nombre viejo %s)", nuevo, viejo);
}

} // namespace ba::app::mcp
