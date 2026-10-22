# Plus 与本地转写 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans. Inline implementation selected by the user; one final independent review.

**Goal:** 在既有 Qt Widgets 面试助手中接入官方 ChatGPT 套餐登录和本地中文实时转写。
**Architecture:** ChatGptAccount 集中管理 OAuth/账户/串行刷新；TextClient 按服务绑定令牌及请求契约。LocalTranscriber 以有界队列和工作线程连接现有 AudioController / SessionController。
**Tech Stack:** C++17 / Qt6.11 Widgets Network Sql Multimedia Test，Windows BCrypt / Credential Manager，Vosk C API / small-cn-0.22。
**Spec:** docs/superpowers/specs/2026-10-22-plus-local-asr-design.md

## Global Constraints
- 固定官方OAuth和模型地址，令牌不进入日志、数据库或源码；账户身份验证通过后才替换。
- 套餐失败不自动转API余额；本地ASR默认无需Key，旧云端配置保留。
- 识别模型加载/计算不占GUI线程，队列有界，结束有界排空。
- 不删除用户历史/录音；新版本单独目录打包；真实授权由用户在浏览器完成。

## Review Focus
- 恶意/重复/过期回调与非对应state不得换取令牌：Task1。
- 过期令牌并发刷新、注销/换账户后迟到结果不得复活会话：Task2。
- 套餐请求不得携带旧账户token到自定义地址或回退APIKey：Task2。
- 大量PCM、加载失败、暂停/结束/快速重启不得阻塞或串会话：Task3/4。
- 本地部分文本修订不得累加重复或触发提前回答：Task3/4。

### Task1 OAuth协议与签名
Files: src/auth/OAuthProtocol.h/.cpp, tests/tst_oauth.cpp。
Interfaces: PKCE challenge、授权URL/回调验证、verifyIdToken(jwt,jwks,client,nonce) -> claims/error。
- [ ] 写错state/重复参数/错误nonce/过期/伪签名的失败测试并观察RED。
- [ ] 实现 BCrypt RS256 验证、严格固定issuer/client及随机state/nonce。
- [ ] 跑oauth测试GREEN，提交。

### Task2 ChatGPT账户与文本契约
Files: src/auth/ChatGptAccount.h/.cpp, src/ai/TextClient.*, src/domain/Types.h, tests/tst_chatgpt.cpp。
Interfaces: shared account service login/active/select/signout/token(callback)/models；TextClient start绑定profileId。
- [ ] 写套餐payload、目标地址限制、取消待令牌任务、刷新/账户失效的失败测试。
- [ ] 实现loopback OAuth、凭据安全保存、单次并发刷新、模型发现及取消，改造TextClient套餐请求。
- [ ] 运行oauth/chatgpt/textclient/session测试GREEN，提交。

### Task3 本地识别引擎
Files: src/transcription/LocalTranscriber.*, src/audio/PcmConverter.*, scripts/prepare-local-asr.ps1, tests/tst_localasr.cpp。
Interfaces: connectSession(sid,ch,path), appendAudio(pcm24k,ms), finish/stop/hasUnfinishedAudio，与原转写信号兼容。
- [ ] 写部分替换/最终ID、重启隔离、失败与队列测试RED。
- [ ] 实现Vosk动态加载、共享模型与独立工作线程、16k转换、排空和有界队列。
- [ ] 下载并固定官方运行库/中文模型，补许可证；验证真实引擎和PCM解码，提交。

### Task4 界面和会话集成
Files: src/ui/SettingsDialog.*, MainWindow.*, src/app/AppSettings.*, SessionController.*，现有测试。
- [ ] 写设置模式与套餐选项、识别路由/结束/暂停回归RED。
- [ ] 接通账户UI、模型列表、本地识别设置和会话录音，保留旧服务。
- [ ] 本地完整测试GREEN，提交。

### Task5 交付
Files: CMakeLists.txt, scripts/package.ps1, README.md, docs/acceptance.md。
- [ ] 一次完整Release测试，打包0.2.0含本地模型依赖并启动检查。
- [ ] 最终独立代码审查；重要问题一次修复并验证；记录所有裁定。
- [ ] 推送指定GitHub main并核对SHA，交付包及用户登录操作说明。
