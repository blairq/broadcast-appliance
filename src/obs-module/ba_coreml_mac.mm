// src/obs-module/ba_coreml_mac.mm — ver ba_coreml_mac.hpp

#include "ba_coreml_mac.hpp"

#include <obs-module.h>

#import <CoreML/CoreML.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <IOSurface/IOSurface.h>
#import <OpenGL/CGLIOSurface.h>
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl3.h>

#include <atomic>
#include <chrono>
#include <mutex>

#ifndef GL_TEXTURE_RECTANGLE
#define GL_TEXTURE_RECTANGLE 0x84F5
#endif

namespace ba::coreml {

namespace {
constexpr int kJuegos = 2; ///< dos juegos de IOSurfaces: uno lo llena el dibujo, otro lo procesa la red

double ahoraMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

/// Una IOSurface BGRA, como CVPixelBuffer (lo que acepta CoreML).
CVPixelBufferRef crearBuffer(int w, int h) {
    NSDictionary* attrs = @{
        (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
        (id)kCVPixelBufferWidthKey: @(w),
        (id)kCVPixelBufferHeightKey: @(h),
        (id)kCVPixelBufferIOSurfacePropertiesKey: @{},
        (id)kCVPixelBufferMetalCompatibilityKey: @YES,
        (id)kCVPixelBufferOpenGLCompatibilityKey: @YES,
    };
    CVPixelBufferRef pb = nullptr;
    CVPixelBufferCreate(kCFAllocatorDefault, w, h, kCVPixelFormatType_32BGRA, (__bridge CFDictionaryRef)attrs, &pb);
    return pb;
}
} // namespace

struct TarjetaCoreMl::Impl {
    MLModel* red = nil;
    dispatch_queue_t cola = nil; ///< donde corre la red (serie)
    std::atomic<bool> cargando{false};
    std::atomic<bool> fallo{false};
    mutable std::mutex mu;
    std::string error;
    int inW = 0, inH = 0, outW = 0, outH = 0;
    // Entrada: IOSurface + textura rectangular de OpenGL + framebuffer para el blit.
    CVPixelBufferRef entrada[kJuegos] = {};
    GLuint texEntrada[kJuegos] = {}, fboEntrada[kJuegos] = {};
    GLuint fboLeer = 0; ///< para leer la textura de OBS en el blit
    // Salida: IOSurface (la escribe CoreML) + su textura de OBS.
    CVPixelBufferRef salida[kJuegos] = {};
    gs_texture_t* texSalida[kJuegos] = {};
    std::atomic<int> ocupado[kJuegos];     ///< 1 mientras la red usa ese juego
    std::atomic<int> listo{-1};            ///< el último juego con resultado
    std::atomic<double> demora{0}, msRed{0};
    double enviado[kJuegos] = {};
    int siguiente = 0;

    Impl() {
        for (auto& o : ocupado) o.store(0);
        cola = dispatch_queue_create("ba.coreml.superres", DISPATCH_QUEUE_SERIAL);
    }
    void ponerError(const std::string& e) {
        std::lock_guard<std::mutex> l(mu);
        error = e;
        if (!e.empty()) blog(LOG_WARNING, "[ba-coreml] %s", e.c_str());
    }
    void soltarSuperficies() {
        // Que la red no esté usando nada.
        dispatch_sync(cola, ^{
                      });
        for (int k = 0; k < kJuegos; ++k) {
            if (fboEntrada[k] != 0) glDeleteFramebuffers(1, &fboEntrada[k]), fboEntrada[k] = 0;
            if (texEntrada[k] != 0) glDeleteTextures(1, &texEntrada[k]), texEntrada[k] = 0;
            if (entrada[k] != nullptr) CVPixelBufferRelease(entrada[k]), entrada[k] = nullptr;
            if (texSalida[k] != nullptr) gs_texture_destroy(texSalida[k]), texSalida[k] = nullptr;
            if (salida[k] != nullptr) CVPixelBufferRelease(salida[k]), salida[k] = nullptr;
            ocupado[k].store(0);
        }
        listo.store(-1);
        inW = inH = outW = outH = 0;
    }
    bool armarSuperficies(int iw, int ih, int ow, int oh) {
        soltarSuperficies();
        CGLContextObj cgl = CGLGetCurrentContext();
        if (cgl == nullptr) {
            ponerError("sin contexto de OpenGL (la tarjeta CoreML necesita el OBS de OpenGL)");
            return false;
        }
        for (int k = 0; k < kJuegos; ++k) {
            entrada[k] = crearBuffer(iw, ih);
            salida[k] = crearBuffer(ow, oh);
            if (entrada[k] == nullptr || salida[k] == nullptr) {
                ponerError("no se pudieron crear las IOSurfaces");
                return false;
            }
            glGenTextures(1, &texEntrada[k]);
            glBindTexture(GL_TEXTURE_RECTANGLE, texEntrada[k]);
            const CGLError e = CGLTexImageIOSurface2D(cgl, GL_TEXTURE_RECTANGLE, GL_RGBA, iw, ih, GL_BGRA,
                                                      GL_UNSIGNED_INT_8_8_8_8_REV, CVPixelBufferGetIOSurface(entrada[k]), 0);
            glBindTexture(GL_TEXTURE_RECTANGLE, 0);
            if (e != kCGLNoError) {
                ponerError(std::string("CGLTexImageIOSurface2D: ") + CGLErrorString(e));
                return false;
            }
            glGenFramebuffers(1, &fboEntrada[k]);
            GLint antes = 0;
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &antes);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fboEntrada[k]);
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_RECTANGLE, texEntrada[k], 0);
            const GLenum st = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(antes));
            if (st != GL_FRAMEBUFFER_COMPLETE) {
                ponerError("el framebuffer de la IOSurface no está completo");
                return false;
            }
            texSalida[k] = gs_texture_create_from_iosurface(CVPixelBufferGetIOSurface(salida[k]));
            if (texSalida[k] == nullptr) {
                ponerError("no se pudo envolver la IOSurface de salida como textura");
                return false;
            }
        }
        if (fboLeer == 0) glGenFramebuffers(1, &fboLeer);
        inW = iw, inH = ih, outW = ow, outH = oh;
        return true;
    }
};

TarjetaCoreMl::TarjetaCoreMl(std::string modelo, int escalaRed)
    : d_(std::make_unique<Impl>()), modelo_(std::move(modelo)), escalaRed_(escalaRed == 4 ? 4 : 2) {
    // El modelo se carga en otro hilo (la primera vez el Neural Engine lo prepara).
    d_->cargando.store(true);
    Impl* d = d_.get();
    NSString* ruta = [NSString stringWithUTF8String:modelo_.c_str()];
    dispatch_async(d->cola, ^{
      @autoreleasepool {
          MLModelConfiguration* conf = [[MLModelConfiguration alloc] init];
          conf.computeUnits = MLComputeUnitsAll;
          NSError* err = nil;
          MLModel* m = [MLModel modelWithContentsOfURL:[NSURL fileURLWithPath:ruta] configuration:conf error:&err];
          if (m == nil) {
              d->ponerError(std::string("no se pudo cargar el modelo CoreML: ") +
                            (err ? err.localizedDescription.UTF8String : ruta.UTF8String));
              d->fallo.store(true);
          } else {
              d->red = m;
          }
          d->cargando.store(false);
      }
    });
}

TarjetaCoreMl::~TarjetaCoreMl() {
    // Se destruye en el hilo de gráficos (el plan del grafo): ahí se sueltan las texturas.
    d_->soltarSuperficies();
    if (d_->fboLeer != 0) glDeleteFramebuffers(1, &d_->fboLeer);
}

double TarjetaCoreMl::demoraMs() const { return d_->demora.load(); }
double TarjetaCoreMl::msRed() const { return d_->msRed.load(); }
std::string TarjetaCoreMl::error() const {
    std::lock_guard<std::mutex> l(d_->mu);
    return d_->error;
}

bool TarjetaCoreMl::procesar(gs_texture_t* entrada, std::uint32_t cx, std::uint32_t cy) {
    Impl& d = *d_;
    if (entrada == nullptr || cx == 0 || cy == 0 || d.fallo.load() || d.cargando.load() || d.red == nil) return false;
    const int iw = static_cast<int>(escalaRed_ == 4 ? cx / 2 : cx), ih = static_cast<int>(escalaRed_ == 4 ? cy / 2 : cy);
    const int ow = static_cast<int>(cx * 2), oh = static_cast<int>(cy * 2);
    if (d.inW != iw || d.inH != ih || d.outW != ow || d.outH != oh) {
        if (!d.armarSuperficies(iw, ih, ow, oh)) {
            d.fallo.store(true);
            return false;
        }
    }

    // 1) La entrada a un juego libre: blit de la textura de OBS a la IOSurface (GPU).
    int k = -1;
    for (int i = 0; i < kJuegos; ++i) {
        const int j = (d.siguiente + i) % kJuegos;
        if (d.ocupado[j].load() == 0 && j != d.listo.load()) {
            k = j;
            break;
        }
    }
    if (k >= 0) {
        const GLuint* tex = static_cast<const GLuint*>(gs_texture_get_obj(entrada));
        GLint antesLeer = 0, antesDibujar = 0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &antesLeer);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &antesDibujar);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, d.fboLeer);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex != nullptr ? *tex : 0, 0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, d.fboEntrada[k]);
        // Las texturas de OBS van con el origen abajo; la IOSurface, arriba: se da vuelta.
        glBlitFramebuffer(0, 0, static_cast<GLint>(cx), static_cast<GLint>(cy), 0, ih, iw, 0, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(antesLeer));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(antesDibujar));
        glFlush(); // que la IOSurface quede escrita antes de que la lea CoreML
        d.ocupado[k].store(1);
        d.enviado[k] = ahoraMs();
        d.siguiente = (k + 1) % kJuegos;
        Impl* dp = &d;
        CVPixelBufferRef en = d.entrada[k];
        CVPixelBufferRef sa = d.salida[k];
        MLModel* red = d.red;
        const int juego = k;
        dispatch_async(d.cola, ^{
          @autoreleasepool {
              const double t0 = ahoraMs();
              NSError* err = nil;
              MLFeatureValue* fv = [MLFeatureValue featureValueWithPixelBuffer:en];
              MLDictionaryFeatureProvider* prov = [[MLDictionaryFeatureProvider alloc] initWithDictionary:@{@"entrada": fv}
                                                                                                  error:&err];
              MLPredictionOptions* op = [[MLPredictionOptions alloc] init];
              op.outputBackings = @{@"salida": (__bridge id)sa}; // CoreML escribe directo en la IOSurface de salida
              id<MLFeatureProvider> res = prov ? [red predictionFromFeatures:prov options:op error:&err] : nil;
              if (res == nil) {
                  dp->ponerError(std::string("CoreML: ") + (err ? err.localizedDescription.UTF8String : "sin resultado"));
              } else {
                  const double t1 = ahoraMs();
                  const double r = dp->msRed.load(), de = dp->demora.load();
                  dp->msRed.store(r == 0 ? t1 - t0 : r * 0.9 + (t1 - t0) * 0.1);
                  const double dem = t1 - dp->enviado[juego];
                  dp->demora.store(de == 0 ? dem : de * 0.9 + dem * 0.1);
                  dp->listo.store(juego);
              }
              dp->ocupado[juego].store(0);
          }
        });
    }

    // 2) Se dibuja el último resultado listo (sin esperar).
    const int l = d.listo.load();
    if (l < 0 || d.texSalida[l] == nullptr) return false;
    gs_texture_rebind_iosurface(d.texSalida[l], CVPixelBufferGetIOSurface(d.salida[l]));
    gs_effect_t* fx = obs_get_base_effect(OBS_EFFECT_DEFAULT_RECT);
    gs_eparam_t* img = gs_effect_get_param_by_name(fx, "image");
    gs_effect_set_texture(img, d.texSalida[l]);
    gs_blend_state_push();
    gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
    while (gs_effect_loop(fx, "Draw"))
        gs_draw_sprite(d.texSalida[l], 0, static_cast<std::uint32_t>(ow), static_cast<std::uint32_t>(oh));
    gs_blend_state_pop();
    return true;
}

} // namespace ba::coreml
