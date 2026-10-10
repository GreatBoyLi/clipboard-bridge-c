> 历史方案：滚动改写部分已删除，由 `2026-10-10-system-scroll-lines.md` 替代；当前使用方法见 `docs/trackpad.md`。

# Two-finger velocity implementation plan

Goal: replace per-event magnitude gain with two-finger vertical centroid displacement/time; retain small deltas through Moonlight integer conversion and preserve native inertia. User approved this approach after hardware feedback.

- [x] Add pure `touch_motion.h` and `test_touch_motion.c`: two stable IDs, bound PID, monotonic timestamps, 150ms reset gap, normalized vertical speed and 25ms estimate smoothing. Same displacement over less time yields greater speed/gain.
- [x] In multitouch source, compute per-device two-finger motion and expose mutex-protected `cb_touch_speed(pid,speed)` snapshot, expiring at 100ms and clearing on other finger counts/invalid context. Tests cover PID and age rejection.
- [x] In keyboard adapter, use fresh touch speed; otherwise compute event pixel distance/time. For momentum reuse last physical-scroll gain. Enable raw listener for scroll even when three-finger navigation disabled, with independent navigation gate.
- [x] Quantize output in 1/120 line units while retaining remainder across events; clear on reverse/new gesture/context change. Encode away from zero in 16.16 to prevent a second truncation from losing the unit. No added event buffering or synthetic inertia.
- [x] Add integration tests for 100 tiny events through real Cocoa conversion and float/short path, and momentum gain. Full sanitizer suite passes in staging.
- [x] Install exact changed/new files after snapshot comparison, strict local build, full regression and whitespace checks. User handles Git commit.

Thresholds: slow <=0.08 trackpad heights/s has 0.12 gain; fast >=1 height/s reaches original gain. Intermediate smoothstep; gain <=1. Event fallback uses pixels/s at a 120Hz reference. Native macOS momentum timing remains unchanged. Windows applications may still respond in whole-line steps; this bridge cannot guarantee Mac pixel scrolling for all applications.

Local Mac build and complete sanitizer tests passed after installation. Windows experience remains a physical acceptance check. No commit or push.
