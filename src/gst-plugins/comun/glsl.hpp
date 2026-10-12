// ============================================================================
// src/gst-plugins/comun/glsl.hpp — compilar shaders escritos al estilo ES2
//
// Los shaders propios (baglsl, bacolorgl, barecortegl) se escriben como GLSL ES
// 1.00 (texture2D, gl_FragColor, varying). Acá se llevan a la versión de GLSL
// del contexto: GL 2 / ES 2 tal cual; GL 3 core (la Mac) o ES 3, con las
// palabras de GLSL 1.30+. Todo corre en el hilo de GL del contexto.
// ============================================================================
#pragma once

#include <gst/gl/gl.h>

#include <cctype>
#include <string>

#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#endif

namespace ba::glsl {

/// El vértice de siempre: un cuadrado con su coordenada de textura.
inline const char* kVertice = "attribute vec4 a_position;\n"
                              "attribute vec2 a_texcoord;\n"
                              "varying vec2 v_texcoord;\n"
                              "void main() { gl_Position = a_position; v_texcoord = a_texcoord; }\n";

/// La versión de GLSL del contexto (la más alta que tiene).
inline void version(GstGLContext* ctx, GstGLSLVersion& ver, GstGLSLProfile& prof) {
    const GstGLAPI api = gst_gl_context_get_gl_api(ctx);
    gint mayor = 0, menor = 0;
    gst_gl_context_get_gl_version(ctx, &mayor, &menor);
    ver = gst_gl_version_to_glsl_version(api, mayor, menor);
    prof = (api & GST_GL_API_GLES2) != 0     ? GST_GLSL_PROFILE_ES
           : (api & GST_GL_API_OPENGL3) != 0 ? GST_GLSL_PROFILE_CORE
                                             : GST_GLSL_PROFILE_COMPATIBILITY;
    if (ver == GST_GLSL_VERSION_NONE) {
        ver = GST_GLSL_VERSION_100;
        prof = GST_GLSL_PROFILE_ES;
    }
}

/// Reemplaza todas las apariciones de una palabra entera.
inline std::string cambiarPalabra(std::string s, const std::string& de, const std::string& a) {
    const auto esId = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    for (size_t i = 0; (i = s.find(de, i)) != std::string::npos;) {
        const bool antes = i > 0 && esId(s[i - 1]);
        const bool despues = i + de.size() < s.size() && esId(s[i + de.size()]);
        if (antes || despues) {
            i += de.size();
            continue;
        }
        s.replace(i, de.size(), a);
        i += a.size();
    }
    return s;
}

/// Lo escrito al estilo ES2 / GLSL 1.10, llevado a GLSL 1.30+ (core).
inline std::string aModerno(const std::string& src, bool fragmento) {
    std::string s = cambiarPalabra(src, "texture2D", "texture");
    if (fragmento) {
        s = cambiarPalabra(s, "varying", "in");
        s = cambiarPalabra(s, "gl_FragColor", "ba_color");
        s = "out vec4 ba_color;\n" + s;
    } else {
        s = cambiarPalabra(s, "attribute", "in");
        s = cambiarPalabra(s, "varying", "out");
    }
    return s;
}

/// Compila vértice + fragmento (estilo ES2) para el contexto. nullptr y `error` si no.
inline GstGLShader* compilar(GstGLContext* ctx, const std::string& fragES, std::string& error,
                             const std::string& vertES = kVertice) {
    GError* err = nullptr;
    GstGLSLVersion ver;
    GstGLSLProfile prof;
    version(ctx, ver, prof);
    const bool moderno = prof == GST_GLSL_PROFILE_CORE || (prof != GST_GLSL_PROFILE_ES && ver >= GST_GLSL_VERSION_130) ||
                         (prof == GST_GLSL_PROFILE_ES && ver >= GST_GLSL_VERSION_300);
    gchar* vs = gst_glsl_version_profile_to_string(ver, prof);
    const std::string cabecera = std::string("#version ") + (vs != nullptr ? vs : "100") + "\n";
    g_free(vs);
    const std::string vert = cabecera + (moderno ? aModerno(vertES, false) : vertES);
    const std::string frag = cabecera + (moderno ? aModerno(fragES, true) : fragES);
    GstGLSLStage* v = gst_glsl_stage_new_with_string(ctx, GL_VERTEX_SHADER, ver, prof, vert.c_str());
    GstGLSLStage* fr = gst_glsl_stage_new_with_string(ctx, GL_FRAGMENT_SHADER, ver, prof, frag.c_str());
    GstGLShader* sh = gst_gl_shader_new_link_with_stages(ctx, &err, v, fr, nullptr);
    if (sh == nullptr) {
        error = err != nullptr ? err->message : "no compiló";
        if (err != nullptr) g_error_free(err);
    }
    return sh;
}

} // namespace ba::glsl
