// swift-tools-version: 6.2
// ============================================================================
// src/modelo-mac — ba-modelo: el modelo local en la Mac (MLX, con MTP)
//
// Un ayudante aparte, como el resto de los procesos del aparato: lo lanza la
// app y le habla por stdin/stdout (una línea JSON por mensaje). Ver main.swift.
//
// Versiones fijas por commit: mlx-swift-lm trae Qwen3.5 con la cabeza MTP
// (Qwen35MTP.swift) y el iterador especulativo que respeta un LogitProcessor.
// Lo compila `make modelo-mac` (xcodebuild: `swift build` no compila los
// shaders de Metal de MLX).
// ============================================================================
import PackageDescription

let package = Package(
    name: "ba-modelo",
    platforms: [.macOS(.v14)],
    dependencies: [
        // Sin el trait FoundationModelsIntegration: pide el SDK de macOS 27.
        .package(
            url: "https://github.com/ml-explore/mlx-swift-lm",
            revision: "ac6f5651a423b6ad9005332eed583dd336e59a53",
            traits: []),
        .package(url: "https://github.com/huggingface/swift-transformers", from: "1.3.0"),
    ],
    targets: [
        .executableTarget(
            name: "ba-modelo",
            dependencies: [
                .product(name: "MLXLLM", package: "mlx-swift-lm"),
                .product(name: "MLXLMCommon", package: "mlx-swift-lm"),
                .product(name: "Tokenizers", package: "swift-transformers"),
            ],
            path: "Sources/ba-modelo",
            swiftSettings: [.swiftLanguageMode(.v5)]),
    ]
)
