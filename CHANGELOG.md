# Changelog

## Unreleased (planned as 0.1.0-rc.2)

### New
- **Preview at playhead.** Renders the frame under the playhead in the selected clip within a second or two, exactly as the final file will look, at reduced size. Hold **Hold to compare with the original** to see it without blur.
- The panel remembers the last shutter angle and sample count, and shows an estimate of the time left while rendering.

### Better image
- **Fast motion is blurred.** The motion search now also works on half-, quarter- and eighth-resolution copies of the frame and finds motion of up to about 120 pixels per frame in 720p and larger footage. Before, anything beyond about 15 pixels per frame was not matched and got no blur at all.
- **Fewer double edges.** Vectors that disagree with the motion measured in the opposite direction lose weight. That typically happens where an object covers or uncovers the background.
- **Slow motion blurs by its real length.** Motion vectors are measured to a fraction of a pixel. Before, half a pixel of motion could round to zero and get no blur.
- **Even blur at the clip edges.** When the source has a frame just before or after the trim, the first and last frames use it and get the same blur as the rest.

### Faster
- Rendering uses all CPU cores. On a 4-core machine a 1080p frame takes about a third of the previous time.

### Changes
- The panel's extension ID is now `com.shuttertrail.premiere`. Remove an earlier install from `%APPDATA%\Adobe\CEP\extensions\se.jojoswe.motionblur`.
- The panel, its messages and the documentation are in English.

## 0.1.0-rc.1

First release candidate: a prebuilt Windows package of the CEP panel and the Media Foundation worker.
