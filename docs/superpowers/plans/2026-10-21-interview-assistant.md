# Qt 面试辅助工具 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现 Windows C++ / Qt Widgets 软件，支持线上面试双路录音、实时转写、GPT / DeepSeek 可选参考回答、历史记录和事后复盘。

**Architecture:** Qt Widgets 主窗口连接一个会话控制器；录音在工作线程运行，网络以异步信号返回，所有业务记录持久化到 SQLite。文本服务通过独立适配器归一化，语音转写单独连接；数据、请求及音频始终关联稳定会话 ID。

**Tech Stack:** C++17、Qt 6.11.2 / Widgets / Multimedia / Network / SQL / Test、Qt WebSockets、Windows WASAPI 与 Credential Manager、CMake、Ninja、Qt 配套 MinGW。

**Spec:** `docs/superpowers/specs/2026-10-21-interview-assistant-design.md`

## 执行记录（2026-10-21）

原计划获用户确认后，已在当前对话实现。以下完成记录优先于后文保留的原始步骤及拟定接口；具体测试与限制见 `docs/acceptance.md`。

| 任务 | 结果 |
| --- | --- |
| 1 界面与构建 | 完成，中文 Qt Widgets 主窗口，英文构建目录副本解决 moc 路径问题 |
| 2 存储与配置 | 完成，SQLite / 稳定 ID / 修订，Windows 凭据管理器 |
| 3 文本流接口 | 完成，OpenAI Responses 与 DeepSeek Chat Completions 独立配置，SSE 与本机 HTTP 测试通过 |
| 4 问题与回答 | 完成，手动及自动提取、来源验证、流式回答、取消及请求归属 |
| 5 双路音频 | 完成，WASAPI / QAudioSource / PCM / WAV；合成源及保存失败测试通过，实物设备待联调 |
| 6 实时转写 | 完成，Windows WinHTTP 替代 Qt WebSockets；协议组装、缓存、缺口与本机双向 WebSocket 测试通过，云端待联调 |
| 7 会话编排 | 完成，历史、编辑、暂停、结束排空、结束后的待提取发言及草稿隔离 |
| 8 复盘与导出 | 完成，按真实对话分段，保留超长发言、来源版本和快照，校验全部引用；不另生成全局汇总 |
| 9 本地交付 | 完成 Debug / Release 构建、10/10 测试套件、运行包和脱离 Qt 开发环境的启动检查；实际设备及云端验收单列 |

独立审查的 7 项重要问题已修复并完成失败到通过的回归验证。用户随后授权将源码更新到 `https://github.com/Burning77/-.git`，因此本地 Git 初始化、提交和向该远端推送均在授权范围内。

业务实体最终采用验证过的 JSON / SQLite 记录，控制器与界面按实际职责合并部分原拟文件。下文复选框是最初的实施清单，不代表另有一套尚未执行的实现。

## Global Constraints

- 用户明确选择 C++ / Qt Widgets；主界面使用中文。
- Windows 播放音频使用 WASAPI loopback，麦克风使用 QAudioSource。
- 第一版以中文、单个应聘者和一条远端音频来源为目标。
- 语音转写独立配置，选择 DeepSeek 回答不改变现有转写连接。
- API Key 保存到 Windows 凭据管理器；源码、SQLite、导出和日志不保存明文 Key。
- AI 参考答案单独标记，禁止作为用户已经回答的内容。
- API 地址和模型可配置，但不承诺任意兼容接口都支持相同协议。
- 无 API Key 时不能声称真实转写或文本模型联调成功。
- 用户已授权初始化本地 Git 并推送指定 GitHub 仓库；实施进度以上方执行记录和验收文档为准。

## Review Focus

- 中文 UTF-8 与 SSE 在任意字节处断开：正文完整、不乱码；任务 3 用逐字节输入测试。
- 生成中切换服务或当前问题：旧回调写回原问题和原配置；任务 4 测试交错回调。
- 录音暂停、结束与迟到转写竞争：区间明确、结束前有界排空、无跨会话写入；任务 7 测试迟到事件。
- 设备拔出或无播放声音：通道独立报错、正常通道可继续、静音时间轴持续；任务 5 用音频源替身测试。
- 复盘引用不足或输出截断：提示证据不足或生成未完成，保留可重试状态；任务 8 用缺失片段和截断响应测试。

## 文件布局与共享接口

`src/app/` 负责启动、配置和会话装配；`src/ui/` 负责 Widgets 界面；`src/domain/` 定义值类型；`src/storage/` 负责数据库和文件；`src/audio/` 负责采集和 WAV；`src/transcription/` 负责语音接口；`src/ai/` 负责文本接口和业务生成；`tests/` 使用 Qt Test；`scripts/` 保存构建和打包脚本。

共享值类型定义于 `src/domain/Types.h`：`Provider { OpenAI, DeepSeek }`、`Channel { Remote, Microphone }`、`SessionState { Idle, Recording, Paused, Stopping, Finished, Interrupted }`、`GenerationState { Queued, Running, Completed, Cancelled, Failed }`。实体 ID 使用 `QString` / UUID，时间偏移使用 `qint64` 毫秒，跨线程传递值对象并注册 Qt 元类型。

`ProviderConfig` 保存 provider、baseUrl、model 和 credentialId；不含密钥。`Session` 保存 id、title、role、experience、createdAt 和 state；`TranscriptSegment` 保存 id、sessionId、channel、startMs、endMs、text、final、revision；`Question` 保存 id、sessionId、text、sourceIds、revision、parentId；`Generation` 保存 id、sessionId、questionId、questionRevision、provider、model、state、text；`Review` 保存 id、sessionId、provider、model、state、markdown。

数据库接口失败均返回 `false` 并可读取 `QString lastError() const`；实体创建接口返回 `std::optional<T>`。工作线程不能直接使用 GUI 线程的 SQL 连接。

## 构建与验证约定

`scripts/build.ps1 -Configuration Debug` 使用 `D:/Qt/Tools/CMake_64/bin/cmake.exe`、`D:/Qt/Tools/Ninja/ninja.exe`、`D:/Qt/Tools/mingw1310_64/bin/g++.exe` 和 `D:/Qt/6.11.2/mingw_64`，设置当前进程 PATH，不修改系统环境。输出目录 `build/`；脚本成功退出码为 0。

运行测试使用 `D:/Qt/Tools/CMake_64/bin/ctest.exe --test-dir build --output-on-failure`；单任务加 `-R <测试名>`。使用 Qt Test 的真实断言，先观察行为测试失败，再写对应实现；依赖缺失或编译器错误不能当成行为测试失败证据。

所有依赖下载或构建输出限制在工作区的 `.deps/` 中。当前 Qt WebSockets 未安装：执行阶段从 Qt 官方同版本源码构建私有组件，验证官方发布校验值；若网络或源码不可得，保留不依赖该模块的开发进度并明确报告阻碍，不修改 D:/Qt 安装。后续 API 联调由用户在软件本地填写密钥。

### Task 1: 可启动的 Widgets 工程与工具链

**Files:** 创建 `CMakeLists.txt`、`.gitignore`、`scripts/build.ps1`、`src/main.cpp`、`src/ui/MainWindow.h/.cpp`、`tests/tst_window.cpp`。

**Interfaces:** 提供 `MainWindow(QWidget *parent = nullptr)`；构建 `InterviewAssistant` 与 `tst_window` 目标。窗口先显示会话工具栏、对话区、问题区和答案区，不显示模拟成功状态。

- [ ] 创建 Widgets / Multimedia / Network / SQL / Test 的 CMake 配置与最小窗口测试；首次启动不连接网络、不采集音频。
- [ ] 验证窗口包含“开始面试”和“设置”，初始状态为未开始；实现最小主窗口并让 `ctest -R window` 通过。
- [ ] 使用指定工具链构建并实际启动窗口；记录使用的 Qt / 编译器版本，检查中文显示。

### Task 2: 会话存储、设置与凭据

**Files:** 创建 `src/domain/Types.h`、`src/storage/SessionStore.h/.cpp`、`src/app/AppSettings.h/.cpp`、`src/app/CredentialStore.h/.cpp`、`src/ui/SettingsDialog.h/.cpp`、`tests/tst_storage.cpp`、`tests/tst_settings.cpp`。

**Interfaces:** `SessionStore::open(const QString &root)`、`createSession(const QString &title, const QString &role, const QString &experience)`、`sessions() const -> QList<Session>`、`saveTranscript(const TranscriptSegment&)`、`saveQuestion(const Question&)`、`saveGeneration(const Generation&)`、`saveReview(const Review&)`、`setSessionState(const QString&, SessionState)`、`removeSession(const QString&)`；按 sessionId 查询 transcripts / questions / generations / reviews；`CredentialStore::write/read/remove(const QString &credentialId, ...)`；`AppSettings::textConfig() const -> ProviderConfig`。

- [ ] 添加临时目录数据库测试：保存后重开读取内容一致；片段修订执行更新而不重复插入；SQL 错误返回失败；会话删除不能访问数据根目录之外的路径。
- [ ] 添加配置测试：服务商分别保存；自定义 API 地址绑定独立 credentialId；QSettings 不出现测试密钥；测试用内存凭据替身。
- [ ] 实现 SQLite schema 版本、事务写入、QStandardPaths 数据目录和 Windows CredWriteW / CredReadW / CredDeleteW；密码输入框遮挡内容。
- [ ] 运行 `ctest -R "storage|settings"`，并在设置页实际检查服务选择、地址、模型和保存反馈。

### Task 3: OpenAI / DeepSeek 文本适配器

**Files:** 创建 `src/ai/SseParser.h/.cpp`、`src/ai/TextClient.h/.cpp`、`src/ai/OpenAiAdapter.h/.cpp`、`src/ai/DeepSeekAdapter.h/.cpp`、`tests/tst_sse.cpp`、`tests/tst_textclient.cpp`。

**Interfaces:** `SseParser::feed(const QByteArray&) -> QList<SseEvent>`；`TextClient::start(const TextRequest&) -> QString`、`cancel(const QString &requestId)`；信号 `textDelta(requestId, QString)`、`finished(requestId)`、`failed(requestId, QString)`。`TextRequest` 包含 requestId、sessionId、questionId、config 快照、systemPrompt、userPrompt、jsonMode；密钥仅在发送前从 CredentialStore 获取。

- [ ] 编写逐字节 UTF-8、CRLF、跨块空行、多个 data 行、空 delta、[DONE]、Responses completed / failed / incomplete 的解析测试。
- [ ] 用网络替身检验两家请求路径、认证、超时、取消及各自终止事件；301/302/307/308 不向未绑定地址自动转发密钥；错误消息不含 Authorization 值。
- [ ] 实现 OpenAI Responses 和 DeepSeek Chat Completions 的独立构包及解析；HTTP 错误与流未正常结束返回失败，已接收正文仍可保存；仅答案 content 送入正文区。
- [ ] 运行 `ctest -R "sse|textclient"`；设置页连接测试复用选定适配器，成功需收到真实有效响应。

### Task 4: 可手动使用的问题与回答流程

**Files:** 创建 `src/ai/QuestionExtractor.h/.cpp`、`src/ai/AnswerService.h/.cpp`、`src/ui/QuestionPanel.h/.cpp`、`src/ui/AnswerPanel.h/.cpp`、`tests/tst_questions.cpp`、`tests/tst_answers.cpp`；修改主窗口。

**Interfaces:** `QuestionExtractor::extract(sessionId, QList<TranscriptSegment>, ProviderConfig)`；信号 `questionsReady(sessionId, QList<Question>)`；`AnswerService::generate(const Question&, const Session&, const QList<TranscriptSegment>&, const ProviderConfig&) -> QString`、`cancel(requestId)`；业务记录通过 SessionStore 写入。

- [ ] 使用确定样例测试提取：纯陈述无问题、复合问题、追问关联、重复来源去重、未知引用拒绝；JSON 为空或截断不生成有效问题。
- [ ] 编写交错回调测试：A 问题生成中选择 B 或 DeepSeek，A 的文本只更新 A；取消请求之后不追加新正文；重试保留旧记录。
- [ ] 实现手动新增/编辑/重新分析、独立请求 ID、服务配置快照和单个回答任务队列；提示词区分已知经历与表达模板。
- [ ] 运行 `ctest -R "questions|answers"`；验证不录音时也能手动输入问题、选择服务并显示答案或明确的缺少凭据提示。

### Task 5: 双路音频与 WAV 保存

**Files:** 创建 `src/audio/AudioSource.h`、`src/audio/MicrophoneSource.h/.cpp`、`src/audio/WasapiLoopbackSource.h/.cpp`、`src/audio/PcmConverter.h/.cpp`、`src/audio/WavWriter.h/.cpp`、`src/audio/AudioController.h/.cpp`、`tests/tst_audio.cpp`。

**Interfaces:** `AudioController::devices(Channel) -> QList<AudioDeviceInfo>`、`start(sessionId, remoteDeviceId, microphoneDeviceId)`、`pause()`、`resume()`、`stop()`；信号 `pcmReady(Channel, QByteArray, qint64 startMs)`、`levelChanged(Channel, float)`、`channelError(Channel, QString)`、`stopped()`。提交转写的输出为单声道 24 kHz PCM16，小端；WAV 采用该规范化格式。

- [ ] 测试立体声 float32 / int16 转单声道、跨块重采样连续性、静音与非整帧数据、WAV 头部和文件长度；检测超过 WAV RIFF 容量前分段，禁止长度溢出。
- [ ] 注入两路音频源替身：一路失败不误报另一通道已停止；暂停停止新数据，恢复建新段；静音包补足时间轴，结束只关闭一次文件。
- [ ] 实现 QAudioSource 和 WASAPI COM 采集线程，处理 silent flag、设备失效、共享模式及取消；统一采集时钟和会话目录，磁盘写入失败停止采集并报错。
- [ ] 运行 `ctest -R audio`；实际戴耳机录制本机播放与麦克风，核对两路文件可播放、电平和暂停区间；未实际运行的设备验收保持未勾选。

### Task 6: 实时转写通道

**Files:** 创建 `scripts/prepare-websockets.ps1`、`src/transcription/RealtimeTranscriber.h/.cpp`、`src/transcription/TranscriptAssembler.h/.cpp`、`tests/tst_transcription.cpp`；修改 CMake。

**Interfaces:** `RealtimeTranscriber::connectSession(sessionId, Channel, const TranscriptionConfig&)`、`appendAudio(const QByteArray&, qint64 startMs)`、`finish()`、`abort()`；信号 `segmentChanged(TranscriptSegment)`、`gapDetected(Channel, qint64 startMs, qint64 endMs)`、`error(Channel, QString)`、`finished(Channel)`。TranscriptionConfig 独立保存 WebSocket URL、模型、语言与 credentialId。

- [ ] 获取并验证 Qt 官方匹配版本 Qt WebSockets 源码，使用现有 Qt 工具链安装到 `.deps/qt-websockets`；CMake 同时搜索该前缀，不修改全局 Qt 安装。无法获取时明确记录依赖阻碍。
- [ ] 编写协议样例测试：临时转写修订、最终片段只确认一次、itemId 顺序关联、非法/错误事件、连接建立前缓存、重连去重与缺口。
- [ ] 实现双实例 QWebSocket；依据官方实时转写文档构建 transcription session 与 PCM append / commit。默认中文、模型名称可配置，应用时间戳明确是近似片段位置。
- [ ] 实现最多 30 秒音频的断线缓存及有界重连，溢出区间显示缺口；两路凭据与连接独立于文本服务。
- [ ] 运行 `ctest -R transcription`；有有效凭据才进行真实中文转写和断线重连测试，并记录实际配置和结果。

### Task 7: 完整会话编排与自动提问

**Files:** 创建 `src/app/SessionController.h/.cpp`、`src/ui/TranscriptPanel.h/.cpp`、`src/ui/HistoryDialog.h/.cpp`、`tests/tst_session.cpp`；修改主窗口和装配代码。

**Interfaces:** `SessionController::start(title, role, experience)`、`pause()`、`resume()`、`finish()`、`requestExtraction()`、`selectSession(sessionId)`；信号 `stateChanged(SessionState)`、`transcriptChanged(TranscriptSegment)`、`questionsChanged(QString sessionId)`、`statusChanged(QString)`；控制器持有音频、转写、提取与回答服务。

- [ ] 编写流程测试：远端最终片段进入提取、自己的片段仅进入上下文；新问题排队；手动立即提取可清空当前缓冲；临时转写不触发自动回答。
- [ ] 固定提取合并窗口为 800 ms 静默，最长缓冲 4 s；有最终文本才提交；每次绑定来源片段版本，过期提取结果不覆盖人工编辑。
- [ ] 添加状态竞争测试：停止后最迟等待转写排空 5 s，超时标记缺失；迟到的旧会话事件不写入新会话；重启把未结束会话标为 Interrupted。
- [ ] 实现历史浏览、设备选择、电平、暂停、结束、关闭运行会话时的确认，以及自动问题和回答连接；对话编辑记录修订版本。
- [ ] 运行 `ctest -R session`；实际运行界面确认采集期间可操作、错误状态可见，结束后重启能读取历史。

### Task 8: 有来源依据的复盘、回放和导出

**Files:** 创建 `src/ai/ReviewService.h/.cpp`、`src/storage/MarkdownExporter.h/.cpp`、`src/ui/ReviewDialog.h/.cpp`、`src/ui/PlaybackController.h/.cpp`、`tests/tst_review.cpp`、`tests/tst_export.cpp`。

**Interfaces:** `ReviewService::generate(const Session&, const QList<TranscriptSegment>&, const QList<Question>&, const ProviderConfig&) -> QString`；`MarkdownExporter::write(sessionId, const QString &filePath) -> bool`；`PlaybackController::play(const QString &sessionId, Channel, qint64 offsetMs)` / `stop()`；复盘由 SessionStore 保存，输入中不包含参考答案正文。

- [ ] 编写样例测试：引用的对话属于当前会话、已知问题无实际回答时标记证据不足、参考答案不能进入用户实际回答输入、截断响应保存 Failed 状态、错误引用不能作为有效报告完成。
- [ ] 实现按问题构造上下文，单个输入以 12000 个 Unicode 字符为分段上限并标出被截断上下文，分段分析后汇总；每次保存模型、来源 ID 与生成状态。
- [ ] 实现按通道和音频段定位回放；报告展示时间位置、优点、遗漏、修正版与练习清单；Markdown 导出真实记录，不导出配置和凭据。
- [ ] 运行 `ctest -R "review|export"`；人工检查中文导出和缺失音频反馈；确认删除会话后目录与数据库记录一致。

### Task 9: 端到端验收与 Windows 交付

**Files:** 创建 `README.md`、`scripts/package.ps1`、`docs/acceptance.md`；更新所有任务进度与已知限制。

**Interfaces:** `scripts/package.ps1` 构建 Release，调用同一 Qt 套件的 windeployqt，将必要运行库、插件、应用及说明输出到 `dist/InterviewAssistant`；不包含用户会话和凭据。

- [ ] 完整构建后运行一次全部 CTest；新增修改或新故障才扩大重复验证。
- [ ] 按真实设备检查双路录音、服务切换、暂停/恢复、最终文本、问题追问、取消/重试、结束落盘、历史回放和复盘；两家真实 API 分别记录已验收或缺少凭据。
- [ ] 制作 Release 包，检查 Qt SQL / SQLite、Multimedia 和 WebSockets 运行依赖；从打包目录启动软件验证；不将“能启动”当作设备或云端联调通过。
- [ ] README 写明构建、首次填写凭据、耳机/设备选择、应用数据路径、清理、回环混音和近似时间戳限制；交付可执行文件绝对路径和验证结果。

## 自审与执行方式

已逐项覆盖设计的会话、音频、转写、文本服务切换、问题修订、复盘、错误恢复、存储、凭据、导出和打包要求。五项 Review Focus 均分配了对应行为测试；真实设备和 API 检验单列，不能由替身测试代替。

建议 Native：在当前对话由主代理顺序实现，任务之间共享接口较多，便于及时集成并控制过程成本。用户确认本计划及执行方式后进入实现；若用户明确选择委派，再使用子代理开发与审查流程。
