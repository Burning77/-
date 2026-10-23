# 0.5.0 中英混合识别验证（2026-10-23）

## 变更与复现

0.4.0 默认使用 Vosk small-cn 中文专用模型。公开英文短片段在旧引擎下输出中文，复现英文被近音汉字替代的问题。0.5.0 改用 sherpa-onnx 1.13.8、ONNX Runtime 1.28.2 和官方 streaming Zipformer bilingual zh-en 2023-02-20 模型；CPU 本地识别、双路独立 stream、共享模型与串行解码保护。

默认模型使用独立 bilingualModel 设置键，升级不沿用 Vosk 中文目录。识别结果不经过翻译；术语表禁止英文别名替换为中文译名，英文拼写别名仍可纠正且保存 rawText。面试准备生成术语时要求英文 term 与中文 meaning 分开。

## 验证结果

- Release 完整构建成功。
- CTest 16/16 测试组通过；QtTest 共 **134 passed / 0 failed / 0 skipped**。
- ASR 13 项检查覆盖：模型真实加载、30 秒队列限制、真实中文/英文/四段中英混说、双路并发、无尾部静音时暂停 flush、续录产生新分段、取消返回小于 200 ms 且不再送出迟到结果。
- 混说实录正确输出 MONDAY、AFTER TOMORROW、ALWAYS、FREQUENTLY、ES；纯英文句子保留英文。Windows 合成技术句中的 DIRECT MEMORY ACCESS 和 MUTEX 识别为英文。
- 专业词准确率边界：合成句中的 semaphore 原始识别为 SEMOPHOR。测试分别检查语言保留与已确认词汇，不把“所有词拼写完全正确”当成已达成能力。准备术语显式添加 semophor → semaphore 别名后，纠正测试通过并保留原始文本。一个不足一秒的英文片段未得到结果，不能据此承诺短词准确率。
- 独立代码审查未发现 Critical / Important 产品问题；指出的无依赖测试路径问题已改为明确跳过。
- 新增双路测试中发现并修复了测试自身保存临时 QJsonObject 的 QJsonValueRef 所致悬空引用；保存为 QString 后真实双路、暂停和续录测试通过。

## 随包依赖验证

将 PATH 限制为运行包与 Windows 系统目录，使用运行包自己的 Qt、ONNX DLL 和双语模型，外置测试辅助程序另带 Qt6Test 与 qoffscreen 插件：

- packaged-bilingual-asr.txt：13 passed / 0 failed / 0 skipped。
- packaged-bilingual-window.txt：5 passed / 0 failed / 0 skipped（空闲启动、面试准备快照、语音设置入口）。
- ZIP 50 个文件条目全部读取解压通过；未包含账号凭据、会话数据库、录音或测试音频。
- 官方 C API 头与下载原文件一致。运行包包含三个识别 DLL、四个模型/词表文件、原始模型 README、Sherpa/ONNX 许可和 ONNX 第三方声明。

这些是公开/合成音频与离屏组件验证，没有操作用户正在运行的软件，没有验证真实麦克风、会议平台、口音、长时间负载或实际端到端延迟。离屏字体/窗口插件警告不代表原生 Windows 界面验证。

## 交付物

- dist/InterviewAssistant-0.5.0/InterviewAssistant.exe
- dist/InterviewAssistant-0.5.0-win64.zip（222633547 bytes）
- EXE SHA-256：CED433CFCF69805ED2CCFB982833CA24630A6679E18E5766472F8B78CFCA0B6B（与已验证构建一致）
- ZIP SHA-256：D5FA49DDEC7EC9EF752121CC2996C9D841AAFC1CFCE1B40EF06759089D99A8C3

复现：先运行 scripts/prepare-local-asr.ps1、scripts/prepare-asr-test-data.ps1，构建 Release 后运行 scripts/test.ps1 -Configuration Release。测试数据脚本需要 Windows 英文语音。模型来源与许可见 THIRD_PARTY.md；原始详细输出位于 build-release/ 与 ASCII 构建镜像中，不纳入源码提交。
