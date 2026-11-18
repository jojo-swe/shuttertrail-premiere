# ShutterTrail for Premiere Pro

<img src="./shuttertrail-avatar.png" alt="ShutterTrail" width="128" height="128">

A local Windows panel that adds motion blur to a trimmed clip in Adobe Premiere Pro. Select the clip, choose a shutter angle and render. The panel imports a silent MP4 into the same time range on the track directly above. You can compare the two by toggling the upper track on and off.

## Getting started on Windows 11 x64

The easy way does not need Visual Studio Build Tools.

1. Download `ShutterTrail-<version>-win-x64.zip` from [Releases](https://github.com/jojo-swe/shuttertrail-premiere/releases) and check it against the `.sha256` file next to it. Release candidates are marked as pre-releases. The same package is also available as the `shuttertrail-premiere-windows` artifact from any green run of the [Windows workflow](https://github.com/jojo-swe/shuttertrail-premiere/actions/workflows/windows.yml). The package contains `panel\`, `build\MotionBlurWorker.exe`, `Install.cmd`, `LICENSE` and `README.md`. It contains no test programs, test footage or logs.
2. Unpack the package to a writable folder and close Premiere. Double-click **Install.cmd**. If `build\MotionBlurWorker.exe` exists, it is used and **Build.cmd** does not run. If it is missing but `bin\MotionBlurWorker.exe` exists, that file is used instead. If neither exists, **Install.cmd** builds the worker with **Build.cmd** before installing. On Windows N editions, the **Media Feature Pack** is also required.
3. Start a Premiere version with CEP 11 or 12. Open **Window → Extensions → Motion Blur**. The target is Premiere Pro 23–26 on hosts where CEP is still available. The version range in the manifest is not a verified compatibility matrix. Adobe has moved to UXP for new development.
4. Save the Premiere project. Put a short, progressive SDR H.264 clip with a constant frame rate on V1 in a sequence with the same frame rate. Create V2 if needed. Trimming off the first second is a good test. Leave the range on V2 empty and unlocked.
5. Select the clip and start with **180° and 8 samples**. Click **Render to track above**. The first result is saved under `Motion Blur Renders` next to the project file. That folder is part of the project's media when you move or archive the project.

Build from source only when no prebuilt worker is included. Install **Visual Studio 2022 Build Tools** with the **Desktop development with C++** workload, the x64 compiler and a current Windows 10 or 11 SDK. Unpack the source tree and remove any `build\MotionBlurWorker.exe` or `bin\MotionBlurWorker.exe`, otherwise that file is installed without a rebuild. Then run **Install.cmd**. The script calls **Build.cmd**, which links the worker with the static C/C++ runtime (`/MT`) and runs both the image test and a real H.264 test before installing. The finished worker does not need the Microsoft Visual C++ Redistributable.

Installation copies the panel to `%APPDATA%\Adobe\CEP\extensions\com.shuttertrail.premiere` and enables developer mode for unsigned panels through the user's `PlayerDebugMode` in `HKCU\Software\Adobe\CSXS.11` and `CSXS.12`. This setting also applies to other CEP panels for the same user. To uninstall, close Premiere and delete the installed panel folder. Restore the registry values to their previous state if you do not need them for other panels.

Build tools: https://visualstudio.microsoft.com/visual-cpp-build-tools/  
Premiere panel support: https://github.com/Adobe-CEP/Samples/tree/master/PProPanel

## Image output and limitations

- **Shutter angle.** 180° simulates exposure during half a frame period, equally before and after the frame's centre. At 25 fps this corresponds to 1/50 s. 0° gives a reference with the same H.264 re-encode. Blur already baked into the source remains.
- **4, 8 or 16 samples.** More time points give a smoother blend and take longer. The render file uses the sequence frame rate, including 24000/1001, 30000/1001 and 60000/1001. The engine works frame by frame in the background.
- **Method.** The engine searches for local motion in both time directions with block matching on luminance, estimates intermediate images at the sample times and averages them. Weak matches are weighted down. The first and last frames only use images inside the trim. The hard-cut detector compares luminance histograms and can miss cuts between similar-looking shots. Fast motion, repeating patterns and occluding objects can produce double edges. Processing runs on the CPU in the video's encoded colour space.
- **Source material.** This version is meant for ordinary local H.264 clips in MP4, 8-bit SDR, square pixels, one video stream, 100 % playback speed and the same frame rate as the sequence. Rendering writes H.264 High, 4:2:0, without audio. The local Windows decoder and encoder determine which frame sizes are supported.
- **Premiere effects.** The panel reads the original file's image before Lumetri, Motion/Scale, masks, opacity, stabilisation and other clip effects. Use unprocessed clips, or bake in the effects that should be visible first. Speed ramps, subclips with a different timebase, interpreted footage, nested sequences, image sequences, VFR, HDR, rotated phone clips and multicam are not supported.
- **Placement.** Keep the clip selected during the job. The panel checks the sequence, clip, trim and free range again before placing the result. There is no fallback placement on the source track. If placement fails, the path to the finished movie is shown so you can use it manually.

## License and contents

All project-specific source code, scripts, tests and documentation in this repository are released under MIT. The license text is in [LICENSE](LICENSE). The repository contains no imported code libraries, trained weights, checked-in binaries, fonts, test clips or SDK files. The test movie is generated locally by the test. The Windows and release workflows publish an install package containing the compiled worker.

| Part | Origin and distribution |
| --- | --- |
| Panel, motion estimation, renderer, tests | Own code in this repository, MIT |
| Premiere Pro and CEP | Installed Adobe host with its own terms |
| Media Foundation and H.264 codec | Windows system components with their own terms |
| MSVC, standard library, Windows SDK, WRL | Build tools and platform interfaces, only needed when compiling the worker from source. `MotionBlurWorker.exe` is linked with `/MT`, so the C/C++ runtime is inside the executable and the target machine does not need the Microsoft Visual C++ Redistributable. Microsoft's runtime is not MIT and is not included as source in the repository; it ships inside the built executable under the Visual Studio Build Tools terms. The project's own code is MIT |
| FFmpeg, RIFE, ncnn, Blur and Flowframes | Not used |

MIT covers the material in this repository. Adobe and Microsoft provide host applications, tools and system components separately. The install package contains the panel, `Install.cmd`, `LICENSE`, `README.md` and `build\MotionBlurWorker.exe` built with `/MT`. It contains no FFmpeg, RIFE, ncnn, model weights or other third-party libraries. Media Foundation and the H.264 codec come from Windows.

## Development and verification

- **Build.cmd** only builds inside `build/` and runs the Windows tests. It does not install any panel. The worker and test programs are linked with `/MT`. The Windows test creates a 60-frame H.264 video at 30000/1001 fps, renders nine frames starting at source frame 37, decodes the result and checks image content, timestamps and frame count.
- **Install.cmd** copies the panel to the CEP folder and `MotionBlurWorker.exe` to the panel's `bin\`. A prebuilt worker in `build\` is used if present, otherwise `bin\MotionBlurWorker.exe` if present. If neither exists, **Build.cmd** runs first.
- **tests/MotionBlurTest.cpp** tests a static image, known motion, two exposure lengths, 0°, neutral chroma, a cut boundary and a single frame. On Linux/macOS it can be run with `c++ -std=c++17 -O2 tests/MotionBlurTest.cpp -o /tmp/motion-test && /tmp/motion-test`.
- **tests/TimelineTest.js** runs the ExtendScript code against a simulated host. Run `node tests/TimelineTest.js`. The test also checks trim, missing or locked target tracks, overlap and a changed sequence.
- GitHub Actions runs the Windows build and tests on every push. When both pass, the install package is uploaded as the `shuttertrail-premiere-windows` artifact. Host integration must also be tried in the Premiere version you intend to support. Pay particular attention to in/out points when trimming, image colour and placement at 29.97 fps.

### Releases

`.github/workflows/release.yml` builds and tests the package, then attaches the zip and its SHA-256 checksum to a **draft pre-release**. Start it by pushing a tag such as `v0.1.0-rc.1`, or run the workflow manually with a tag name. Nothing becomes public until a maintainer reviews the draft and publishes it.

The panel UI lives in `panel/`, the timeline calls in `panel/jsx/Timeline.jsx`, the motion estimation in `worker/MotionBlur.hpp` and the Windows integration in `worker/RenderClip.cpp`.
