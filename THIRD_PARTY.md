# Third-party components

Version 0.5.0 local ASR uses sherpa-onnx **v1.13.8** (Apache-2.0), ONNX Runtime **1.28.2** (MIT), and **sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20** (Apache-2.0, as declared by its upstream model README). The package includes the int8 encoder/joiner and fp32 decoder, plus the original model README. It does not include Vosk or OpenBLAS from older releases.

Sources and documentation:

- [sherpa-onnx source and C API](https://github.com/k2-fsa/sherpa-onnx/tree/v1.13.8)
- [Official Windows runtime release](https://github.com/k2-fsa/sherpa-onnx/releases/tag/v1.13.8)
- [Bilingual model documentation](https://k2-fsa.github.io/sherpa/onnx/pretrained_models/online-transducer/zipformer-transducer-models.html#csukuangfj-sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20-bilingual-chinese-english)
- [ONNX Runtime source](https://github.com/microsoft/onnxruntime/tree/v1.28.2)

The original C API header is vendored unchanged at src/third_party/sherpa-onnx/c-api.h. The package carries SHERPA-ONNX-LICENSE.txt, ONNX-RUNTIME-LICENSE.txt and ONNX-THIRD-PARTY-NOTICES.txt in local-asr/, with model attribution in local-asr/bilingual/README.md. scripts/prepare-local-asr.ps1 pins archive SHA-256 checksums. Runtime/model binaries are downloaded from upstream, not checked into this repository.

Qt and the MinGW runtime libraries are deployed from the installed Qt toolchain and retain their respective licenses and runtime exceptions. English terms, accents and noisy meeting audio still require validation with the user's equipment.
