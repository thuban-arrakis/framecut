# Trimming in Framecut 0.3

Framecut trims one selected clip by changing its source start and end times. It keeps the original file intact and reads that file again for export. Imported clips retain separate trim ranges and undo histories for the current application session. Project saving, clip splitting, joining clips, and removing a middle section are not included in this version.

## Timeline interaction

Select a video and drag either vertical handle on its timeline box. Moving the left edge inward removes the beginning; moving the right edge inward removes the ending. The video and audio boxes shrink together. Drag an edge outward to restore footage. The source ruler keeps its scale, and the subdued area behind the selection shows excluded footage. The excluded area does not become a blank section in playback or export.

Dragging pauses playback and shows the boundary frame in the preview. A tooltip reports the edited boundary and selected duration. Handle movements snap to the source's nominal frame rate, when known, and the range cannot collapse below one frame. **Escape** cancels the current drag. A completed drag is one undo operation.

The **Trim** button opens the inspector's **Trim** tab. Its **Start** and **End** fields accept source times in seconds to three decimal places. **Selected duration** shows the output length. **Reset trim** restores the whole source; **Edit → Undo trim / Redo trim** restores previous ranges. The standard undo/redo shortcuts are listed in **Keyboard shortcuts**. The app applies the selected range to preview playback and the seek controls; reaching its end pauses playback, and playing again starts at its beginning.

The preview can use a lower-resolution and lower-frame-rate copy to keep playback responsive and reduce CPU use. Export uses the original source and selected times, rather than exporting the preview copy.

## Export behavior

| Selection | Video output | Audio and other streams |
| --- | --- | --- |
| Full clip, remove audio | Original container and copied video streams; no re-encoding | All audio removed; non-audio streams and chapters retained |
| Trimmed range, remove audio | H.264 MP4 at the source's displayed dimensions | No audio; extra tracks, subtitles, attachments, and chapters excluded |
| Trimmed range, keep audio | H.264 MP4 at the source's displayed dimensions | First audio track encoded as AAC; other auxiliary streams excluded |

Trim export uses CPU `libx264`, CRF 18, the `fast` preset, and two encoding threads. Retained audio uses AAC at 192 kbit/s. Display rotation is applied to the pixels. Normal even-sized frames use 8-bit 4:2:0; odd-sized frames use 4:4:4 to preserve dimensions. This is high-quality encoding, not a lossless trim or preservation of the original codec/HDR format. Test representative HDR, unusual color formats, and variable-frame-rate sources before production use.

FFmpeg seeks to the input range with accurate seeking enabled and re-encodes the selected frames. Its documentation explains that stream copying can retain material before the requested time because seeking first lands at a nearby seek point; transcoding discards that earlier material. This is why Framecut uses re-encoding for precise cuts. [FFmpeg seeking documentation](https://www.ffmpeg.org/ffmpeg.html)

Exports are written to temporary staging files and inspected before the completed file appears at the destination. Original files and existing destinations are protected. Cancellation discards incomplete staging output. Trim export takes CPU time proportional to the selected footage and can be substantially slower than full-length audio removal.

## Design references

CapCut documents trimming a clip by dragging its edges. Framecut adopts that direct interaction in its existing timeline. [CapCut's edge-trimming example](https://www.capcut.com/resource/free-video-overlay-effects)

Shotcut describes separate in/out handles, restoring a clip's duration by moving its boundaries, and preview scrubbing during a trim. Framecut uses these basic ideas for one selected clip. Multiclip ripple and roll edits are outside this version's scope. [Shotcut trimming documentation](https://forum.shotcut.org/t/trimming-clips/49216)
