# Written input fixes (0.8.1)

User reports: keep the software above other windows; Alt+C produces no visible result; a second screenshot becomes visible only after resizing/minimizing.

Investigation and scope:
- Both primary windows lacked the topmost flag. The native Windows regression returned42 before the fix and0 afterward, using cloaked synthetic windows.
- The screenshot preview was submitted to the model before its new frame painted. The native pre-submit paint regression returned43 before the fix. Commit the frame synchronously before dispatch, without resizing or recreating the HWND.
- The clipboard helper required the sequence to remain unchanged across GetClipboardData. Windows permits delayed rendering during that call, so a valid read can advance the sequence. Return the final locked sequence with the text, keep clipboard ownership pinned, and validate that sequence/owner/foreground at acceptance. Short clipboard contention retries only the read, within the original deadline; Ctrl+C is sent once.
- Input failure/status text was overwritten by periodic old-answer status. Deduplicate unchanged answer status and display per-key registration status independently of AI generation.

Alt+C clarification: user reports no hint at all. Read only shortcut configuration and app paths; both Alt+C and Ctrl+Alt+A currently returned Win32 1409 on a temporary availability probe. That proves they are registered somewhere, not which process owns them. Posted WM_HOTKEY-to-selection delivery passes with a synthetic selection backend. Do not claim the specific live application failure was fully reproduced; provide persistent registration/read diagnostics for field verification.

Validation:
- Delayed same-owner rendering, bounded retry without another copy, owner changes and fresh sequence checks.
- Native Alt+C message dispatch into the real WrittenInput flow using synthetic clipboard data.
- Persistent key-conflict message after answer status changes.
- Preview paint before automatic dispatch, topmost style, unchanged capture exclusion/boss-key lifecycle.
- Final regression and complete package checks; retain 0.8.0 and all user data.

Final review closure: input diagnostics now have priority across old answer-state transitions and reset when a question is explicitly committed. The Running -> copy failure -> Completed -> new question regression observed failure before the fix.
