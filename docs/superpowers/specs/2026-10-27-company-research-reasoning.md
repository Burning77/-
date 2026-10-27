# Company research and ChatGPT reasoning

The user requests a reasoning selector defaulting to high, and online company verification before resume/job analysis. They selected direct implementation in this conversation. Existing screenshots now show successful replacement of the prior analysis; stale/failed results still need clear in-page status.

## Approach

Use the existing Responses transport and official hosted web_search for ChatGPT/OpenAI. Search only the company name, then make a separate analysis request containing the retrieved evidence plus the supplied JD/resume. Do not use brittle search-page scraping, invent a model ID suffix, switch accounts/providers, or send resume contents to web search. The current DeepSeek adapter retains offline preparation and explicitly declines online research.

## Behavior

- ChatGPT settings: server model slug unchanged; independent default/high/medium/low/xhigh/max selector, initially high. Persist the choice and send reasoning.effort on ChatGPT requests; default omits the parameter. Other providers keep existing reasoning behavior.
- Preparation: online company research checkbox, initially on. Research must return a completed native search call and real HTTP(S) source metadata before it can be accepted. No search activity or sources means failure, never an online success claim.
- Search only the exact company name; prefer official/government/reputable company sources, distinguish namesakes and uncertain facts. Do not send JD, resume, old analysis, or personal experience in that stage.
- Feed bounded research text and sources to the second, non-search preparation request, alongside the resume and JD. Save research company, sources, time and provider/model as part of preparation. Changed company or unchecked online mode must not silently reuse the prior company's research.
- Show pending, outdated and failed states inside the analysis tab. Successful analysis replaces the displayed/saved result. Keep old results and drafts on failure or cancellation; show clickable known source links and the retrieval time.
- Account/workspace policy may deny search. Surface that error and let the user explicitly disable online verification; never silently substitute a provider or claim verification.
- No real user credentials, resumes, native UI, or current recordings are accessed in development tests.

## Validation

Offline Qt fixtures cover serialized reasoning and persistence, settings UI selection, actual native search events/sources, missing/unsafe sources, privacy between stages, updated analysis and provenance, stale company data, failures, cancellation, and unsupported providers. Full suite, release build, package-only checks, independent source review and a separate versioned local package are required. Real account search remains user-environment validation.
