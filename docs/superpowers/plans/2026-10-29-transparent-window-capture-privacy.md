# 半透明界面与远程捕获排除 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 提供仅背景半透明的界面，以及本机仍可见、明确报告状态的 Windows 捕获排除开关。

**Architecture:** 外观、无边框操作栏及捕获状态各自独立。普通/透明模式共用固定的透明原生窗口，普通模式绘制不透明浅色表面，避免切换时重建句柄。捕获控制器只管理主窗口及其所属顶层弹窗，原生适配器校验本进程所有权、Windows版本和实际读回值。

**Tech Stack:** C++17、Qt Widgets 6.11.2、Windows User32/DWM、QtTest、现有 CMake/MinGW 构建与打包脚本。

**Spec:** `docs/superpowers/specs/2026-10-29-transparent-window-capture-privacy-design.md`

## Global Constraints

- 透明背景默认不透明度80%，允许40%至95%；普通模式恢复浅色界面，文字不透明。
- “远程隐藏”默认关闭，每次启动主动开启；完整排除要求 Windows 10 2004 或更高版本。
- 只报告实际接口结果，失败明确显示未完全生效；不静默退回黑块遮挡。
- 新顶层对话框及原生句柄重建后重新应用；本机窗口保持可见。
- 不操作向日葵、实际账号、简历或用户录音，不捕获用户桌面；向日葵实会话必须标为未验收。
- 延用当前对话直接实施方式与已有 GitHub 同步授权，不增加实施审批轮次。

## Review Focus

- 透明模式切换期间录音、会话和原生句柄保持稳定（Task 3集成测试）。
- 弹窗设置失败及关闭捕获排除失败，不能显示全部成功（Task 2故障注入）。
- 未显示窗口、重新显示和 WinIdChange，不能遗漏应用或重复递归（Task 2事件测试）。
- 高DPI/小屏幕及最大化后的边缘操作，不能丢失恢复与关闭入口（Task 1及既有窗口套件）。
- 深色透明下输入区、富文本和设置窗口的前景/背景不能混色（Task 1渲染及Task 3截图）。

### Task 1: 透明外观与窗口操作

**Files:** 创建 `src/ui/WindowAppearance.h/.cpp`、`src/ui/WindowChrome.h/.cpp`、`tests/tst_appearance.cpp`；修改 `src/ui/MainWindow.h/.cpp`。

**Interfaces:** `WindowAppearance::surface(bool,int)->QColor`、`WindowAppearance::styleSheet(bool)->QString`、`WindowAppearance::palette(bool)->QPalette`、`WindowChrome(QWidget*)`；MainWindow保留现有构造调用方式。

- [x] 写真实 MainWindow 回归：透明开关与40/80/95背景alpha、文字alpha255、窗口opacity仍1、普通浅色恢复；先运行观察缺失功能失败。
- [x] 实现固定 FramelessWindowHint/WA_TranslucentBackground，自绘表面；独立透明样式，QAbstractScrollArea视口/内容的填充随模式切换。
- [x] 实现操作栏与8个缩放手柄，原生移动/缩放优先，离屏测试平台提供几何回退；保留最大化、还原、最小化、关闭和屏幕内恢复。
- [x] 接入“透明背景”和不透明度调节，保存 `appearance/transparent` 与 `appearance/backgroundOpacity`，非法值回退80/钳制40..95。
- [x] 运行 `tst_appearance` 及既有 `tst_window`，修复后通过，检查合成界面截图，提交 Task 1。

### Task 2: 捕获排除适配器及所属窗口状态

**Files:** 创建 `src/app/WindowCapture.h/.cpp`、`src/ui/CapturePrivacy.h/.cpp`、`tests/tst_windowprivacy.cpp`；修改 `CMakeLists.txt` Windows链接项。

**Interfaces:** `WindowCaptureResult {bool ok; QString error;}`；`WindowCaptureBackend=std::function<WindowCaptureResult(quintptr,bool)>`；`WindowCapture::setExcluded(quintptr,bool)`；`CapturePrivacy(QWidget*,WindowCaptureBackend={})`，`setRequested(bool)`、`requested()`、`fullyApplied()`、`error()`及`stateChanged()`。

- [x] 写默认关闭、主窗请求/清除、子对话框/弹窗、外部窗口忽略、句柄变化重用、失败状态及关闭失败测试，先失败再实现。
- [x] 原生适配器只接受当前进程的有效顶层HWND；启用时核对Windows build>=19041及DWM，设置0x11后用GetWindowDisplayAffinity读回；禁用设0，错误返回清晰原因。
- [x] 控制器过滤本窗口所属树的 Show/WinIdChange/Hide，跟踪QPointer及每窗结果，处理尚未创建/销毁的句柄，失败不冒充成功；关闭对所有曾管理的原生窗口清除。
- [x] 使用本进程创建的不可见Win32测试窗口验证真实0x11/0读回，不抓取桌面。模拟后端覆盖错误、重入及局部失败；两类测试均通过后提交 Task 2。

### Task 3: 开关集成、回归及交付

**Files:** 修改 `MainWindow.h/.cpp`、`CMakeLists.txt`、`README.md`、`docs/acceptance.md`、两个新测试套件。

**Interfaces:** MainWindow构造增加可选 `WindowCaptureBackend` 以便隔离验证；现有两参数调用保持兼容。按钮与状态显示只消费CapturePrivacy的请求/应用结果。

- [x] 先写集成失败测试：默认off、成功/失败/未完全生效显示、透明切换及录音期间状态不丢失、设置窗口仍受管理。
- [x] 加“远程隐藏”独立按钮和“向日葵效果需实测”提示；错误与关闭失败可见，始终不隐藏本地窗口。快捷键窗口内Ctrl+Shift+O切外观、Ctrl+Shift+P切捕获请求。
- [ ] 本对话内完成全部任务，作一次全分支独立只读审查；重要问题修复须先复现，再验证。
- [ ] 更新版本0.7.4，构建并运行完整Release CTest；生成独立包，移除开发PATH复验新功能与渲染，检查ZIP不含用户数据。
- [ ] 更新验收记录，明确Windows接口验证与向日葵实测的区别；提交最终改动，按已有授权同步main到GitHub并读回远端提交。

## 实施记录

用户已指定在当前对话直接实施。编译继续使用既有英文构建副本；只读审查一次，执行者不分派实施子代理。任务过程与验证命令记录在此计划对应的独立执行台账。
