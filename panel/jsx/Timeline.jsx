// SPDX-License-Identifier: MIT
// ExtendScript runs inside Premiere. Its older JS engine does not supply JSON
// in all hosts, so return URI-encoded fields separated by an ASCII delimiter.
var MotionBlurTimeline = (function () {
    function fail(message) { throw Error(message); }
    function reply(fields) {
        var encoded = [];
        for (var i=0; i<fields.length; ++i) encoded.push(encodeURIComponent(String(fields[i])));
        return encoded.join("|");
    }
    function overlaps(items, clip) {
        for (var i=0; i<items.numItems; ++i) {
            if (items[i].start.seconds < clip.end.seconds && items[i].end.seconds > clip.start.seconds) return true;
        }
        return false;
    }
    // The selected clip and its source trim. Shared by rendering and preview.
    function selection() {
        var project=app.project, seq=project.activeSequence;
        if (!seq) fail("Open a sequence first.");
        var clip=null, index=-1;
        for (var t=0; t<seq.videoTracks.numTracks; ++t) {
            var items=seq.videoTracks[t].clips;
            for (var i=0; i<items.numItems; ++i) if (items[i].isSelected()) {
                if (clip) fail("Select exactly one video clip.");
                clip=items[i]; index=t;
            }
        }
        if (!clip) fail("Select a video clip in the timeline.");
        var item=clip.projectItem;
        if (!item || item.isSequence() || item.isOffline() || !item.getMediaPath())
            fail("Choose a local video clip whose original media is available.");
        if (clip.getSpeed()!==1 || clip.isSpeedReversed() || clip.disabled)
            fail("The clip must be enabled and play forward at 100 %.");
        var step=Number(seq.timebase);
        function frame(time) {
            var n=Number(time.ticks)/step, rounded=Math.round(n);
            if (Math.abs(n-rounded)>0.00001) fail("The clip boundary falls between sequence frames.");
            return rounded;
        }
        var start=frame(clip.inPoint), count=frame(clip.end)-frame(clip.start);
        if (start<0 || count<1 || frame(clip.outPoint)-start!==count)
            fail("The source trim and the sequence duration must match.");
        var info=item.getFootageInterpretation();
        if (info && info.frameRate>0 && Math.abs(info.frameRate-254016000000/step)>0.002)
            fail("The clip and the sequence must have the same frame rate.");
        return {project:project, seq:seq, clip:clip, index:index, item:item, step:step, start:start, count:count};
    }
    function capture() {
        var s=selection(), project=s.project, seq=s.seq, clip=s.clip, index=s.index, item=s.item;
        var step=s.step, start=s.start, count=s.count;
        if (!project.path) fail("Save the project first.");
        if (index+1>=seq.videoTracks.numTracks) fail("Create a video track directly above the clip.");
        var target=seq.videoTracks[index+1];
        if ((target.isLocked && target.isLocked()) || target.isMuted())
            fail("Unlock and enable the track above.");
        if (overlaps(target.clips,clip) || overlaps(target.transitions,clip))
            fail("The range on the video track above is occupied.");
        if (overlaps(seq.videoTracks[index].transitions,clip))
            fail("The clip has a transition. Render a standalone clip first.");
        var state=reply(["OK",item.getMediaPath(),project.path,start,count,step,index+2,
                         seq.sequenceID,clip.nodeId,item.nodeId,clip.start.ticks,clip.end.ticks,target.id]);
        return {state:state, project:project, clip:clip, target:target};
    }
    return {
        // The source frame under the playhead in the selected clip. Outside the
        // clip, the nearest clip frame is used and the last field is 0.
        preview:function () {
            try {
                var s=selection(), position=s.seq.getPlayerPosition();
                var at=Math.floor((Number(position.ticks)-Number(s.clip.start.ticks))/s.step+0.00001);
                var inside=at>=0 && at<s.count;
                at=Math.max(0,Math.min(s.count-1,at));
                return reply(["OK",s.item.getMediaPath(),s.start+at,s.step,at,s.count,inside?1:0]);
            } catch (e) { return reply(["ERR",e.message]); }
        },
        inspect:function () {
            try { return capture().state; } catch (e) { return reply(["ERR",e.message]); }
        },
        place:function (expected, encodedFile) {
            try {
                var now=capture();
                if (now.state!==expected) fail("The clip, selection or sequence changed during the render.");
                var file=decodeURIComponent(encodedFile), bin=now.project.getInsertionBin();
                if (!now.project.importFiles([file],true,bin,false)) fail("Premiere could not import the MP4 file.");
                var found=bin.findItemsMatchingMediaPath(file,true);
                if (!found || !found.length) fail("The imported project item could not be found.");
                var imported=found[0], duration=now.clip.end.seconds-now.clip.start.seconds;
                var importedDuration=imported.getOutPoint(1).seconds-imported.getInPoint(1).seconds;
                var step=Number(now.project.activeSequence.timebase)/254016000000;
                if (Math.abs(importedDuration-duration)>step*0.1)
                    fail("Premiere interprets the render file's duration differently. The file is in the Project panel.");
                // Import may trigger host updates. Repeat all timeline checks.
                now=capture();
                if (now.state!==expected) fail("The timeline changed during import.");
                now.target.overwriteClip(imported,now.clip.start.ticks);
                var clips=now.target.clips;
                for (var i=0; i<clips.numItems; ++i) {
                    if (clips[i].projectItem && clips[i].projectItem.nodeId===imported.nodeId &&
                        clips[i].start.ticks===now.clip.start.ticks && clips[i].end.ticks===now.clip.end.ticks)
                        return "OK";
                }
                fail("Placement could not be verified. Check the track and its lock state.");
            } catch (e) { return reply(["ERR",e.message]); }
        }
    };
}());
