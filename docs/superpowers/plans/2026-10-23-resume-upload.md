# Resume Upload Implementation Plan

> For agentic workers: use superpowers:executing-plans for inline execution, then one independent whole-change review.

**Goal:** 上传简历替代经历输入，AI 分析公司与岗位结合简历。

**Architecture:** 独立本机 ResumeReader 提取 PDF/DOCX/TXT，复用 ResumeUploadWidget 展示导入状态。准备数据保存简历文本，并进入分析请求及会话背景快照。

**Tech Stack:** C++17、Qt Widgets/CorePrivate 6.11.2、PDFium chromium/8111 Windows x64。

**Spec:** docs/superpowers/specs/2026-10-23-resume-upload-design.md

## Global Constraints

- Windows / C++ Qt Widgets；沿用 OpenAI、ChatGPT、DeepSeek 配置。
- 文件最大 20 MB，文字最多 20000 字，后台解析超时 30 秒。
- 不读取用户实际简历或账号，不操作正在运行的旧程序。
- 保留旧经历、草稿、会话快照和简短口述回答规则。

## Review Focus

- 损坏/扫描/加密文件：明确失败、保留旧简历，不把空文本当成功。
- 导入未结束或录音中：阻止保存/分析/开始录音读取半成品。
- 更换简历：旧分析及术语失效，旧会话快照不受影响。
- 文档表格、中文路径和编码：提取真实正文，避免乱码与漏掉段落。
- 打包完整性：独立解析程序、PDFium 与许可证随包部署，离线可用。

### Task 1: 简历进入分析和会话背景

Files: src/preparation/Preparation.*, src/ai/InterviewLogic.cpp, tests/tst_preparation.cpp, tests/tst_window.cpp.

- [x] 新增失败测试：20000 字简历完整进入三种服务分析请求；修改简历使指纹失效；旧经历保持兼容。
- [x] 增加 Preparation::candidateText(const QJsonObject &)，更新 fingerprint/request，分析明确结合简历匹配与差距。
- [x] 执行 preparation 回归，记录红绿结果。

### Task 2: 本地文件读取与异步上传组件

Files: src/resume/ResumeDocument.*, src/resume/reader_main.cpp, src/ui/ResumeUploadWidget.*, scripts/prepare-resume.ps1, CMakeLists.txt, tests/tst_resume.cpp.

- [x] 新增解析及组件失败测试，覆盖中文 TXT、DOCX 表格、真实生成 PDF、多页、扫描空白、损坏、过长文件、导入失败保留旧值。
- [x] 实现 ResumeDocument::extract(QString) 返回 {fileName,text,sha256} 或 {error}；独立进程输出 JSON。
- [x] 实现组件的异步导入、超时、预览、移除、只读；不阻塞 UI。
- [x] 固定下载 PDFium 并校验 SHA-256，执行 resume 回归。

### Task 3: 两处 UI 接入、部署和交付

Files: src/ui/MainWindow.*, src/ui/PreparationDialog.*, scripts/build.ps1, scripts/package.ps1, README.md, THIRD_PARTY.md, tests/tst_window.cpp, tests/tst_preparation.cpp.

- [x] 增加 UI 集成测试：导入并保存、分析请求使用简历、移除/替换导致失效、开始面试复制背景、录音快照只读。
- [x] 替换两处经历编辑框；MainWindow 自动保存上传的草稿，PreparationDialog 保存/分析时保存。
- [x] 版本升级 0.6.0；完整构建和测试、隔离运行包解析与窗口验证；独立代码审查。
- [x] 打包新目录、不覆盖旧运行程序；提交并按既有授权推送到 GitHub main。
