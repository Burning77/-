# Plus 登录与本地中文转写设计
日期：2026-10-22。用户已要求直接实现前述“本地语音识别 + Plus 分析回答”，沿用当前对话直接实施及推送指定仓库的授权。

目标：没有 API 余额也能在本地保存双路录音并实时显示中文转写；文本生成通过用户在官方浏览器中登录并授权的 ChatGPT 套餐完成，保留原 OpenAI API Key / DeepSeek / 云端转写选项。

## 授权与账户
新增独立 ChatGPT 文本服务，官方动态客户端注册、127.0.0.1 临时端口 /auth/callback、PKCE S256、state/nonce，验证 ID token 的 RS256/JWKS、issuer/audience/expiry/nonce 后保存注册。Host UUID 保持稳定，已发 client_id 按账户保留，令牌存 Windows 凭据管理器，过期前串行刷新。账户切换和注销取消待授权/刷新操作，停止原账户请求；失败不自动改为 API Key 扣费。模型列表从当前授权账户发现，调用使用固定公开 Responses 地址、store:false/stream:true，system 文本转 instructions。授权只由用户在浏览器完成。

## 本地中文识别
默认 Vosk small-cn-0.22，包内附带官方 Windows DLL、中文模型和许可证。PCM24k 单声道转换为16k后送识别工作线程；两路独立识别器共享只读模型。队列上限30秒，溢出报告缺口；临时结果替换同一片段，停顿/8秒上限确认最终文本，暂停/结束排空，旧会话回调隔离。加载模型和推理均不阻塞 GUI；缺依赖明确显示错误并继续录音。

## 产品交互
设置增加 ChatGPT 登录、账户选择、退出、刷新模型和管理用量，显示套餐是否授权；文本服务选择含 ChatGPT。语音页增加本地/云端选择、本地模型路径和本地引擎测试。Plus 只负责文本分析，音频识别保持本地。保留既有API配置，切换只影响后续请求。

## 验证边界
OAuth 回调/PKCE、签名和claims、权限拒绝、刷新/注销/取消及恶意回调有自动化测试；Responses 套餐请求契约有测试。本地依赖加载失败、部分/最终文本、队列边界与真实本地模型解码有测试。完整 Release 和运行包检查后交付0.2.0。真实Plus授权及真实人声准确率由用户操作后才可确认，不能把模拟测试称作账号联调通过。

官方依据：
- https://developers.openai.com/siwc/token-sharing-open-source/sign-in
- https://developers.openai.com/siwc/token-sharing-open-source/models-and-inference
- https://developers.openai.com/siwc/token-sharing-open-source/preview-limitations
- https://alphacephei.com/vosk/models
