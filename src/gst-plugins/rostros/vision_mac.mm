// ============================================================================
// src/gst-plugins/rostros/vision_mac.mm — ver vision.hpp
// ============================================================================
#include "vision.hpp"

#import <CoreVideo/CoreVideo.h>
#import <Vision/Vision.h>

namespace ba::rostros {

bool hayVision() { return true; }

bool detectarConVision(const std::uint8_t* bgra, int ancho, int alto, std::vector<CajaVision>& caras) {
    caras.clear();
    if (bgra == nullptr || ancho <= 0 || alto <= 0) return false;
    @autoreleasepool {
        CVPixelBufferRef pb = nullptr;
        // Sin copiar: Vision lee los píxeles que ya tenemos.
        if (CVPixelBufferCreateWithBytes(kCFAllocatorDefault, ancho, alto, kCVPixelFormatType_32BGRA,
                                         const_cast<std::uint8_t*>(bgra), static_cast<size_t>(ancho) * 4, nullptr,
                                         nullptr, nullptr, &pb) != kCVReturnSuccess)
            return false;
        VNDetectFaceRectanglesRequest* pedido = [[VNDetectFaceRectanglesRequest alloc] init];
        VNImageRequestHandler* h = [[VNImageRequestHandler alloc] initWithCVPixelBuffer:pb options:@{}];
        NSError* error = nil;
        const BOOL ok = [h performRequests:@[ pedido ] error:&error];
        if (ok) {
            for (VNFaceObservation* o in pedido.results) {
                const CGRect r = o.boundingBox; // fracciones, origen ABAJO a la izquierda
                caras.push_back({static_cast<float>(r.origin.x),
                                 static_cast<float>(1.0 - r.origin.y - r.size.height),
                                 static_cast<float>(r.size.width), static_cast<float>(r.size.height),
                                 static_cast<float>(o.confidence)});
            }
        }
        CVPixelBufferRelease(pb);
        return ok;
    }
}

} // namespace ba::rostros
