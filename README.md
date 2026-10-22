# 面试助手 · C++ / Qt Widgets

Windows 桌面面试记录工具：双路录音、实时中文转写、问题提取、OpenAI GPT / DeepSeek 流式参考回答、面试复盘和 Markdown 导出。

## 0.1.2 连接诊断修复

模型测试失败时显示 HTTP 状态、服务错误码、类型和简短说明，支持 JSON 及流式错误，密钥会先脱敏。`error: null` 不再被误判为服务失败。HTTP 错误正文最多缓存 64 KiB，非 JSON 网关页面不直接显示。

运行包：`dist/InterviewAssistant-0.1.2`。进入设置重新测试，将具体错误码作为排查依据；“已保存”只表示本地保存了密钥，不代表服务验证成功。

实时对话还需要单独配置“语音转写”页并通过测试。文本服务使用模型 API ID，例如 `gpt-6.1-sol`；语音页默认使用 `gpt-live-transcribe` 和 `wss://api.openai.com/v1/realtime?intent=transcription`。官方 OpenAI Key 可在对应权限允许时分别填写到两页，程序按服务和地址独立保存。

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
pwsh -File scripts/package.ps1 -BuildRoot E:/project/InterviewAssistant-build -PackageName InterviewAssistant-0.1.1
```

脚本默认使用 `D:/Qt/6.11.2/mingw_64` 和 `D:/Qt/Tools` 中的配套工具。源码路径含中文时，将源码复制到指定英文目录编译，再把可执行文件复制回工程。`-BuildRoot` 应使用专门的英文目录；未指定则使用临时目录中的工程专用子目录。不更改系统 PATH 或全局 Qt 安装。

## 首次使用

1. 打开“设置”，选择 OpenAI 或 DeepSeek；分别填写 API 地址、账户可用的模型 ID 和对应 API Key。
2. 在“语音转写”页填写独立的转写凭据；默认连接 OpenAI 实时转写，默认模型为 `gpt-live-transcribe`。DeepSeek 文本 Key 不用于 OpenAI 语音转写。
3. 使用连接测试确认模型和转写配置；测试会产生真实 API 请求。
4. 填写面试名称、岗位和真实项目经历摘要，选择远端播放设备与麦克风。
5. 在参与者知情并同意录音及 AI 辅助的情况下开始；建议佩戴耳机，减少远端声音回录进麦克风。
6. 结束后从历史记录回听、生成复盘并导出报告。

未配置转写服务时，本地录音与手动问题流程仍可使用。模型调用需要可用的 API 权限与网络；网页登录或订阅不等同于 API 配置。

## 数据与密钥

- 应用数据目录由 Qt `QStandardPaths::AppDataLocation` 决定，界面提供目录入口。
- 数据库：`interviews.sqlite`；音频：`sessions/<会话ID>/*.wav`。
- API Key 存入 Windows 凭据管理器，按服务商及 API 地址分别绑定。
- 源码、导出和日志不包含 API Key。发布包不包含用户数据库与录音。
- 音频会发送给配置的转写服务；问题及必要对话上下文会发送给所选文本服务。

## 使用边界

回环采集的是播放设备混音，可能包括其他应用和通知声音。来源标签表示音频通道，不能准确识别多位面试官的身份。时间位置来自应用估算，实时转写不提供逐词精确对齐。

模型输出是参考材料，可能存在识别和推理错误；可以编辑问题与转写后重新分析。复盘建议与应聘者实际回答分开记录。长会话按对话分段分析，超长发言拆段保留；报告保存原始来源版本及节选快照，缺失证据时应明确说明。

## 验证记录

见 [docs/acceptance.md](docs/acceptance.md)。本地构建、自动化测试、真实设备测试和云端联调分别记录，不以模拟测试代替真实服务验收。
