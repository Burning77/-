# 面试助手 · C++ / Qt Widgets

Windows 桌面面试记录工具：双路录音、实时中文转写、问题提取、OpenAI GPT / DeepSeek 流式参考回答、面试复盘和 Markdown 导出。

## 0.2.0：本地中文识别与 ChatGPT 套餐

运行包：`dist/InterviewAssistant-0.2.0-win64.zip`。完整解压后运行同目录的 `InterviewAssistant.exe`，保留 `local-asr` 目录。包内附带 Vosk small-cn-0.22 中文模型，默认在本机识别，无需语音 API Key。模型首次加载需要数秒，状态显示“本地中文识别已就绪”后说话。识别在线程中执行；临时文本会修订，停顿后形成最终文本。

1. 设置 → **ChatGPT 套餐** → **Continue with ChatGPT**，在系统浏览器登录并允许使用套餐。
2. 返回软件，在已授权账户的模型列表中选择模型并保存；当前文本服务选择 **ChatGPT**。
3. 设置 → **语音转写**，保持“本地中文识别”，点击测试确认模型可加载。
4. 选择实际会议使用的播放设备及自己的麦克风，开始面试；两路音频分别识别。

ChatGPT 套餐授权与 OpenAI API Key 是两条独立路径；此版本通过官方 Sign in with ChatGPT 接入已授权套餐，受账户套餐、模型可用性和使用限制约束。不会读取 Codex 的登录凭据，也不会在套餐失败时自动转用 API 余额。API 平台余额为零不代表 Plus 订阅无效；使用“OpenAI”服务仍需单独的 API 余额。

支持保存多个账户/工作区注册，切换后取消旧账户的活动请求；退出会删除本地凭据并尝试远端撤销。OAuth 凭据经 Windows DPAPI 加密后原子保存，注册映射保留用于再次登录。本地识别不上传音频，提取问题、生成回答及复盘会将必要文本发给所选服务。可切换回云端转写；ChatGPT 套餐登录不用于语音 API。

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

1. 推荐按上面的 0.2.0 步骤登录 ChatGPT 套餐；也可选择 OpenAI 或 DeepSeek，填写 API 地址、模型 ID 和对应 API Key。
2. “语音转写”默认本地中文识别。需要云端时切换模式，填写独立语音凭据；DeepSeek 文本 Key 不用于 OpenAI 语音转写。
3. 测试本地识别仅加载模型；文本或云端转写测试会产生真实网络请求，受所选账户的计费及额度规则约束。
4. 填写面试名称、岗位和真实项目经历摘要，选择远端播放设备与麦克风。
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
