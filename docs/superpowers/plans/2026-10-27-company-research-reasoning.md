# Company research and reasoning Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** Add selectable ChatGPT reasoning defaulting to high, and source-backed company research before resume/JD preparation.

**Architecture:** Preserve the current Qt Widgets and TextClient transport. Add a company_research request operation with hosted web_search; consume authoritative search events/sources, then run the existing JSON preparation request separately. Rendering distinguishes old/pending/failed results.

**Tech Stack:** C++17, Qt6.11 Widgets/Network, Responses HTTP/SSE, QSettings, QtTest.

**Spec:** docs/superpowers/specs/2026-10-27-company-research-reasoning.md

## Global constraints

Keep server model IDs unchanged. Company-only web queries. No provider/account fallback. Native search completion and safe HTTP(S) sources required. Default high applies to ChatGPT only. Keep existing recordings and old release packages intact. Do not test with real credentials/resumes.

## Review focus

- A selected effort must survive saving without a logged-in model list; test settings persistence and outgoing payload.
- A textual claim to have searched is insufficient; test rejection without search events or sources.
- Resume/JD/old background must not enter the search request; test captured first/second requests.
- Changed company and cancellation must not save/reuse prior research; test stage cancellation and evidence matching.
- Failure must not display an old report as current; test in-page stale/error notice and persisted old result.

### Task 1: ChatGPT effort

Files: src/domain/Types.h, src/app/AppSettings.cpp, src/ai/TextClient.cpp, src/ui/SettingsDialog.*, tests/tst_settings.cpp, tests/tst_textclient.cpp.

- [x] Write failing tests: missing setting defaults high; selecting medium/default persists; ChatGPT payload includes reasoning.effort without modifying model; other providers omit the field.
- [x] Run tests and confirm expected failures.
- [x] Add ProviderConfig.reasoningEffort, settings persistence and a chatgptReasoningEffort combo with separate model slug and values default/low/medium/high/xhigh/max.
- [x] Serialize reasoning only for ChatGPT, omitting it for explicit default.
- [x] Run affected tests.

### Task 2: Source-backed web transport

Files: src/ai/TextClient.*, src/preparation/Preparation.*, tests/tst_textclient.cpp, tests/tst_preparation.cpp.

- [x] Write failing tests for company_research tools/tool_choice/include and company-only input.
- [x] Write failing SSE tests: completed web_search_call plus sources accepted; no actual search, no sources, unsafe URLs, truncated responses and errors rejected.
- [x] Add Preparation::companyResearchRequest and research validation/render helpers; reject evidence for a different company.
- [x] Add TextClient::webSources(id,sources), requiring successful native search and sources before finishing research.
- [x] Run transport/preparation tests.

### Task 3: Two-stage preparation UI

Files: src/ui/PreparationDialog.*, tests/tst_preparation.cpp.

- [x] Write failing UI tests for online checkbox, stage privacy, saved sources, updated result, stale banner and failed/cancelled analysis.
- [x] Add checkbox and first-stage request; preserve the config/input snapshot across research and analysis, and keep inputs locked until both finish.
- [x] Render pending/old/failed state and known clickable sources; preserve previous analysis on error and cancel.
- [x] Keep explicit offline mode and a clear unsupported-provider error. Update existing offline fixture tests to choose offline mode explicitly.
- [x] Run preparation/window tests.

### Task 4: Deliver

- [x] Update README, acceptance evidence and version.
- [x] Build Release in the existing ASCII mirror; run the full test suite.
- [x] Obtain an independent read-only source review; fix important findings and rerun affected checks.
- [x] Produce a separate local release, verify package-only tests and ZIP contents/hash.
- [ ] Commit and push to the already-authorized repository; report actual limits without claiming authenticated live search succeeded.

## Execution record

- All implementation and test steps completed; full suite17/17,222checks; package-onlypreparation32/settings12/text17.
- Ruling: preserve direct implementation and existing checkout/repository authorization from the conversation; no extra approval round.
- Ruling: use current native ChatGPT/OpenAI web search; the existing DeepSeek adapter explicitly rejects research instead of pretending or changing providers.
- Review: one important provenance issue accepted. Pending research stays in requestInput; only successful analysis commits it. analysisResearch preserves the retained report references even after company edits. RED two cases, GREEN four failure/cancel/company-change cases plus reopening.
- Real-account web search and factual company accuracy remain user-environment validation, not offline-test claims.
