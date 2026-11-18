# Agent guide: ShutterTrail for Premiere Pro

A Windows CEP panel for Adobe Premiere Pro that adds motion-compensated shutter blur to a trimmed clip. A native worker (C++17, Media Foundation) decodes the clip, estimates motion, blends exposure samples and writes a silent H.264 MP4. The panel then places it on the track above. Read `HANDOVER.md` for the current state and the open work.

## Layout

| Path | What it is |
| --- | --- |
| `worker/MotionBlur.hpp` | The engine. Header-only, portable C++17, no dependencies. It contains the luma pyramid, block matching, sub-pixel refinement, the forward-backward check (`crossCheck`), `blend`, the streaming `Exposer` and `parallelRows`. |
| `worker/Preview.hpp` | NV12 to BMP conversion for the panel preview (BT.709/601, limited/full range, downscale). Portable. |
| `worker/RenderClip.cpp` | Windows only. Media Foundation reader and writer, `process()` (decode with handle frames, then `Exposer`), `render()` (MP4) and `renderPreview()` (BMP). |
| `panel/` | The CEP panel: `index.html`, `panel.js` (Node is enabled through `--enable-nodejs --mixed-context`), `panel.css`, `CSXS/manifest.xml`. |
| `panel/jsx/Timeline.jsx` | ExtendScript that runs inside Premiere: selection checks, `inspect()`, `place()`, `preview()`. |
| `tests/` | `MotionBlurTest.cpp` and `PreviewTest.cpp` (portable), `MediaFoundationTest.cpp` (Windows), `TimelineTest.js` (Node with a mock host). |
| `Build.cmd`, `Install.cmd` | Windows build plus tests, and install into `%APPDATA%\Adobe\CEP\extensions\com.shuttertrail.premiere`. |
| `.github/workflows/` | `windows.yml` (build and test on every push, uploads the install package) and `release.yml` (draft pre-release). |

## Build and test

Windows, with Visual Studio 2022 Build Tools, the C++ workload and a Windows SDK:

```
Build.cmd                     :: builds into build\ and runs every C++ test including Media Foundation
node tests\TimelineTest.js
```

Any OS, for fast checks of the portable parts:

```
c++ -std=c++17 -O2 -pthread tests/MotionBlurTest.cpp -o /tmp/mt && /tmp/mt
c++ -std=c++17 -O2 -pthread tests/PreviewTest.cpp -o /tmp/pt && /tmp/pt
node tests/TimelineTest.js
```

For any change to `MotionBlur.hpp` or `Preview.hpp`, also run the tests under `-fsanitize=thread` and `-fsanitize=address,undefined` (g++ or clang++), and build with `-Wall -Wextra -Wshadow`. MSVC builds with `/W4`.

## Rules that keep the project working

- **The engine output must not depend on the thread count.** `parallelRows` gives each row only its own output. A test compares a threaded run with `threadLimit()=1`. Keep that test passing.
- **Frames smaller than 128×128 use exactly the single-level search.** The existing tests rely on this, and the pyramid keeps the old search as a fallback candidate at every level.
- **`Exposer` contract:** each call's `cur` and `next` are the next call's `prev` and `cur`. Pass the same object as `prev` or `next` to clamp at an edge, because `Field` treats identical addresses as "no motion". A test checks that streaming equals independent `expose()` calls.
- **Frame layout:** `Frame` is tightly packed NV12, a luma plane followed by interleaved half-size UV.
- **`Timeline.jsx` is ExtendScript (ES3).** Don't use `let`, `const`, arrow functions, `JSON` or template strings. Replies are URI-encoded fields joined with `|`, starting with `OK` or `ERR`.
- **Interfaces that change together:**
  - The worker command line, used by `panel.js` and by `tests/MediaFoundationTest.cpp`:
    - `input output.mp4 start count ticksPerFrame angle samples [phase strength]`
    - `--preview input outputBase frame ticksPerFrame angle samples [phase strength]`
    - `phase` is -180..180 degrees (0 centred, -angle/2 trailing), `strength` 0..100 %. Both or neither.
  - The field order of `inspect()` and `preview()`, used by `panel.js`. `place()` compares the whole `inspect()` state string.
- **`.cmd` files are CRLF** through `.gitattributes`. Edit them so they stay CRLF.
- **English only** in the UI, messages, comments and docs.
- **License hygiene:** everything is MIT and the repo has no third-party code, binaries, fonts, models or test footage. Tests generate their own media. Keep it that way, or update the license section in `README.md`.
- **Never commit** `build/`, `package/`, `dist/`, `*.exe`, renders or `.recall/`. These are already in `.gitignore`.

## Expectations for changes

- For a behaviour change or bug fix, add a test that fails before the change and passes after it. Check that it really fails.
- For an engine change, report what it measurably does: before/after numbers on synthetic frames, and per-frame cost at 1080p.
- Update `README.md` (user-facing behaviour, the test list) and `CHANGELOG.md` (the "Unreleased" section) in the same change.
- Windows-only code can't be compiled outside Windows. Say so when you haven't run it, and rely on CI (`windows.yml`) before merging.

## Releases

`release.yml` builds and tests on `windows-2022`, then attaches `ShutterTrail-<tag>-win-x64.zip` and its `.sha256` to a **draft pre-release**. You start it by pushing a `v*` tag or with **Run workflow** and a tag name. For an existing tag it builds the tagged commit. It refuses to touch a release that is already published. A maintainer reviews and publishes the draft by hand. Keep `ExtensionBundleVersion` in the manifest in step with the release you are preparing.
