// ============================================================================
// src/gst-plugins/superres/glsl.cpp — baglsl: shaders de mpv (FSRCNNX, Anime4K…) en la GPU
//
// Proposito:
//   Elemento de GStreamer `baglsl` (un GstGLFilter): corre un «user shader» de
//   mpv —el formato en que se publican FSRCNNX, Anime4K, RAVU, NNEDI3…— sobre
//   la imagen, en la GPU, sin bajarla a la CPU (memory:GLMemory de entrada y
//   de salida). Es un intérprete del formato de ganchos de mpv:
//
//     //!HOOK LUMA|MAIN|NATIVE   sobre qué textura trabaja la pasada
//     //!BIND nombre             qué texturas lee (HOOKED, LUMA, MAIN o una guardada)
//     //!SAVE nombre             dónde deja el resultado (si no, reemplaza la enganchada)
//     //!WIDTH / //!HEIGHT expr  tamaño de la salida, en notación polaca inversa
//     //!WHEN expr               si la pasada corre (idem)
//     //!COMPONENTS n, //!DESC   (informativos)
//   Y le da a cada textura lo que esperan los shaders: NOMBRE_tex(pos),
//   NOMBRE_texOff(off), NOMBRE_pos, NOMBRE_size, NOMBRE_pt, NOMBRE_mul,
//   NOMBRE_raw; y input_size, target_size, frame, random.
//
//   LUMA es el brillo (BT.709) de la imagen, en una textura propia. Si el
//   shader agranda LUMA (FSRCNNX), al final se recombina con el color de la
//   imagen agrandada simple (como mpv). Si agranda MAIN (Anime4K), sale esa.
//   Las texturas intermedias son de media precisión (RGBA16F): las capas de
//   las redes tienen valores negativos y fuera de 0..1.
//
//   El tamaño de salida (x2 de FSRCNNX/Anime4K) sale de evaluar las pasadas al
//   negociar (OUTPUT = el doble de la entrada, como mpv en una pantalla más
//   grande): transform_internal_caps lo declara.
//
// Modelo de threading:
//   Todo lo de GL corre en el hilo de GL del contexto (GstGLFilter); el
//   shader se lee al poner la propiedad y se compila en el hilo de GL.
// ============================================================================

#include <gst/gl/gl.h>
#include <gst/gl/gstglfuncs.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#include "comun.hpp"
#include "../comun/glsl.hpp"

#include <cmath>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#ifndef GL_RGBA16F
#define GL_RGBA16F 0x881A
#endif
#ifndef GL_HALF_FLOAT
#define GL_HALF_FLOAT 0x140B
#endif

G_BEGIN_DECLS
#define BA_TYPE_GLSL (ba_glsl_get_type())
G_DECLARE_FINAL_TYPE(BaGlsl, ba_glsl, BA, GLSL, GstGLFilter)
G_END_DECLS

namespace {

/// Una pasada del shader.
struct Pasada {
    std::string desc, gancho, guardar, ancho, alto, cuando, codigo;
    std::vector<std::string> lee;
    GstGLShader* programa = nullptr;
};

/// Una textura (de GL) con su tamaño.
struct Tex {
    guint id = 0;
    int w = 0, h = 0;
};

std::string recortar(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\r");
    const auto b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

/// Lee un user shader de mpv: sus pasadas (los //!TEXTURE no se soportan).
bool leerShader(const std::string& texto, std::vector<Pasada>& out, std::string& error) {
    std::istringstream in(texto);
    std::string linea;
    Pasada actual;
    bool enCabecera = false, hay = false;
    const auto cerrar = [&]() {
        if (hay && !actual.gancho.empty()) out.push_back(actual);
        actual = Pasada{};
        hay = false;
    };
    while (std::getline(in, linea)) {
        if (linea.rfind("//!", 0) == 0) {
            if (!enCabecera) cerrar();
            enCabecera = true;
            hay = true;
            const std::string r = linea.substr(3);
            const auto sp = r.find(' ');
            const std::string clave = r.substr(0, sp);
            const std::string valor = sp == std::string::npos ? "" : recortar(r.substr(sp + 1));
            if (clave == "HOOK")
                actual.gancho = valor;
            else if (clave == "BIND")
                actual.lee.push_back(valor);
            else if (clave == "SAVE")
                actual.guardar = valor;
            else if (clave == "WIDTH")
                actual.ancho = valor;
            else if (clave == "HEIGHT")
                actual.alto = valor;
            else if (clave == "WHEN")
                actual.cuando = valor;
            else if (clave == "DESC")
                actual.desc = valor;
            else if (clave == "TEXTURE") {
                error = "el shader trae //!TEXTURE (tablas): no se soporta todavía";
                return false;
            }
        } else {
            enCabecera = false;
            if (hay) actual.codigo += linea + "\n";
        }
    }
    cerrar();
    if (out.empty()) error = "el archivo no tiene pasadas //!HOOK";
    return !out.empty();
}

/// Evalúa una expresión de mpv en notación polaca inversa ("LUMA.w 2 *").
double evaluar(const std::string& expr, const std::map<std::string, std::pair<int, int>>& tam, bool& ok) {
    std::vector<double> pila;
    std::istringstream in(expr);
    std::string t;
    ok = true;
    const auto sacar = [&]() {
        if (pila.empty()) {
            ok = false;
            return 0.0;
        }
        const double v = pila.back();
        pila.pop_back();
        return v;
    };
    while (in >> t) {
        if (t == "+" || t == "-" || t == "*" || t == "/" || t == ">" || t == "<" || t == "=") {
            const double b = sacar(), a = sacar();
            pila.push_back(t == "+"   ? a + b
                           : t == "-" ? a - b
                           : t == "*" ? a * b
                           : t == "/" ? (b != 0 ? a / b : 0)
                           : t == ">" ? (a > b ? 1 : 0)
                           : t == "<" ? (a < b ? 1 : 0)
                                      : (a == b ? 1 : 0));
        } else if (t == "!") {
            pila.push_back(sacar() == 0 ? 1 : 0);
        } else if (const auto p = t.find('.'); p != std::string::npos && !std::isdigit(static_cast<unsigned char>(t[0]))) {
            const std::string nombre = t.substr(0, p), campo = t.substr(p + 1);
            const auto it = tam.find(nombre);
            if (it == tam.end()) {
                ok = false;
                return 0;
            }
            pila.push_back(campo == "w" || campo == "width" ? it->second.first : it->second.second);
        } else {
            char* fin = nullptr;
            const double v = std::strtod(t.c_str(), &fin);
            if (fin == t.c_str()) {
                ok = false;
                return 0;
            }
            pila.push_back(v);
        }
    }
    return pila.size() == 1 ? pila.back() : (ok = false, 0.0);
}

const char* kVertice = "attribute vec4 a_position;\n"
                       "attribute vec2 a_texcoord;\n"
                       "varying vec2 v_texcoord;\n"
                       "void main() { gl_Position = a_position; v_texcoord = a_texcoord; }\n";

/// El fragmento de una pasada: las macros de mpv para cada textura + su código.
std::string fragmentoDe(const Pasada& p) {
    // Los tipos de GLSL 1.20+ que usan los shaders, en contextos GL 2 (GLSL 1.10).
    std::string s = "#ifdef GL_ES\nprecision highp float;\n#endif\n"
                    "#if !defined(GL_ES) && __VERSION__ < 130\n"
                    "#define mat2x2 mat2\n#define mat3x3 mat3\n#define mat4x4 mat4\n#endif\n"
                    "varying vec2 v_texcoord;\n"
                    "uniform vec2 input_size;\nuniform vec2 target_size;\nuniform int frame;\nuniform float random;\n"
                    "vec2 tex_offset = vec2(0.0);\n";
    std::vector<std::string> nombres{"HOOKED"};
    for (const auto& n : p.lee)
        if (n != "HOOKED") nombres.push_back(n);
    for (const auto& n : nombres) {
        s += "uniform sampler2D " + n + "_raw;\nuniform vec2 " + n + "_size;\nuniform vec2 " + n + "_pt;\n";
        s += "#define " + n + "_pos v_texcoord\n";
        s += "#define " + n + "_mul 1.0\n";
        s += "#define " + n + "_rot mat2(1.0, 0.0, 0.0, 1.0)\n";
        s += "#define " + n + "_off vec2(0.0)\n";
        s += "#define " + n + "_tex(pos) (" + n + "_mul * texture2D(" + n + "_raw, pos))\n";
        s += "#define " + n + "_texOff(off) " + n + "_tex(" + n + "_pos + " + n + "_pt * vec2(off))\n";
    }
    s += p.codigo;
    s += "\nvoid main() { gl_FragColor = hook(); }\n";
    return s;
}

// Lo propio (no del shader): el brillo, y la vuelta a la salida.
const char* kLuma = "#ifdef GL_ES\nprecision highp float;\n#endif\n"
                    "varying vec2 v_texcoord;\nuniform sampler2D MAIN_raw;\n"
                    "void main() { vec3 c = texture2D(MAIN_raw, v_texcoord).rgb;\n"
                    "  gl_FragColor = vec4(dot(c, vec3(0.2126, 0.7152, 0.0722)), 0.0, 0.0, 1.0); }\n";
const char* kSalida = "#ifdef GL_ES\nprecision highp float;\n#endif\n"
                      "varying vec2 v_texcoord;\nuniform sampler2D MAIN_raw;\nuniform sampler2D LUMA_raw;\n"
                      "uniform float con_luma;\n"
                      "void main() { vec4 c = texture2D(MAIN_raw, v_texcoord);\n"
                      "  if (con_luma > 0.5) {\n"
                      "    float y = texture2D(LUMA_raw, v_texcoord).r;\n"
                      "    float y0 = dot(c.rgb, vec3(0.2126, 0.7152, 0.0722));\n"
                      "    c.rgb += vec3(y - y0);\n"
                      "  }\n"
                      "  gl_FragColor = vec4(clamp(c.rgb, 0.0, 1.0), 1.0); }\n";

} // namespace

struct _BaGlsl {
    GstGLFilter parent;
    gchar* archivo;
    gchar* codigo;         ///< el shader en el texto (gana sobre `archivo`)
    GstStructure* uniformes; ///< floats que se le pasan a cada pasada (bajo GST_OBJECT_LOCK)
    gboolean activo;
    gchar* error;
    std::vector<Pasada>* pasadas;
    int factor; ///< lo que agranda el shader (1 o 2)
    // GL (solo en el hilo de GL)
    GstGLShader* luma;
    GstGLShader* salida;
    guint fbo, vbo, vao;
    std::map<std::string, Tex>* propias; ///< texturas intermedias por nombre (y ranura)
    guint64 cuadro;
};

G_DEFINE_TYPE(BaGlsl, ba_glsl, GST_TYPE_GL_FILTER)

GST_DEBUG_CATEGORY_STATIC(ba_glsl_debug);
#define GST_CAT_DEFAULT ba_glsl_debug

enum { PROP_0, PROP_SHADER, PROP_CODIGO, PROP_UNIFORMES, PROP_ACTIVO, PROP_FACTOR, PROP_PASADAS, PROP_ERROR };

static void ponerError(BaGlsl* s, const std::string& e) {
    GST_OBJECT_LOCK(s);
    g_free(s->error);
    s->error = e.empty() ? nullptr : g_strdup(e.c_str());
    GST_OBJECT_UNLOCK(s);
    if (!e.empty()) GST_WARNING_OBJECT(s, "%s", e.c_str());
}

/// Simula los tamaños (para saber cuánto agranda): el tamaño final de MAIN o LUMA.
static int factorDe(const std::vector<Pasada>& ps, int w, int h) {
    std::map<std::string, std::pair<int, int>> tam{
        {"MAIN", {w, h}}, {"LUMA", {w, h}}, {"NATIVE", {w, h}}, {"OUTPUT", {2 * w, 2 * h}}};
    for (const auto& p : ps) {
        const std::string g = p.gancho == "LUMA" ? "LUMA" : "MAIN";
        if (p.gancho != "LUMA" && p.gancho != "MAIN" && p.gancho != "NATIVE") continue;
        tam["HOOKED"] = tam[g];
        bool ok = true;
        if (!p.cuando.empty() && evaluar(p.cuando, tam, ok) == 0 && ok) continue;
        int ow = tam[g].first, oh = tam[g].second;
        if (!p.ancho.empty()) ow = static_cast<int>(std::lround(evaluar(p.ancho, tam, ok)));
        if (!p.alto.empty()) oh = static_cast<int>(std::lround(evaluar(p.alto, tam, ok)));
        tam[p.guardar.empty() ? g : p.guardar] = {ow, oh};
    }
    // Cuánto agranda con ESTE tamaño de entrada (x2, x4…: lo que diga el shader).
    const int f = std::max(tam["MAIN"].first, tam["LUMA"].first) / std::max(w, 1);
    return std::clamp(f, 1, 8);
}

static void cargarShader(BaGlsl* s) {
    s->pasadas->clear();
    s->factor = 1;
    std::string texto;
    if (s->codigo != nullptr && *s->codigo != '\0') {
        texto = s->codigo;
    } else {
        if (s->archivo == nullptr || *s->archivo == '\0') return;
        std::ifstream f(s->archivo);
        if (!f) {
            ponerError(s, std::string("no se pudo leer ") + s->archivo);
            return;
        }
        std::stringstream ss;
        ss << f.rdbuf();
        texto = ss.str();
    }
    std::string e;
    if (!leerShader(texto, *s->pasadas, e)) {
        s->pasadas->clear();
        ponerError(s, e);
        return;
    }
    // Una estimación hasta que llegue el tamaño de verdad (las caps): con el
    // lienzo del estudio, si se sabe. Al negociar se calcula con la entrada real.
    s->factor = factorDe(*s->pasadas, 1280, 720);
    ponerError(s, "");
    GST_INFO_OBJECT(s, "%s: %zu pasadas, x%d", s->codigo ? "(código)" : s->archivo, s->pasadas->size(), s->factor);
}

// --- Caps ----------------------------------------------------------------------------

/// El factor con el tamaño de entrada de las caps (si viene fijo): los
/// WHEN/WIDTH/HEIGHT de un shader de mpv dependen del tamaño real.
static int factorCon(BaGlsl* s, GstPadDirection dir, GstCaps* caps) {
    int w = 0, h = 0;
    if (dir == GST_PAD_SINK && caps != nullptr && gst_caps_get_size(caps) > 0) {
        const GstStructure* st = gst_caps_get_structure(caps, 0);
        if (gst_structure_get_int(st, "width", &w) && gst_structure_get_int(st, "height", &h) && w > 0 && h > 0) {
            GST_OBJECT_LOCK(s);
            const int f = s->pasadas->empty() ? 1 : factorDe(*s->pasadas, w, h);
            GST_OBJECT_UNLOCK(s);
            return f;
        }
    }
    return s->factor;
}

static GstCaps* capsInternas(GstGLFilter* f, GstPadDirection dir, GstCaps* caps, GstCaps*) {
    auto* s = BA_GLSL(f);
    const int factor = factorCon(s, dir, caps);
    return ba::sr::transformarCaps(dir, caps, nullptr, s->activo && factor > 1, nullptr, std::max(factor, 2));
}

static GstCaps* fijarCaps(GstBaseTransform* bt, GstPadDirection dir, GstCaps* caps, GstCaps* otras) {
    return ba::sr::fijarCapsPor(dir, caps, otras, std::max(factorCon(BA_GLSL(bt), dir, caps), 1));
}

// --- GL ----------------------------------------------------------------------------------

static GstGLShader* compilar(GstGLContext* ctx, const std::string& fragES, std::string& error) {
    return ba::glsl::compilar(ctx, fragES, error, kVertice);
}

static Tex texturaPropia(BaGlsl* s, const std::string& clave, int w, int h) {
    const GstGLFuncs* gl = GST_GL_BASE_FILTER(s)->context->gl_vtable;
    Tex& t = (*s->propias)[clave];
    if (t.id != 0 && t.w == w && t.h == h) return t;
    if (t.id != 0) gl->DeleteTextures(1, &t.id);
    gl->GenTextures(1, &t.id);
    gl->BindTexture(GL_TEXTURE_2D, t.id);
    gl->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    t.w = w;
    t.h = h;
    return t;
}

static gboolean glArrancar(GstGLBaseFilter* bf) {
    auto* s = BA_GLSL(bf);
    const GstGLFuncs* gl = bf->context->gl_vtable;
    std::string e;
    s->luma = compilar(bf->context, kLuma, e);
    s->salida = compilar(bf->context, kSalida, e);
    if (s->luma == nullptr || s->salida == nullptr) {
        ponerError(s, "no compiló lo propio: " + e);
        return FALSE;
    }
    gl->GenFramebuffers(1, &s->fbo);
    // Un cuadrado que cubre todo: posición (x, y) y coordenada de textura (u, v).
    const GLfloat v[] = {-1, -1, 0, 0, 1, -1, 1, 0, -1, 1, 0, 1, 1, 1, 1, 1};
    if (gl->GenVertexArrays != nullptr) {
        gl->GenVertexArrays(1, &s->vao);
        gl->BindVertexArray(s->vao);
    }
    gl->GenBuffers(1, &s->vbo);
    gl->BindBuffer(GL_ARRAY_BUFFER, s->vbo);
    gl->BufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_STATIC_DRAW);
    gl->BindBuffer(GL_ARRAY_BUFFER, 0);
    if (gl->BindVertexArray != nullptr) gl->BindVertexArray(0);
    return GST_GL_BASE_FILTER_CLASS(ba_glsl_parent_class)->gl_start(bf);
}

static void glParar(GstGLBaseFilter* bf) {
    auto* s = BA_GLSL(bf);
    const GstGLFuncs* gl = bf->context->gl_vtable;
    for (auto& p : *s->pasadas)
        if (p.programa != nullptr) {
            gst_object_unref(p.programa);
            p.programa = nullptr;
        }
    for (auto& [k, t] : *s->propias)
        if (t.id != 0) gl->DeleteTextures(1, &t.id);
    s->propias->clear();
    if (s->luma != nullptr) gst_object_unref(s->luma);
    if (s->salida != nullptr) gst_object_unref(s->salida);
    s->luma = s->salida = nullptr;
    if (s->fbo != 0) gl->DeleteFramebuffers(1, &s->fbo);
    if (s->vbo != 0) gl->DeleteBuffers(1, &s->vbo);
    if (s->vao != 0 && gl->DeleteVertexArrays != nullptr) gl->DeleteVertexArrays(1, &s->vao);
    s->fbo = s->vbo = s->vao = 0;
    GST_GL_BASE_FILTER_CLASS(ba_glsl_parent_class)->gl_stop(bf);
}

/// Dibuja con `sh` en la textura `destino` (w x h), leyendo `entradas` (nombre -> textura).
static void dibujar(BaGlsl* s, GstGLShader* sh, guint destino, int w, int h,
                    const std::vector<std::pair<std::string, Tex>>& entradas, int inW, int inH) {
    const GstGLFuncs* gl = GST_GL_BASE_FILTER(s)->context->gl_vtable;
    gl->BindFramebuffer(GL_FRAMEBUFFER, s->fbo);
    gl->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, destino, 0);
    gl->Viewport(0, 0, w, h);
    gst_gl_shader_use(sh);
    for (size_t i = 0; i < entradas.size(); ++i) {
        const auto& [n, t] = entradas[i];
        gl->ActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(i));
        gl->BindTexture(GL_TEXTURE_2D, t.id);
        gst_gl_shader_set_uniform_1i(sh, (n + "_raw").c_str(), static_cast<gint>(i));
        gst_gl_shader_set_uniform_2f(sh, (n + "_size").c_str(), static_cast<gfloat>(t.w), static_cast<gfloat>(t.h));
        gst_gl_shader_set_uniform_2f(sh, (n + "_pt").c_str(), 1.0f / static_cast<gfloat>(t.w),
                                     1.0f / static_cast<gfloat>(t.h));
    }
    gst_gl_shader_set_uniform_2f(sh, "input_size", static_cast<gfloat>(inW), static_cast<gfloat>(inH));
    gst_gl_shader_set_uniform_2f(sh, "target_size", static_cast<gfloat>(2 * inW), static_cast<gfloat>(2 * inH));
    gst_gl_shader_set_uniform_1i(sh, "frame", static_cast<gint>(s->cuadro));
    gst_gl_shader_set_uniform_1f(sh, "random", static_cast<gfloat>(g_random_double()));
    // Los parámetros del que lo usa (p. ej. la nitidez de FSR), en vivo.
    GST_OBJECT_LOCK(s);
    if (s->uniformes != nullptr)
        for (gint i = 0; i < gst_structure_n_fields(s->uniformes); ++i) {
            const gchar* n = gst_structure_nth_field_name(s->uniformes, i);
            gdouble d = 0;
            if (gst_structure_get_double(s->uniformes, n, &d)) gst_gl_shader_set_uniform_1f(sh, n, static_cast<gfloat>(d));
        }
    GST_OBJECT_UNLOCK(s);
    if (s->vao != 0) gl->BindVertexArray(s->vao);
    gl->BindBuffer(GL_ARRAY_BUFFER, s->vbo);
    const GLint pos = gst_gl_shader_get_attribute_location(sh, "a_position");
    const GLint tc = gst_gl_shader_get_attribute_location(sh, "a_texcoord");
    if (pos >= 0) {
        gl->VertexAttribPointer(static_cast<GLuint>(pos), 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), nullptr);
        gl->EnableVertexAttribArray(static_cast<GLuint>(pos));
    }
    if (tc >= 0) {
        gl->VertexAttribPointer(static_cast<GLuint>(tc), 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat),
                                reinterpret_cast<void*>(2 * sizeof(GLfloat)));
        gl->EnableVertexAttribArray(static_cast<GLuint>(tc));
    }
    gl->DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    if (pos >= 0) gl->DisableVertexAttribArray(static_cast<GLuint>(pos));
    if (tc >= 0) gl->DisableVertexAttribArray(static_cast<GLuint>(tc));
    gl->BindBuffer(GL_ARRAY_BUFFER, 0);
    if (s->vao != 0) gl->BindVertexArray(0);
    for (size_t i = 0; i < entradas.size(); ++i) {
        gl->ActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(i));
        gl->BindTexture(GL_TEXTURE_2D, 0);
    }
    gl->ActiveTexture(GL_TEXTURE0);
    gl->BindFramebuffer(GL_FRAMEBUFFER, 0);
}

static gboolean filtrar(GstGLFilter* f, GstGLMemory* in, GstGLMemory* out) {
    auto* s = BA_GLSL(f);
    GstGLContext* ctx = GST_GL_BASE_FILTER(f)->context;
    const int w = static_cast<int>(gst_gl_memory_get_texture_width(in));
    const int h = static_cast<int>(gst_gl_memory_get_texture_height(in));
    const int ow = static_cast<int>(gst_gl_memory_get_texture_width(out));
    const int oh = static_cast<int>(gst_gl_memory_get_texture_height(out));
    ++s->cuadro;
    std::map<std::string, Tex> tex{{"MAIN", {gst_gl_memory_get_texture_id(in), w, h}}};
    std::map<std::string, std::pair<int, int>> tam{
        {"MAIN", {w, h}}, {"NATIVE", {w, h}}, {"LUMA", {w, h}}, {"OUTPUT", {2 * w, 2 * h}}};
    bool conLuma = false, lumaCambio = false;
    const auto asegurarLuma = [&]() {
        if (conLuma) return;
        const Tex l = texturaPropia(s, "LUMA#0", w, h);
        dibujar(s, s->luma, l.id, w, h, {{"MAIN", tex["MAIN"]}}, w, h);
        tex["LUMA"] = l;
        conLuma = true;
    };
    std::map<std::string, int> ranura; // para no escribir en la textura que se lee
    if (s->activo) {
        for (auto& p : *s->pasadas) {
            if (p.gancho != "LUMA" && p.gancho != "MAIN" && p.gancho != "NATIVE") continue;
            const std::string g = p.gancho == "LUMA" ? "LUMA" : "MAIN";
            tam["HOOKED"] = tam[g];
            bool ok = true;
            if (!p.cuando.empty() && evaluar(p.cuando, tam, ok) == 0 && ok) continue;
            if (g == "LUMA") asegurarLuma();
            for (const auto& n : p.lee)
                if (n == "LUMA") asegurarLuma();
            if (p.programa == nullptr) {
                std::string e;
                p.programa = compilar(ctx, fragmentoDe(p), e);
                if (p.programa == nullptr) {
                    ponerError(s, "la pasada «" + p.desc + "» no compiló: " + e);
                    return FALSE;
                }
            }
            int pw = tam[g].first, ph = tam[g].second;
            if (!p.ancho.empty()) pw = static_cast<int>(std::lround(evaluar(p.ancho, tam, ok)));
            if (!p.alto.empty()) ph = static_cast<int>(std::lround(evaluar(p.alto, tam, ok)));
            if (!ok || pw <= 0 || ph <= 0) {
                ponerError(s, "la pasada «" + p.desc + "» tiene un tamaño que no se entiende");
                return FALSE;
            }
            std::vector<std::pair<std::string, Tex>> entradas{{"HOOKED", tex[g]}};
            for (const auto& n : p.lee) {
                if (n == "HOOKED") continue;
                if (!tex.count(n)) {
                    ponerError(s, "la pasada «" + p.desc + "» lee «" + n + "», que no existe");
                    return FALSE;
                }
                entradas.emplace_back(n, tex[n]);
            }
            const std::string destino = p.guardar.empty() ? g : p.guardar;
            const int r = ranura[destino] ^= 1; // dos texturas por nombre, alternadas
            const Tex t = texturaPropia(s, destino + "#" + std::to_string(r), pw, ph);
            dibujar(s, p.programa, t.id, pw, ph, entradas, w, h);
            tex[destino] = t;
            tam[destino] = {pw, ph};
            if (destino == "LUMA") lumaCambio = true;
        }
    }
    // La salida: MAIN (agrandada por el shader o simple) con la LUMA nueva si la hubo.
    std::vector<std::pair<std::string, Tex>> entradas{{"MAIN", tex["MAIN"]}};
    if (lumaCambio) entradas.emplace_back("LUMA", tex["LUMA"]);
    gst_gl_shader_use(s->salida);
    gst_gl_shader_set_uniform_1f(s->salida, "con_luma", lumaCambio ? 1.0f : 0.0f);
    dibujar(s, s->salida, gst_gl_memory_get_texture_id(out), ow, oh, entradas, w, h);
    return TRUE;
}

// --- Propiedades ---------------------------------------------------------------------------

static void ponerPropiedad(GObject* o, guint id, const GValue* v, GParamSpec* ps) {
    auto* s = BA_GLSL(o);
    switch (id) {
        case PROP_SHADER:
            g_free(s->archivo);
            s->archivo = g_value_dup_string(v);
            cargarShader(s);
            gst_base_transform_reconfigure_src(GST_BASE_TRANSFORM(s));
            break;
        case PROP_CODIGO:
            g_free(s->codigo);
            s->codigo = g_value_dup_string(v);
            cargarShader(s);
            gst_base_transform_reconfigure_src(GST_BASE_TRANSFORM(s));
            break;
        case PROP_UNIFORMES: {
            const GstStructure* u = gst_value_get_structure(v);
            GST_OBJECT_LOCK(s);
            if (s->uniformes != nullptr) gst_structure_free(s->uniformes);
            s->uniformes = u != nullptr ? gst_structure_copy(u) : nullptr;
            GST_OBJECT_UNLOCK(s);
            break;
        }
        case PROP_ACTIVO:
            s->activo = g_value_get_boolean(v);
            gst_base_transform_reconfigure_src(GST_BASE_TRANSFORM(s));
            break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

static void leerPropiedad(GObject* o, guint id, GValue* v, GParamSpec* ps) {
    auto* s = BA_GLSL(o);
    switch (id) {
        case PROP_SHADER: g_value_set_string(v, s->archivo); break;
        case PROP_CODIGO: g_value_set_string(v, s->codigo); break;
        case PROP_UNIFORMES:
            GST_OBJECT_LOCK(s);
            gst_value_set_structure(v, s->uniformes);
            GST_OBJECT_UNLOCK(s);
            break;
        case PROP_ACTIVO: g_value_set_boolean(v, s->activo); break;
        case PROP_FACTOR: g_value_set_int(v, s->factor); break;
        case PROP_PASADAS: g_value_set_int(v, static_cast<gint>(s->pasadas->size())); break;
        case PROP_ERROR:
            GST_OBJECT_LOCK(s);
            g_value_set_string(v, s->error);
            GST_OBJECT_UNLOCK(s);
            break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

static void finalizar(GObject* o) {
    auto* s = BA_GLSL(o);
    g_free(s->archivo);
    g_free(s->codigo);
    if (s->uniformes != nullptr) gst_structure_free(s->uniformes);
    g_free(s->error);
    delete s->pasadas;
    delete s->propias;
    G_OBJECT_CLASS(ba_glsl_parent_class)->finalize(o);
}

static void ba_glsl_class_init(BaGlslClass* k) {
    auto* go = G_OBJECT_CLASS(k);
    auto* ge = GST_ELEMENT_CLASS(k);
    GST_DEBUG_CATEGORY_INIT(ba_glsl_debug, "baglsl", 0, "Shaders de mpv en la GPU");
    go->set_property = ponerPropiedad;
    go->get_property = leerPropiedad;
    go->finalize = finalizar;
    const auto listo = static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY);
    const auto vivo = static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING);
    const auto ro = static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property(go, PROP_SHADER,
                                    g_param_spec_string("shader", "Shader",
                                                        "Un user shader de mpv (.glsl: FSRCNNX, Anime4K…)", nullptr,
                                                        listo));
    g_object_class_install_property(go, PROP_CODIGO,
                                    g_param_spec_string("codigo", "Código",
                                                        "El shader de mpv en el texto (gana sobre `shader`)", nullptr,
                                                        listo));
    g_object_class_install_property(
        go, PROP_UNIFORMES,
        g_param_spec_boxed("uniformes", "Uniformes",
                           "Valores (float) que reciben todas las pasadas: «u, nitidez=(double)0.2»", GST_TYPE_STRUCTURE,
                           vivo));
    g_object_class_install_property(go, PROP_ACTIVO,
                                    g_param_spec_boolean("activo", "Activo", "FALSE: pasa sin tocar", TRUE, vivo));
    g_object_class_install_property(
        go, PROP_FACTOR, g_param_spec_int("factor", "Factor", "Lo que agranda el shader (1 o 2)", 1, 2, 1, ro));
    g_object_class_install_property(go, PROP_PASADAS,
                                    g_param_spec_int("pasadas", "Pasadas", "Cuántas pasadas tiene", 0, 1000, 0, ro));
    g_object_class_install_property(
        go, PROP_ERROR, g_param_spec_string("error", "Error", "Por qué no anda (si no anda)", nullptr, ro));
    gst_element_class_set_static_metadata(ge, "Shader de mpv (GPU)", "Filter/Effect/Video/Scaler",
                                          "Corre un user shader de mpv (FSRCNNX, Anime4K…) en la GPU",
                                          "Broadcast Appliance");
    gst_gl_filter_add_rgba_pad_templates(GST_GL_FILTER_CLASS(k));
    GST_BASE_TRANSFORM_CLASS(k)->fixate_caps = GST_DEBUG_FUNCPTR(fijarCaps);
    GST_GL_BASE_FILTER_CLASS(k)->gl_start = GST_DEBUG_FUNCPTR(glArrancar);
    GST_GL_BASE_FILTER_CLASS(k)->gl_stop = GST_DEBUG_FUNCPTR(glParar);
    GST_GL_BASE_FILTER_CLASS(k)->supported_gl_api =
        static_cast<GstGLAPI>(GST_GL_API_OPENGL | GST_GL_API_OPENGL3 | GST_GL_API_GLES2);
    GST_GL_FILTER_CLASS(k)->filter_texture = GST_DEBUG_FUNCPTR(filtrar);
    GST_GL_FILTER_CLASS(k)->transform_internal_caps = GST_DEBUG_FUNCPTR(capsInternas);
}

static void ba_glsl_init(BaGlsl* s) {
    s->archivo = nullptr;
    s->codigo = nullptr;
    s->uniformes = nullptr;
    s->activo = TRUE;
    s->error = nullptr;
    s->pasadas = new std::vector<Pasada>();
    s->factor = 1;
    s->luma = s->salida = nullptr;
    s->fbo = s->vbo = s->vao = 0;
    s->propias = new std::map<std::string, Tex>();
    s->cuadro = 0;
}

extern "C" gboolean ba_registrar_glsl(GstPlugin* plugin) {
    return gst_element_register(plugin, "baglsl", GST_RANK_NONE, BA_TYPE_GLSL);
}
