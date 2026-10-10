# Raw trackpad repair implementation plan

Goal: use the pixel delta observed in real Mac events for fine two-finger scrolling, and replace unavailable AppKit swipe events with raw three-finger contact recognition. User explicitly accepted private MultitouchSupport API compatibility tradeoff.

Architecture: keep Windows and network protocol unchanged. A dynamically loaded, isolated Mac source converts private 96-byte contacts into public C values; pure recognizer tracks three stable IDs and a bound foreground PID. Existing keyboard worker remains sole owner of navigation injection.

- [x] Reproduce pixel != 0, Cocoa.deltaY == 0 and accumulated line bursts in adapter tests; derive one line delta per pixel event at pixels/10 before existing gain. Verify positive/negative tiny input and no double counting of line bursts.
- [x] Add `src/touch_swipe.h` and `tests/test_touch_swipe.c`: exactly three fingers, centroid horizontal displacement >=12%, horizontal/vertical ratio >=1.8, >=3 frames and >=40ms. Four fingers, dominant vertical movement, finger ID changes, PID changes or invalid frames cancel until full release. One action per gesture.
- [x] Add `src/multitouch.h` and `src/multitouch_macos.m`: dynamic symbols, per-device state, contact states 3/4, coordinate validation, mutex-protected callback lifetime, stop/unregister before freeing, retain library until exit.
- [x] Replace AppKit monitor/pump with context and queue callbacks in keyboard adapter. Check authenticated connection and foreground PID; worker retains held-key/modifier/stale-request checks.
- [x] Update Mac build/test scripts; test startup API failure, partial start failure, conversion, idempotent stop using simulated private API. Run strict build, full sanitizer tests and whitespace check.
- [x] Install staged source after comparing snapshots; build locally. Rewrite docs around final implementation, keeping explicit hardware acceptance requirements. User handles Git commit.

Validation: `bash scripts/build_mac.sh`, `bash scripts/test_mac.sh`, `git diff --check`. Real contact delivery, raw-coordinate direction and Windows application scrolling remain user acceptance checks; no automated test injects real input or accesses actual clipboard.

Local installation verified: Mac executable rebuilt, all sanitizer regression tests passed, and git diff --check passed. Required private API symbols are available on this Mac. Hardware callbacks and Windows user experience remain unverified. No commit or push performed.
