# Third-party components

Version 0.6.0 adds local PDF resume text extraction using PDFium **chromium/8111 (158.0.8111.0)**, BSD-style license, from [PDFium](https://pdfium.googlesource.com/pdfium/) and the fixed [pdfium-binaries Windows x64 release](https://github.com/bblanchon/pdfium-binaries/releases/tag/chromium/8111). The non-V8/non-XFA build is dynamically loaded only by ResumeReader.exe. Its original LICENSE and all bundled third-party licenses ship in resume/. Original public fpdfview.h/fpdf_text.h headers are vendored unchanged under src/third_party/pdfium/. scripts/prepare-resume.ps1 pins the release archive SHA-256; binaries are not committed.

DOCX text extraction uses Qt 6.11.2 CorePrivate QZipReader plus QXmlStreamReader. Builds require the matching Qt private development headers; runtime uses the same deployed QtCore library. No Office or Python installation is required to read resumes in the distributed app.

Version 0.5.0 local ASR uses sherpa-onnx **v1.13.8** (Apache-2.0), ONNX Runtime **1.28.2** (MIT), and **sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20** (Apache-2.0, as declared by its upstream model README). The package includes the int8 encoder/joiner and fp32 decoder, plus the original model README. It does not include Vosk or OpenBLAS from older releases.

Sources and documentation:

- [sherpa-onnx source and C API](https://github.com/k2-fsa/sherpa-onnx/tree/v1.13.8)
- [Official Windows runtime release](https://github.com/k2-fsa/sherpa-onnx/releases/tag/v1.13.8)
- [Bilingual model documentation](https://k2-fsa.github.io/sherpa/onnx/pretrained_models/online-transducer/zipformer-transducer-models.html#csukuangfj-sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20-bilingual-chinese-english)
- [ONNX Runtime source](https://github.com/microsoft/onnxruntime/tree/v1.28.2)

The original C API header is vendored unchanged at src/third_party/sherpa-onnx/c-api.h. The package carries SHERPA-ONNX-LICENSE.txt, ONNX-RUNTIME-LICENSE.txt and ONNX-THIRD-PARTY-NOTICES.txt in local-asr/, with model attribution in local-asr/bilingual/README.md. scripts/prepare-local-asr.ps1 pins archive SHA-256 checksums. Runtime/model binaries are downloaded from upstream, not checked into this repository.

Qt and the MinGW runtime libraries are deployed from the installed Qt toolchain and retain their respective licenses and runtime exceptions. English terms, accents and noisy meeting audio still require validation with the user's equipment.
