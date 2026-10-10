# System scroll lines implementation plan

Goal: preserve native Moonlight scrolling and map two-finger velocity linearly to 1–5 Windows wheel lines.

Architecture: Mac sends opt-in authenticated control messages only after Windows advertises support. Windows guard alone owns the global setting, remembers its exact baseline and restores on idle lease, parent exit, graceful shutdown or connection reset. Changes are session-only, not saved to the user profile. Only one guard may own the setting.

- [x] Test linear mapping, stability, finite input, protocol validation and restoration state transitions.
- [x] Replace scroll delta rewriting with raw two-finger speed sampling; preserve three-finger and keyboard behaviors.
- [x] Add capability + bounded line request protocol and periodic authenticated delivery.
- [x] Add optional Windows guard, with parent handle monitoring, shared-memory lease and baseline restoration.
- [x] Remove obsolete gain/rounding module and tests; rewrite usage and update both builds.
- [x] Build Mac, run regression tests; report Windows runtime verification limitation explicitly.

No commits or pushes: user handles Git. All staging installs verify original snapshots first.

Read-only review completed; no blocking issues found. Windows MSVC/runtime are unavailable on Mac, so actual Windows build and process-termination acceptance remain for the user on Windows.
