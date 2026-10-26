// ============================================================================
// src/app/tests/test_registro_totems.cpp — la configuración de los tótems
//
//   1. Ids y claves: qué sirve, el id que sale del nombre, sin choques.
//   2. Un tótem nuevo trae valores razonables (nombre y correo).
//   3. Validar: claves repetidas o inválidas, opciones sin opciones.
//   4. Cambiar: solo lo que viene, `campos` entero, y nada si no valida.
//   5. Los textos con {evento}, {nombre} y {hola}.
//   6. JSON y disco: ida y vuelta, archivo a mano incompleto, roto, ausente.
// ============================================================================
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "registro/Totems.hpp"

namespace fs = std::filesystem;
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

void ids() {
    CHECK(idValido("entrada-1"), "id válido");
    CHECK(!idValido(""), "vacío no");
    CHECK(!idValido("Entrada"), "mayúsculas no");
    CHECK(!idValido("con espacio"), "espacios no");
    CHECK(!idValido(std::string(33, 'a')), "más de 32 no");
    CHECK(claveValida("viene_acompanado"), "clave válida");
    CHECK(!claveValida("con-guion"), "clave con guion no");
    CHECK(idDesdeNombre("Entrada principal", {}) == "entrada-principal", "id del nombre");
    CHECK(idDesdeNombre("Recepción Ñandú!!", {}) == "recepcion-nandu", "sin acentos ni signos");
    CHECK(idDesdeNombre("Entrada principal", {"entrada-principal"}) == "entrada-principal-2", "sin choques");
    CHECK(idDesdeNombre("Entrada principal", {"entrada-principal", "entrada-principal-2"}) == "entrada-principal-3",
          "sin choques, otra vez");
    CHECK(idDesdeNombre("¡¡!!", {}) == "totem", "sin letras");
    CHECK(idValido(idDesdeNombre(std::string(80, 'x'), {})), "largo: se recorta");
}

void nuevoYValidar() {
    const Totem t = totemNuevo("hall", "Hall");
    CHECK(t.activo && t.confirmar && t.reconocer, "prendido, confirma y reconoce");
    CHECK(t.campos.size() == 2 && t.campos[0].tipo == TipoCampo::Nombre && t.campos[0].obligatorio, "nombre obligatorio");
    CHECK(t.campos[1].tipo == TipoCampo::Correo && !t.campos[1].obligatorio, "correo opcional");
    CHECK(t.saludo.find("{evento}") != std::string::npos, "el saludo usa el nombre del evento");
    CHECK(validar(t).empty(), "un tótem nuevo valida");

    Totem mal = t;
    mal.campos[1].clave = "nombre";
    CHECK(!validar(mal).empty(), "clave repetida");
    mal = t;
    mal.campos[1].clave = "Correo Electrónico";
    CHECK(!validar(mal).empty(), "clave inválida");
    mal = t;
    mal.campos[1].tipo = TipoCampo::Opcion;
    mal.campos[1].opciones = {"una"};
    CHECK(!validar(mal).empty(), "opción con una sola");
    mal = t;
    mal.nombre.clear();
    CHECK(!validar(mal).empty(), "sin nombre");

    TipoCampo x;
    for (TipoCampo tc : todosLosTipos()) {
        CHECK(tipoDeNombre(nombreDeTipo(tc), x) && x == tc, "tipo ida y vuelta");
        CHECK(!preguntaDeFabrica(tc).empty(), "pregunta de fábrica");
    }
    CHECK(!tipoDeNombre("fecha", x), "tipo desconocido");
}

void lista() {
    Totems ts;
    CHECK(ts.agregar(totemNuevo("", "Entrada principal")).empty(), "agrega con id del nombre");
    CHECK(ts.porId("entrada-principal") != nullptr, "y se encuentra");
    CHECK(!ts.agregar(totemNuevo("entrada-principal", "Otra")).empty(), "id repetido no");
    CHECK(ts.agregar(totemNuevo("", "Entrada principal")).empty() && ts.lista().size() == 2, "mismo nombre, otro id");

    // Cambiar solo lo que viene.
    CHECK(ts.cambiar("entrada-principal", {{"saludo", "Hola, {evento}"}, {"confirmar", false}}).empty(), "cambio parcial");
    const Totem* t = ts.porId("entrada-principal");
    CHECK(t->saludo == "Hola, {evento}" && !t->confirmar && t->campos.size() == 2, "cambió solo eso");
    // Campos enteros.
    const json campos = json::array({{{"clave", "empresa"}, {"etiqueta", "Empresa"}, {"tipo", "texto"}},
                                     {{"clave", "sala"}, {"tipo", "opcion"}, {"opciones", {"A", "B"}}, {"obligatorio", true}}});
    CHECK(ts.cambiar("entrada-principal", {{"campos", campos}}).empty(), "campos enteros");
    t = ts.porId("entrada-principal");
    CHECK(t->campos.size() == 2 && t->campos[1].opciones.size() == 2 && t->campos[1].obligatorio, "campos nuevos");
    // Si no valida, no cambia nada (ni lo que sí estaba bien).
    const json malos = json::array({{{"clave", "x"}}, {{"clave", "x"}}});
    CHECK(!ts.cambiar("entrada-principal", {{"nombre", "Otro"}, {"campos", malos}}).empty(), "no valida");
    CHECK(ts.porId("entrada-principal")->nombre == "Entrada principal", "y no tocó el nombre");
    CHECK(!ts.cambiar("entrada-principal", {{"color", "rojo"}}).empty(), "clave desconocida");
    CHECK(!ts.cambiar("no-existe", {{"nombre", "x"}}).empty(), "tótem que no existe");
    CHECK(ts.cambiar("entrada-principal", {{"id", "otro"}}).empty() && ts.porId("entrada-principal") != nullptr,
          "el id no cambia");
    CHECK(ts.quitar("entrada-principal") && ts.porId("entrada-principal") == nullptr, "quitar");
    CHECK(!ts.quitar("entrada-principal"), "quitar dos veces");
}

void textos() {
    CHECK(conDatos("{hola}. Bienvenida a {evento}.", "Expo Agro", "Ana María Pérez") == "Hola Ana. Bienvenida a Expo Agro.",
          "con nombre: el de pila");
    CHECK(conDatos("{hola}. Bienvenida a {evento}.", "Expo Agro", "") == "Hola. Bienvenida a Expo Agro.", "sin nombre");
    CHECK(conDatos("Gracias {nombre}, nos vemos.", "X", "") == "Gracias, nos vemos.", "nombre vacío sin coma suelta");
    CHECK(conDatos("Gracias {nombre}!", "X", "") == "Gracias!", "nombre vacío antes de !");
    CHECK(conDatos("Bienvenida a {evento}", "", "") == "Bienvenida a el evento", "sin evento");
}

void jsonYDisco() {
    Totems ts;
    Totem t = totemNuevo("hall", "Hall");
    t.campos[1].ejemplo = "ana arroba gmail punto com";
    ts.agregar(t);
    ts.agregar(totemNuevo("puerta", "Puerta"));
    const Totems vuelta = Totems::deJson(ts.aJson());
    CHECK(vuelta.lista().size() == 2 && vuelta.porId("hall")->campos[1].ejemplo == "ana arroba gmail punto com",
          "ida y vuelta");

    // Un archivo a mano con lo mínimo: el resto, de fábrica. Y uno roto no tira los demás.
    const json aMano = json::parse(R"({"totems":[{"id":"a","campos":[{"clave":"dni","tipo":"numero"}]},
                                                   {"nombre":"sin id"}, {"id":"a"}, {"id":"b","activo":false}]})");
    const Totems leido = Totems::deJson(aMano);
    CHECK(leido.lista().size() == 2, "sin id y repetido se descartan");
    CHECK(leido.porId("a")->campos.size() == 1 && leido.porId("a")->campos[0].tipo == TipoCampo::Numero, "campos propios");
    CHECK(!leido.porId("a")->saludo.empty(), "saludo de fábrica");
    CHECK(!leido.porId("b")->activo && leido.porId("b")->campos.size() == 2, "apagado, campos de fábrica");

    const fs::path tmp = fs::temp_directory_path() / "ba_test_registro_totems";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);
    bool ok = false;
    CHECK(cargarTotems(tmp, &ok).lista().empty() && ok, "sin archivo: vacío y sin error");
    CHECK(guardarTotems(tmp, ts), "guarda");
    CHECK(!fs::exists(tmp / "registro.json.tmp"), "sin temporal");
    CHECK(cargarTotems(tmp, &ok).lista().size() == 2 && ok, "lee");
    { std::ofstream(tmp / "registro.json") << "{roto"; }
    std::string err;
    cargarTotems(tmp, &ok, &err);
    CHECK(!ok && !err.empty(), "roto avisa");
    CHECK(!guardarTotems({}, ts, &err), "sin carpeta no guarda");
    fs::remove_all(tmp, ec);
}

} // namespace

int main() {
    ids();
    nuevoYValidar();
    lista();
    textos();
    jsonYDisco();
    if (g_fallos == 0) std::printf("test_registro_totems: todo bien\n");
    return g_fallos == 0 ? 0 : 1;
}
