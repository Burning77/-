# 面试助手 · C++ / Qt Widgets

Windows 桌面面试记录工具：双路录音、实时中英混合转写、问题提取、OpenAI GPT / DeepSeek 流式参考回答、面试复盘和 Markdown 导出。

## 0.7.0：ChatGPT 推理强度与公司联网资料

“设置 → ChatGPT 套餐”新增独立的 **推理强度**，初始为 **high**，可选择 low、medium、high、xhigh、max 或“默认（由模型决定）”。模型 ID 继续使用账户返回的原始 slug，例如 gpt-6.1-sol，不添加 -high 后缀。保存后，ChatGPT 的连接测试、问题提取、参考回答、复盘和准备分析使用所选强度；选“默认”时不发送强度参数。各模型可用强度以服务端支持为准，OpenAI API Key 和 DeepSeek 原有强度行为不变。

“面试准备”新增 **联网核实公司信息（ChatGPT / OpenAI）**，初始勾选，可保存为关闭。点击“AI 分析公司与岗位”先单独联网检索公司公开资料，搜索阶段只提供公司名；取得实际搜索完成事件和有效来源后，再把公司资料、职位描述和简历交给当前 AI 分析。报告附检索时间和可点击来源，区分来源事实、推测和待确认项，不把模型记忆当作已联网核实。

联网和后续分析分别发起请求，受当前模型、账户/工作区搜索权限及用量限制。搜索失败、找不到有效来源或权限不允许时会显示错误，保留上次分析和已填资料；不会静默改用其他账号或 API Key。当前 DeepSeek 适配器尚未接入该联网工具，可以明确取消勾选后继续只分析填写资料。取消勾选或更换公司不会套用先前公司的联网资料。

“面试前分析”页直接显示分析中、已过期、失败、停止或已完成状态。成功后自动显示本次新分析；失败/停止显示明确的旧结果提示。生成成功但保存失败会保留新内容并显示未保存状态，可再次点击“保存准备信息”。录音中的准备快照保持只读。

退出旧版后运行 dist/InterviewAssistant-0.7.0/InterviewAssistant.exe，保留同目录组件。进入设置选择模型和推理强度并保存，再到面试准备勾选联网核验，点击分析。真实账号的搜索权限及实际返回内容仍需在用户登录环境确认。

接口依据：[GPT-6.1 Sol 推理强度](https://developers.openai.com/api/docs/models/gpt-6.1-sol)、[原生网页搜索及来源](https://developers.openai.com/api/docs/guides/tools-web-search)、[ChatGPT 套餐工具限制](https://developers.openai.com/siwc/token-sharing-open-source/preview-limitations)。

## 0.6.3：DeepSeek 连接关闭后的有界恢复

针对 Qt 2 / Connection closed，模型列表读取最多自动重试两次。参考回答、问题提取、岗位分析及连接测试共用的 DeepSeek 文本请求，仅在新连接尚未建立 TLS、尚未发出 HTTP 请求且未收到任何内容时自动重试，最多两次。已经发出请求、已有部分内容、证书错误、HTTP 错误、取消或账号改变均不会自动重发；对曾建立过 TLS 的连接池来源也保守地不重发。

当前环境复现到请求经 Windows 系统代理 127.0.0.1:7897 时，部分连接约5秒后在 TLS 建立前被关闭，另一些能够收到 HTTP 响应。普通网页可用并不能证明这条 API 链路稳定；尚不能断定关闭发生在代理、上游路径还是服务端。0.6.2 的 TLS 组件切换没有解决全部中断，本版增加的是恢复机制，不宣称消除了外部链路故障。

退出旧版后运行 dist/InterviewAssistant-0.6.3/InterviewAssistant.exe，保留同目录文件。先点“读取账号可用模型”，再点“保存并测试连接”。通常无需重新登录；失败后请保留完整错误提示。真实账号推理仍需在用户登录环境确认；软件不会关闭系统代理、忽略证书校验或在失败后改用 API Key。

## 0.6.2：DeepSeek 登录状态及网络兼容

修复登录后的模型列表读取失败覆盖“已登录”提示的问题，保留当前模型与登录凭据。网络失败显示主机、Qt 错误码及具体连接原因，替代含糊的 HTTP 0 提示。运行包补齐并优先使用 OpenSSL 3.5.9 与匹配的 Qt TLS 插件，作为当前代理环境的兼容措施，保留系统代理和证书校验。测试中曾复现连接被关闭，后续两种后端均恢复连通，因此不能将间歇性中断完全归因于某个 TLS 后端。

先结束当前会话并退出旧版，再运行 dist/InterviewAssistant-0.6.2/InterviewAssistant.exe，保留同目录的全部 DLL 和 tls/。进入“设置 → DeepSeek → 读取账号可用模型”，然后“保存并测试连接”；已有登录通常无需重复授权。真实账号可用模型、权限和额度仍以平台返回为准。

## 0.6.1：参考回答优先结合本场简历

自动参考回答、手动添加问题及“重新生成所选回答”统一优先读取本场简历快照，结合问题选取相关项目、技术和职责，以面试者第一人称简短口述。完整简历进入回答请求，不依赖旧版摘要副本；没有简历时兼容旧版经历。保留参与、协助、负责的职责边界，不把参与说成主导，不虚构简历没有的项目或成果。

先退出旧版，再运行 dist/InterviewAssistant-0.6.1/InterviewAssistant.exe。上传简历后开始的新会话使用本场简历；旧会话保持其原有背景。已经生成的回答可选中问题后点 **重新生成所选回答**。OpenAI、ChatGPT、DeepSeek 使用同一规则。

## 0.6.0：上传简历并结合简历分析岗位

主界面和“面试准备 → 公司与岗位”中的经历输入区改为 **上传简历**。支持有文字层的 PDF（最多30页）、DOCX、TXT，文件最大20 MB、提取文字最多20000字。选择文件后在本机后台提取，显示文件名和字数，点 **查看文字** 检查内容；可更换或移除。无需安装 Office、Python 或额外填写语音 API。

填写公司、岗位和职位描述，再点 **AI 分析公司与岗位**。当前选择的 OpenAI、ChatGPT 或 DeepSeek 会结合简历分析岗位匹配点、差距、项目准备、常见追问和专业术语。点击分析时向所选服务发送提取文字，不上传原始文件；准备草稿和本场快照保存文字及文件名，原简历可移动或删除。

更换或移除简历后请重新分析；旧分析和未编辑的旧术语不会套用于新背景。开始面试时保存独立快照，参考回答也使用本场简历背景。录音中不能替换本场简历。旧版手填经历仍可查看，上传后替换。

扫描/图片 PDF 需要先做 OCR 或另存为可选中文字的 PDF/DOCX/TXT；本版不做 OCR。加密 PDF 需先解除密码，旧 DOC 需另存为 DOCX/PDF。复杂多栏排版可能影响文字顺序，请先查看提取文字。读取失败、超长或超时会保留原简历。

先退出旧版，再运行 dist/InterviewAssistant-0.6.0/InterviewAssistant.exe；保留同目录的 ResumeReader.exe、resume/ 和 local-asr/。完整压缩包为 dist/InterviewAssistant-0.6.0-win64.zip。

## 0.5.2：上翻查看时暂停自动跟随

实时对话和面试官问题默认跟随新内容。向上滚动查看历史时，刷新会保留阅读位置、横向滚动位置和选中的条目；即使只向上滚动一条，也不会被强制拉到底部。滚回底部后恢复自动跟随。切换会话时显示该会话的最新内容。

先退出旧版，再运行 dist/InterviewAssistant-0.5.2/InterviewAssistant.exe。保留原有设置、面试准备和历史记录；本次仅修正列表刷新和滚动行为。

## 0.5.1：简短的面试者口述回答

参考回答改为面试者第一人称，默认 2～4 句、约 80～160 字，直接给结论和必要理由。省去分析过程、要点清单、原理讲义、表达框架及追问预测；只有题目明确要求展开、举例或代码时才补充必要内容。保留不虚构经历、职责或测试数据的约束。DeepSeek、OpenAI、ChatGPT 使用同一回答规则。

运行 dist/InterviewAssistant-0.5.1/InterviewAssistant.exe 前先退出旧版。新生成的参考回答采用新规则；已经保存的长回答需选择问题后点 **重新生成所选回答**。运行包完整保留 0.5.0 的本地中英混合识别组件。

## 0.5.0：本地中英混合实时识别

运行包：dist/InterviewAssistant-0.5.0-win64.zip。完整解压后打开 InterviewAssistant.exe，保留旁边的 local-asr 目录。先结束旧版会话并退出旧版，新版继续使用原来的账号设置、面试准备和历史。

- 默认语音引擎改为 **sherpa-onnx + 中英双语流式 Zipformer**，同时识别中文、英文和一句话中的中英混说，英文直接显示英文字母。模型的英文通常为大写。识别在后台进行，两路音频独立分段，共用模型以减少内存占用；无需语音 API Key。
- 设置 → **语音转写 → 本地中英混合识别 → 保存并测试连接**。开始面试后，设备下方应显示 **本地中英混合识别已就绪**。首次加载需数秒。升级时自动采用随包双语模型，不沿用旧 Vosk 中文模型路径；自定义目录也必须是兼容的双语模型。
- 术语表仍可纠正明确的误听和字母拆读，但不会把已识别的英文替换成中文译名。纠正会保留原始文字。面试前在“面试准备”补充岗位技术缩写及明确别名，有助于后续理解；无法保证生僻缩写、极短单词、口音或噪声下的准确率。
- 原来的中文模型没有英文识别能力；切换文本服务 OpenAI / ChatGPT / DeepSeek 不会改变语音模型。新版采用独立双语识别引擎，录音仍留在本机。过去已经误听的文字不会自动恢复，可以双击修改。

双语模型与词表约 200 MB，运行包和运行时内存比旧中文小模型更大。官方模型说明见 [sherpa-onnx 中英双语流式模型](https://k2-fsa.github.io/sherpa/onnx/pretrained_models/online-transducer/zipformer-transducer-models.html#csukuangfj-sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20-bilingual-chinese-english)。离线测试包含真实中英混说与英文句子；仍需用自己的麦克风和会议音频检查效果。

开发验证：先运行 scripts/prepare-local-asr.ps1 和 scripts/prepare-asr-test-data.ps1，再运行 scripts/test.ps1 -Configuration Release。测试音频只用于开发验证，不随运行包分发。

## 0.4.0（历史版本）：面试前准备、专业术语辅助和离屏恢复

运行包：dist/InterviewAssistant-0.4.0-win64.zip。主界面新增 **面试准备**：填写公司、岗位、职位描述和真实经历，再点 **AI 分析公司与岗位**。使用当前已选择的 ChatGPT / OpenAI / DeepSeek 服务，生成岗位理解、技术考点、常见问题、准备清单和可编辑术语表；仅分析填写资料，不联网查询公司，模型推测应自行核对。

术语表填写标准词、常见读法或误识别别名及中文含义，可手动增加。保存后用于下一场面试；开始录音时保存独立快照，问题提取、参考回答和复盘会参考本场资料，录音中可点“面试准备”查看只读快照。更改公司、岗位、职位描述或经历后应重新分析；旧分析失效，未编辑的旧术语也不会自动套用。

本地识别使用明确、无歧义的别名辅助纠正最终文本，保留原始转写并标注“术语辅助”；没有重训 Vosk，无法保证本地模型识别所有英文缩写或口音。云端 gpt-live-transcribe 使用术语 keywords 和背景 prompt，仍需独立语音 API 权限。术语含义也传给文本模型辅助理解；误听仍可双击修正。云端参数依据 [OpenAI 实时转写文档](https://developers.openai.com/api/docs/guides/realtime-transcription)。

准备草稿原子保存到应用数据目录 preparation.json，分析失败或停止会保留填写资料。开始面试后的准备快照保存在会话中，后续修改不会改写历史。导出报告区分准备参考资料与实际对话。

窗口首次居中之外，在重新显示、激活、恢复及显示器变化时检查离屏；有效的手动位置及屏幕内最大化状态保留。修正 Qt 控件与原生窗口最大化状态/位置不同步的边界。

## 0.3.0：DeepSeek 账号登录、窗口大小及丢弃录音

运行包：`dist/InterviewAssistant-0.3.0-win64.zip`，完整解压后运行同目录的 `InterviewAssistant.exe`。旧版的会话先保存或按已有流程结束，再退出并打开新版。

- 初次显示在主屏幕居中，窗口客户区最大约 1280×780，同时不超过屏幕可用宽高的 85%；小窗口支持滚动，设置页也可滚动。
- 录音中或暂停时可点 **结束不保存**，确认后停止采集和生成，删除本次会话的录音、对话及 AI 结果；即使正在浏览另一条历史记录，也只丢弃正在录音的会话。
- **设置 → DeepSeek → 连接方式选“DeepSeek 账号登录” → 使用 DeepSeek 账号登录**。在默认浏览器中的官方平台页面完成授权，回到软件读取可用模型，再点“保存并测试连接”；文本服务选 **DeepSeek**。
- 保留 **API Key** 方式，可手动切换。账号调用使用 DeepSeek 平台授予的权限和额度，不保证网页聊天的免费用量适用于本软件。退出或授权失效时不会自动改用 API Key。

账号授权采用 PKCE 和本机回调，凭据用 Windows DPAPI 加密保存。取消及退出会阻止迟到的登录响应恢复账户；推理凭据仅发送到 DeepSeek 官方账号推理端点。语音转写仍按语音设置独立运行。协议依据为 DeepSeek 官方 [账号授权实现](https://github.com/deepseek-ai/deepseek-harness/tree/master/packages/credentials/deepseek-account-platform) 和 [账号模型适配](https://github.com/deepseek-ai/deepseek-harness/tree/master/packages/llm/llm-deepseek-account)。真实 DeepSeek 账号授权及回答需用户在浏览器登录后测试，本地测试使用隔离的虚构凭据。

## 0.2.2：ChatGPT 问题提取请求修复

修复 ChatGPT 套餐提取问题时 HTTP 400：JSON 输出要求现在同时放入 `input` 的 developer 消息，满足 JSON 模式的请求检查；原始对话和来源校验保留。运行包 `dist/InterviewAssistant-0.2.2`。结束并保存旧版会话后退出，打开新版，从历史中选择会话再点“立即提取问题”，无需重新录音。

## 0.2.1：启动位置修复

首次显示时指定主屏幕，按排除任务栏的可用区域将整个窗口居中。窗口之后手动移动、隐藏或恢复时保留当前位置。运行包 `dist/InterviewAssistant-0.2.1`，退出旧版后打开新版。

## 0.2.0：本地中文识别与 ChatGPT 套餐

运行包：`dist/InterviewAssistant-0.2.0-win64.zip`。完整解压后运行同目录的 `InterviewAssistant.exe`，保留 `local-asr` 目录。包内附带 Vosk small-cn-0.22 中文模型，默认在本机识别，无需语音 API Key。模型首次加载需要数秒，状态显示“本地中文识别已就绪”后说话。识别在线程中执行；临时文本会修订，停顿后形成最终文本。

1. 设置 → **ChatGPT 套餐** → **Continue with ChatGPT**，在系统浏览器登录并允许使用套餐。
2. 返回软件，在已授权账户的模型列表中选择模型并保存；当前文本服务选择 **ChatGPT**。
3. 设置 → **语音转写**，保持“本地中文识别”，点击测试确认模型可加载。
4. 选择实际会议使用的播放设备及自己的麦克风，开始面试；两路音频分别识别。

ChatGPT 套餐授权与 OpenAI API Key 是两条独立路径；此版本通过官方 Sign in with ChatGPT 接入已授权套餐，受账户套餐、模型可用性和使用限制约束。不会读取 Codex 的登录凭据，也不会在套餐失败时自动转用 API 余额。API 平台余额为零不代表 Plus 订阅无效；使用“OpenAI”服务仍需单独的 API 余额。

支持保存多个账户/工作区注册，切换后取消旧账户的活动请求；退出会删除本地凭据并尝试远端撤销。OAuth 凭据经 Windows DPAPI 加密后原子保存，注册映射保留用于再次登录。本地识别不上传音频，提取问题、生成回答及复盘会将必要文本发给所选服务。可切换回云端转写；ChatGPT 套餐登录不用于语音 API。

同一 Windows 用户仅运行一个新版实例，以避免多个进程同时轮换登录凭据。识别引擎缺失或失败时会保留录音，并把识别问题及缺失区间写入会话，便于复盘时识别证据缺口。

官方协议依据：[登录接入](https://developers.openai.com/siwc/token-sharing-open-source/sign-in)、[模型与推理](https://developers.openai.com/siwc/token-sharing-open-source/models-and-inference)、[预览限制](https://developers.openai.com/siwc/token-sharing-open-source/preview-limitations)。本地组件许可见 [THIRD_PARTY.md](THIRD_PARTY.md)。

## 0.1.2 连接诊断修复

模型测试失败时显示 HTTP 状态、服务错误码、类型和简短说明，支持 JSON 及流式错误，密钥会先脱敏。`error: null` 不再被误判为服务失败。HTTP 错误正文最多缓存 64 KiB，非 JSON 网关页面不直接显示。

运行包：`dist/InterviewAssistant-0.1.2`。进入设置重新测试，将具体错误码作为排查依据；“已保存”只表示本地保存了密钥，不代表服务验证成功。

旧版实时对话需要单独配置云端语音转写；0.2.0 默认本地识别。云端模式可使用 `gpt-live-transcribe` 和 `wss://api.openai.com/v1/realtime?intent=transcription`，需要相应 API 权限。

## 0.1.1 卡顿修复

- 未配置语音转写服务时只进行本地录音，显示“仅本地录音”，不缓存音频或反复生成缺失提示。
- 连续转写缺失按通道合并并定期保存，界面和流式回答按短间隔批量更新。
- 旧会话中大量连续缺失提示会合并显示，原始数据库记录保留。
- 修复包位于 `dist/InterviewAssistant-0.1.1`，标题栏显示 `0.1.1`。退出旧程序后运行新版；会话数据目录和凭据位置保持一致。

## 功能

- WASAPI 回环采集所选播放设备，QAudioSource 采集麦克风；两路 WAV 分别保留。
- 实时转写显示临时文本和最终文本，来源、时间位置及缺失区间可追溯。
- 自动提取问题和追问，也可手动输入、修改、重新分析。
- OpenAI / DeepSeek 分别配置 API 地址、模型和密钥，切换影响后续请求。
- 复盘依据实际对话给出回答优点、知识遗漏、表达建议和练习清单。
- SQLite 保存会话；音频回放、历史查看、删除及 Markdown 导出。

## 开发环境

本机工具链：Qt 6.11.2 MinGW 64 位、配套 GCC 13.1、CMake 3.30、Ninja、PowerShell 7。

Qt 组件需要 Widgets、Multimedia、Network、SQL 和 Test。转写的 WebSocket 使用 Windows WinHTTP，不需要额外安装 Qt WebSockets。

```powershell
pwsh -File scripts/build.ps1 -BuildRoot E:/project/InterviewAssistant-build
pwsh -File scripts/test.ps1
pwsh -File scripts/package.ps1 -BuildRoot E:/project/InterviewAssistant-build
# 输出到独立的修复包目录：
pwsh -File scripts/package.ps1 -BuildRoot E:/project/InterviewAssistant-build -PackageName InterviewAssistant-0.2.0
```

脚本默认使用 `D:/Qt/6.11.2/mingw_64` 和 `D:/Qt/Tools` 中的配套工具。源码路径含中文时，将源码复制到指定英文目录编译，再把可执行文件复制回工程。`-BuildRoot` 应使用专门的英文目录；未指定则使用临时目录中的工程专用子目录。不更改系统 PATH 或全局 Qt 安装。

## 首次使用

1. 可按上述步骤登录 ChatGPT 套餐或 DeepSeek 账号；也可选择 OpenAI / DeepSeek 的 API Key 方式，填写地址、模型 ID 和对应密钥。
2. “语音转写”默认本地中英混合识别。需要云端时切换模式，填写独立语音凭据；DeepSeek 文本 Key 不用于 OpenAI 语音转写。
3. 测试本地识别仅加载模型；文本或云端转写测试会产生真实网络请求，受所选账户的计费及额度规则约束。
4. 填写面试名称和岗位，上传简历；在“面试准备”补充公司与职位描述，点击“AI 分析公司与岗位”，然后选择远端播放设备与麦克风。
5. 在参与者知情并同意录音及 AI 辅助的情况下开始；建议佩戴耳机，减少远端声音回录进麦克风。
6. 结束后从历史记录回听、生成复盘并导出报告。

本地模型缺失时仍可录音和手动添加问题，但不会出现自动转写。文本分析需要网络及有效的套餐授权或 API 权限。

## 数据与密钥

- 应用数据目录由 Qt `QStandardPaths::AppDataLocation` 决定，界面提供目录入口。
- 数据库：`interviews.sqlite`；音频：`sessions/<会话ID>/*.wav`。
- API Key 存入 Windows 凭据管理器，按服务商及 API 地址分别绑定。
- 源码、导出和日志不包含 API Key。发布包不包含用户数据库与录音。
- 本地模式在本机识别；只有云端模式向配置的转写服务发送音频。问题及必要对话上下文会发送给所选文本服务。

## 使用边界

回环采集的是播放设备混音，可能包括其他应用和通知声音。来源标签表示音频通道，不能准确识别多位面试官的身份。时间位置来自应用估算，实时转写不提供逐词精确对齐。

模型输出是参考材料，可能存在识别和推理错误；可以编辑问题与转写后重新分析。复盘建议与应聘者实际回答分开记录。长会话按对话分段分析，超长发言拆段保留；报告保存原始来源版本及节选快照，缺失证据时应明确说明。

## 验证记录

见 [docs/acceptance.md](docs/acceptance.md)。本地构建、自动化测试、真实设备测试和云端联调分别记录，不以模拟测试代替真实服务验收。
