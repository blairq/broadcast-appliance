// ============================================================================
// src/app/tests/test_registro_atencion.cpp — una conversación de registro atendida
// por una persona (la atención remota, docs/funciones/REGISTRO-TOTEM.md)
//
//   1. Atender pausa el guion: lo oído, lo tipeado, los silencios y la foto no hacen
//      hablar al asistente; el paso dice quién la atiende.
//   2. Poner un dato desde afuera: atendiendo (callado) y sin atender (como lo
//      tipeado: si era el que se pedía, el guion sigue); borrar; lo que no se entiende.
//   3. Devolver al asistente: sigue desde el próximo campo sin dato.
//   4. Guardar ya: sin la foto, con los obligatorios; y que diga cuáles faltan.
//   5. Atender una que no empezó (empieza callada) y una que terminó (no).
// ============================================================================
#include <cstdio>
#include <string>

#include "registro/Conversacion.hpp"

using namespace ba::registro;
using nlohmann::json;

namespace {

int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (false)

bool contiene(const std::string& s, const std::string& x) { return s.find(x) != std::string::npos; }

/// Nombre (obligatorio), correo y teléfono; con foto al final y confirmando.
Totem totemDePrueba() {
    Totem t = totemNuevo("entrada", "Entrada");
    t.foto = true;
    t.confirmar = true;
    Campo tel;
    tel.clave = "telefono";
    tel.etiqueta = "Teléfono";
    tel.tipo = TipoCampo::Telefono;
    t.campos.push_back(tel);
    return t;
}

void pausaElGuion() {
    Conversacion cv(totemDePrueba(), "Expo");
    auto r = cv.empezar({});
    CHECK(r.pedir == "nombre", "empieza pidiendo el nombre");
    CHECK(cv.atender("Laura") && cv.atendida() && cv.atendidaPor() == "Laura", "la atiende Laura");
    CHECK(cv.paso() == "la atiende Laura", "el paso dice quién la atiende");
    r = cv.oido("me llamo ana gómez");
    CHECK(r.decir.empty() && !r.campos && cv.datos().empty(), "lo oído no cambia nada ni la hace hablar");
    r = cv.silencio();
    r = cv.silencio();
    CHECK(r.decir.empty() && !r.terminar && cv.enCurso(), "los silencios no cortan: está hablando con una persona");
    r = cv.escrito("correo", "ana@ejemplo.com");
    CHECK(r.decir.empty() && r.campos && cv.datos().value("correo", "") == "ana@ejemplo.com",
          "lo tipeado en el tótem se anota callado");
    CHECK(cv.fotoLista().decir.empty() && cv.fotoFallo("acercate").decir.empty(), "la foto no la hace hablar");
    Conversacion sinNombre(totemDePrueba(), "Expo");
    sinNombre.empezar({});
    sinNombre.atender("");
    CHECK(sinNombre.paso() == "la atiende alguien del equipo", "sin nombre: alguien del equipo");
}

void ponerDatos() {
    Conversacion cv(totemDePrueba(), "Expo");
    cv.empezar({});
    std::string error;
    // Sin atender: como tipearlo. Era el que se pedía: lo agradece y sigue.
    auto r = cv.poner("nombre", "ana gómez", error);
    CHECK(error.empty() && contiene(r.decir, "ya lo anoté") && r.pedir == "correo", "sin atender, sigue con el próximo");
    CHECK(cv.datos().value("nombre", "") == "Ana Gómez", "el nombre, prolijo");
    // Sin atender y no es el que se pide: se anota sin cambiar la pregunta.
    r = cv.poner("telefono", "11 4567 8901", error);
    CHECK(error.empty() && r.campos && r.pedir == "correo" && cv.datos().value("telefono", "") == "1145678901",
          "otro campo: se anota y sigue preguntando lo mismo");
    // Lo que no se entiende no se anota.
    r = cv.poner("correo", "esto no es un correo", error);
    CHECK(!error.empty() && contiene(error, "correo") && !cv.datos().contains("correo"), "un correo malo: el error dice por qué");
    r = cv.poner("edad", "30", error);
    CHECK(contiene(error, "edad"), "un campo que no existe");
    // Atendiendo: callado, también el que se pide, y mientras se confirmaba.
    cv.oido("ana arroba gmail punto com");
    CHECK(cv.estado() == Conversacion::Estado::Confirmando, "estaba confirmando el correo");
    cv.atender("Laura");
    CHECK(cv.estado() == Conversacion::Estado::Preguntando, "al atender, lo que se confirmaba queda sin confirmar");
    r = cv.poner("correo", "ana.gomez@gmail.com", error);
    CHECK(error.empty() && r.decir.empty() && r.campos && cv.datos().value("correo", "") == "ana.gomez@gmail.com",
          "atendiendo: el dato que se pedía, callado");
    // Corregir es también borrar.
    r = cv.poner("telefono", "  ", error);
    CHECK(error.empty() && r.campos && !cv.datos().contains("telefono"), "vacío borra el dato");
    CHECK(cv.camposParaMostrar()[2]["listo"] == false, "y se muestra sin dato");
}

void devolverAlAsistente() {
    Conversacion cv(totemDePrueba(), "Expo");
    cv.empezar({});
    cv.atender("Laura");
    std::string error;
    cv.poner("nombre", "Ana Gómez", error);
    cv.poner("correo", "ana@gmail.com", error);
    auto r = cv.devolver();
    CHECK(!cv.atendida() && cv.atendidaPor().empty(), "ya no está atendida");
    CHECK(contiene(r.decir, "Sigo yo.") && r.pedir == "telefono" && r.campos,
          "el guion sigue desde el próximo campo sin dato");
    CHECK(cv.devolver().decir.empty(), "devolver dos veces no hace nada");
    // Y el resto anda como siempre: el teléfono (confirmando) y la foto.
    r = cv.oido("once cuatro cinco seis siete ocho nueve cero uno dos");
    CHECK(cv.estado() == Conversacion::Estado::Confirmando, "confirma el teléfono");
    r = cv.oido("sí");
    CHECK(r.foto && cv.estado() == Conversacion::Estado::Foto, "y pasa a la foto");
    // Atender en la foto y devolver: vuelve a pedirla.
    cv.atender("Laura");
    r = cv.devolver();
    CHECK(r.foto && contiene(r.decir, "mirá derecho a la cámara"), "devuelta en la foto: la pide de nuevo");
}

void guardarYa() {
    Conversacion cv(totemDePrueba(), "Expo");
    cv.empezar({});
    cv.atender("Laura");
    std::string error;
    auto r = cv.guardarYa(error);
    CHECK(!r.guardar && contiene(error, "Nombre"), "sin el nombre (obligatorio) no guarda, y dice qué falta");
    CHECK(cv.faltan().size() == 1 && cv.faltan()[0] == "Nombre", "faltan: el nombre");
    cv.poner("nombre", "Ana Gómez", error);
    r = cv.guardarYa(error);
    CHECK(error.empty() && r.guardar && r.terminar && !r.foto, "guarda ya, sin el paso de la foto");
    CHECK(contiene(r.decir, "ya quedó tu registro") && cv.estado() == Conversacion::Estado::Terminada && !cv.atendida(),
          "se despide (la despedida del tótem) y termina");
    CHECK(cv.datos().value("nombre", "") == "Ana Gómez" && !cv.datos().contains("correo"), "con lo que había");
    r = cv.guardarYa(error);
    CHECK(!r.guardar && !error.empty(), "terminada: no se guarda dos veces");
    CHECK(!cv.atender("Laura"), "una que terminó no se atiende");
}

void atenderSinEmpezar() {
    Conversacion cv(totemDePrueba(), "Expo");
    CHECK(cv.atender("Laura") && cv.enCurso() && cv.estado() == Conversacion::Estado::Preguntando,
          "sin empezar: empieza callada");
    CHECK(cv.pedido().value("clave", "") == "nombre", "en el primer campo sin dato");
    std::string error;
    cv.poner("nombre", "Ana", error);
    cv.poner("correo", "ana@gmail.com", error);
    cv.poner("telefono", "1145678901", error);
    auto r = cv.devolver();
    CHECK(r.foto, "con todo puesto, al devolverla va a la foto");
    // Cancelar sigue andando atendida.
    Conversacion c2(totemDePrueba(), "Expo");
    c2.atender("Laura");
    r = c2.cancelar();
    CHECK(r.terminar && !r.guardar && c2.estado() == Conversacion::Estado::Terminada, "cancelar, atendida, termina sin guardar");
}

} // namespace

int main() {
    pausaElGuion();
    ponerDatos();
    devolverAlAsistente();
    guardarYa();
    atenderSinEmpezar();
    if (g_fallos == 0) std::printf("test_registro_atencion: todo bien\n");
    return g_fallos == 0 ? 0 : 1;
}
