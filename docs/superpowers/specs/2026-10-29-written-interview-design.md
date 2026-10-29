# Written interview design

## Accepted behavior

Add a main-window button that switches to a separate written-interview window. The main window is hidden without destruction so the same SessionController, recording/pause state, resume, role and history remain available. End/close returns to the main window. The written window contains a model selector and End button at the top, then a vertical question/answer split.

Alt+C copies the current external selection once and submits the fresh text automatically. Ctrl+Alt+A selects a region on the pointer's monitor and submits the original PNG directly to the selected AI; no local OCR is performed. Both shortcuts are configurable under Settings / Shortcuts and are registered only in written mode. Ordinary question editing is debounced; changing model regenerates the current text/image question.

## Data and lifecycle

The existing answer request includes the current session's resume and role. Screenshot metadata lives on the question; validated PNG assets live in sessions/<session>/questions/<question>.png, with ownership paths, bounded size and SHA256 validation before dispatch. Queued jobs hold metadata rather than duplicate image bytes. Session discard/deletion removes these assets. A text-only submission cannot reuse an image-backed question merely because its prompt matches.

Selected text is accepted only after a changed clipboard sequence and matching clipboard-owner/target process. A bounded helper process isolates delayed clipboard rendering. Foreground changes, changed ownership, cancellation and late callbacks do not submit a question.

## Windows behavior

The written window shares transparency, Windows capture exclusion and the boss key. Restoring via the boss key targets the current mode, not both main and written windows. Taskbar restoration is balanced across mode switches and destruction; callback lifetime guards prevent stale dereferences.

The region selector sets and reads back capture exclusion before showing any selection feedback. It draws all feedback in its own protected HWND. An explicit screenshot action may show this protected selector while the interview window remains boss-hidden; ordinary owned dialogs remain hidden. The exception is active only after protection succeeds and ends at selection/cancel.

Windows flags do not prove Sunlogin remote visibility. Verify the actual remote screen on two devices. No desktop capture, real clipboard access, actual interview audio or cloud account inference is part of the automated fixtures.

## Validation

Use synthetic PNGs, fake streaming replies, isolated settings/data and cloaked own windows. Cover mode round trips, recording/pause preservation, resume/image payloads for all provider adapters, late streams, screenshot cleanup, shortcut configuration/release, clipboard provenance and fail-closed selection protection. A native test verifies a protected selector remains visible locally while the boss-hidden interview windows stay hidden.
