# Third-party components

The local speech recognizer uses Vosk API v0.3.45 and vosk-model-small-cn-0.22, distributed under Apache License 2.0. The runtime package includes the upstream license in local-asr/VOSK-LICENSE.txt and the original model README in local-asr/model/README.

- Vosk source and third-party dependencies: https://github.com/alphacep/vosk-api/tree/v0.3.45
- Vosk model source, license and evaluation: https://alphacephei.com/vosk/models
- Apache License 2.0: https://www.apache.org/licenses/LICENSE-2.0

Vosk links Kaldi, OpenFst and OpenBLAS; consult their upstream distributions for the applicable notices. Qt and the MinGW runtime libraries are deployed from the installed Qt toolchain and retain their respective licenses and runtime exceptions.

The Chinese small model is optimized for modest CPU/memory use. Recognition of technical terms, accents and noisy meeting audio requires validation with the user's equipment.
