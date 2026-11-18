# Handover

State on 2026-11-18, written at the end of a cloud session so that work can continue in a local session on the maintainer's Windows machine. Read `AGENTS.md` for how the code is organised and the rules to follow.

## Where things stand

- **`main` is green.** Windows CI (`windows.yml`) passes on every merged commit. Nothing is in flight: no open pull requests and no unpushed work.
- **Releases** (both are unpublished drafts, visible only to maintainers under *Releases*):
  - `v0.1.0-rc.2`: **the current candidate**, built from `main` at `11133df` (merge of #6). It contains everything listed below.
  - `v0.1.0-rc.1`: obsolete. It predates the engine work and can be deleted.
- `CHANGELOG.md` describes rc.2 under "Unreleased (planned as 0.1.0-rc.2)". Rename that heading when rc.2 is published.

## What was done in the cloud session

| PR | Change |
| --- | --- |
| #3 | README, UI and messages in English. Bundle ID renamed to `com.shuttertrail.premiere`. `release.yml` (draft pre-releases). Social preview image. `.cmd` line endings normalised. `.gitignore` extended. |
| #4 | `release.yml`: builds the tagged commit when the tag already exists, and never replaces assets on a published release. Both were Devin Review findings. |
| #5 | Engine: `parallelRows` (all cores, output identical for any thread count) and a hierarchical luma-pyramid search, so motion up to about 120 px/frame is found. Before, motion beyond ±15 px got no blur at all. |
| #6 | Forward-backward consistency check plus streaming `Exposer` (fewer double edges), handle frames at the trim edges, sub-pixel vectors, **Preview at playhead** (worker `--preview`, `Preview.hpp`, `Timeline.jsx` `preview()`), remembered panel settings, time-left estimate, `CHANGELOG.md`. |

Measured on synthetic frames in a 4-core container:

| | Before | After |
| --- | --- | --- |
| 1080p frame, 4 px motion | 0.98 s | 0.33 s |
| 40 / 90 px motion | not found (no blur) | found at 39 / 89 px, confidence 0.96 / 0.91 |
| Wrong-but-confident vectors around an occluding object | 12 tiles (confidence mass 5.5) | 4 tiles (2.1) |
| Worst error for 0.5 / 2.5 px motion | 0.50 px | 0.19 px |

## To do by hand (maintainer)

1. **Test rc.2 in Premiere.** Download the zip from the rc.2 draft, close Premiere, delete any old `%APPDATA%\Adobe\CEP\extensions\se.jojoswe.motionblur`, then run `Install.cmd`. Check:
   - **Preview at playhead** shows an image, the caption names the right clip frame, and **Hold to compare with the original** toggles it. This is untested in a real host.
   - A fast pan gets blur along the motion, and edges are clean around moving objects.
   - The first and last frames of a trimmed clip look as blurred as the middle.
   - Placement on the track above at 23.976, 25 and 29.97 fps. Colours match the source.
   - Render time on a real clip. The panel shows time left.
2. **Publish rc.2** when it's good, and delete the rc.1 draft.
3. **Upload the social preview.** Go to *Settings → General → Social preview* and upload `.github/social-preview.png`. There is no API for this.
4. **Look at Devin's flag on #5.** Devin Review reported one flag on PR #5 that is only visible in its app, at https://app.devin.ai/review/jojo-swe/shuttertrail-premiere/pull/5. Devin's later review of #6 found no issues.
5. Optional: delete the merged branches `claude/readme-release-setup-sobp9f` and `cursor/nonzero-pts-frame-mapping-113e` on GitHub.

## Known risks and things nobody has verified

- **Inside Premiere:**
  - `Sequence.getPlayerPosition()` and the frame mapping in `Timeline.jsx` `preview()` are only tested against a mock.
  - Showing a BMP `data:` URL in CEP's Chromium is only tested against a mock. The CSP allows `img-src data:`.
- **Real footage:** all quality numbers come from synthetic textures. The confidence formula (`1 - SAD/(36*28)`) and the `crossCheck` tolerance (`1.5 + 0.1·|v|` px, fading to zero over 3 px) have not been tuned on real clips.
- **Pure white noise:** the greedy step search can lock onto the wrong vector when motion isn't a multiple of its step. Natural images are smooth enough, but it shows up in synthetic tests that use per-pixel noise.
- **Host limits:** CEP is being replaced by UXP in Premiere, and the manifest range `[23.0,26.99]` is not a verified compatibility matrix. Installing sets `PlayerDebugMode` for CEP 11 and 12, because the panel is unsigned.

## Backlog (suggested order)

1. **Tune on real footage.** Add a small, licence-clean set of local test clips that are kept out of the repo, plus a script that renders them and saves before/after frames. Use it to tune the confidence and `crossCheck` constants.
2. **Linear-light blending** for brighter, more physical highlights in the blur. This is a luma-only approximation on Y′ with a LUT. Make it a panel option at first.
3. **Signing:** a signed ZXP for the panel removes the need for `PlayerDebugMode`, and Authenticode for `MotionBlurWorker.exe` avoids SmartScreen warnings. Both need certificates.
4. **UXP port** of the panel, for the long term. The worker and its command line can stay as they are.
5. **HEVC sources** through Media Foundation when the HEVC extension is installed. The reader currently accepts only H.264.
6. **Speed:** SIMD SAD, and reusing pyramid levels between fields (today every field rebuilds the pyramids of both its frames).
7. **macOS:** an AVFoundation version of `RenderClip.cpp`. The engine and `Preview.hpp` are already portable.

## Working locally

```
git checkout main
git pull
Build.cmd              :: needs Visual Studio 2022 Build Tools with the C++ workload
node tests\TimelineTest.js
Install.cmd            :: copies the panel and worker into the CEP extensions folder
```

- **Debugging the panel:** put a `.debug` file in the installed extension folder that maps `com.shuttertrail.premiere.panel` to a port, for example 8088. Restart Premiere, then open `http://localhost:8088` in Chrome for DevTools. Don't commit that file.
- **Reinstalling after a change** to `panel/` or the worker: close Premiere and run `Install.cmd` again. It rebuilds only when `build\MotionBlurWorker.exe` is missing, so run `Build.cmd` first after worker changes.
- **Claude Code** reads `CLAUDE.md`, which imports `AGENTS.md` and this file. Cursor, Codex and Devin read `AGENTS.md`.
