# Code review — 2026-10-19

Baseline: `90fcf36`. Reviewed transfer/transport, identity and device management,
shared app workers, Qt drops, JNI boundaries, build/provisioning scripts, CI and
user/developer documentation. This is a scoped source review with focused
regressions, not a proof that every platform path is free of defects.

## Fixed

- **P1: duplicate QUIC streams could block teardown.** The connection callback
  destroyed a rejected stream whose destructor waits for a shutdown callback on
  that same worker. All incoming stream contexts now remain connection-owned
  until application-thread teardown. A localhost regression sends a second
  payload stream after the first has closed and checks receiver failure/cleanup.
- **P1: Qt move drops could cause source deletion.** Beam retained only the path
  but accepted the proposed move. Drag-enter and drop now require and negotiate
  CopyAction, rejecting move-only sources. Tests include modifier-selected moves.
- **P2: the Qt editor bypassed file-drop validation.** Its built-in handler could
  insert file URLs as text. Drops now propagate to the window's existing local
  file validation instead. The editor and its viewport both disable native drops.
- **P2: IPv4-mapped IPv6 routes were not canonicalized.** Unscoped mapped addresses
  now normalize to IPv4 before connection and deduplication. Unit tests cover
  mapped Tailscale aliases; an authenticated localhost transfer covers loopback.

These resolve findings 1, 2, 3 and 5 in the [earlier review](2026-10-13-code-review.md).
Wire formats, trust records, public APIs and UI layout are unchanged.

## Remaining limitation

**P2: Linux/X11 PRIMARY selection.** The stock Qt editor can publish selected text
and paste PRIMARY on middle-click. Explicit Beam Load/Copy controls use the regular
clipboard. The desktop guide now states this limitation instead of claiming no
PRIMARY use. A fix needs X11-backed tests and a deliberate editing policy; this
review leaves those interactions unchanged. Offscreen tests cannot verify it.

## Complexity and CI

The existing core/platform boundaries, joined workers, bounded parsing and explicit
provisioning are appropriate. Retain the ownership, transaction and filesystem
checks: they enforce failure recovery and safe cleanup. Avoid a broad rewrite of
Android tool selection or macOS lifecycle scripts, whose branches encode supported
platform and safety requirements.

Removed a redundant receive-state condition, a single-ABI test loop and duplicate
Make help. Shortened repeated Android setup and contributor-test instructions,
linked device management from onboarding, and corrected stale desktop Stop-control
and clipboard claims. Specialized protocol/security/platform documents retain
their own responsibilities rather than being merged into one long guide.

CI now caches Gradle downloads and pinned native dependencies and cancels
superseded runs for the same ref/event. Android install caches include selected
NDK/CMake versions and provisioning inputs; host caches include the runner image.
The source fallback cache is separate from system-runtime validation. Formatting,
static analysis, release packaging, emulator, security and live discovery checks
remain enabled. Warm-cache timing gains require observation in subsequent runs.

## Validation

- CLI/core Release: formatting, LLVM 23 static analysis, build, and all 25 tests
  passed. Six socket-dependent tests needed a rerun outside the sandbox.
- Native macOS Release: full `make check` passed, including formatting/static
  analysis, compilation, packaging/lifecycle and all 31 tests.
- New mapped-address and duplicate-stream localhost regressions passed.
- Changed shell scripts pass `bash -n`; workflow YAML parses; local documentation
  links and `git diff --check` pass.
- [CI run 39205887720](https://github.com/septimochen/beam/actions/runs/39205887720)
  passed for implementation commit `03a0366`: Linux LLVM 23 analysis, Qt UI and
  transfer tests, live Avahi, source-fallback installation/transfers, Android native
  dependency isolation, debug checks, release lint/packaging and emulator JNI/UI.
- Dependency cache uploads passed. Warm-cache restores and timing gains still need
  measurement in a subsequent run; no speedup percentage is claimed.
