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
    function capture() {
        var project=app.project, seq=project.activeSequence;
        if (!seq || !project.path) fail("Öppna en sekvens och spara projektet först.");
        var clip=null, index=-1;
        for (var t=0; t<seq.videoTracks.numTracks; ++t) {
            var items=seq.videoTracks[t].clips;
            for (var i=0; i<items.numItems; ++i) if (items[i].isSelected()) {
                if (clip) fail("Markera exakt ett videoklipp.");
                clip=items[i]; index=t;
            }
        }
        if (!clip) fail("Markera ett videoklipp på tidslinjen.");
        var item=clip.projectItem;
        if (!item || item.isSequence() || item.isOffline() || !item.getMediaPath())
            fail("Välj ett lokalt videoklipp med tillgängligt originalmedia.");
        if (clip.getSpeed()!==1 || clip.isSpeedReversed() || clip.disabled)
            fail("Klippet måste vara aktivt och spelas framåt i 100 %.");
        if (index+1>=seq.videoTracks.numTracks) fail("Skapa ett videospår direkt ovanför klippet.");
        var target=seq.videoTracks[index+1];
        if ((target.isLocked && target.isLocked()) || target.isMuted())
            fail("Lås upp och aktivera spåret ovanför.");
        if (overlaps(target.clips,clip) || overlaps(target.transitions,clip))
            fail("Intervallet på videospåret ovanför är upptaget.");
        if (overlaps(seq.videoTracks[index].transitions,clip))
            fail("Klippet har en övergång. Rendera ett fristående klipp först.");
        var step=Number(seq.timebase);
        function frame(time) {
            var n=Number(time.ticks)/step, rounded=Math.round(n);
            if (Math.abs(n-rounded)>0.00001) fail("Klippgränsen ligger mellan sekvensens bildrutor.");
            return rounded;
        }
        var start=frame(clip.inPoint), count=frame(clip.end)-frame(clip.start);
        if (start<0 || count<1 || frame(clip.outPoint)-start!==count)
            fail("Källans trimning och sekvensens längd måste motsvara varandra.");
        var info=item.getFootageInterpretation();
        if (info && info.frameRate>0 && Math.abs(info.frameRate-254016000000/step)>0.002)
            fail("Klippet och sekvensen måste ha samma bildfrekvens.");
        var state=reply(["OK",item.getMediaPath(),project.path,start,count,step,index+2,
                         seq.sequenceID,clip.nodeId,item.nodeId,clip.start.ticks,clip.end.ticks,target.id]);
        return {state:state, project:project, clip:clip, target:target};
    }
    return {
        inspect:function () {
            try { return capture().state; } catch (e) { return reply(["ERR",e.message]); }
        },
        place:function (expected, encodedFile) {
            try {
                var now=capture();
                if (now.state!==expected) fail("Klippet, markeringen eller sekvensen ändrades under renderingen.");
                var file=decodeURIComponent(encodedFile), bin=now.project.getInsertionBin();
                if (!now.project.importFiles([file],true,bin,false)) fail("Premiere kunde inte importera MP4-filen.");
                var found=bin.findItemsMatchingMediaPath(file,true);
                if (!found || !found.length) fail("Det importerade projektobjektet kunde inte hittas.");
                var imported=found[0], duration=now.clip.end.seconds-now.clip.start.seconds;
                var importedDuration=imported.getOutPoint(1).seconds-imported.getInPoint(1).seconds;
                var step=Number(now.project.activeSequence.timebase)/254016000000;
                if (Math.abs(importedDuration-duration)>step*0.1)
                    fail("Premiere tolkar renderfilens längd annorlunda. Filen finns i projektpanelen.");
                // Import may trigger host updates. Repeat all timeline checks.
                now=capture();
                if (now.state!==expected) fail("Tidslinjen ändrades vid importen.");
                now.target.overwriteClip(imported,now.clip.start.ticks);
                var clips=now.target.clips;
                for (var i=0; i<clips.numItems; ++i) {
                    if (clips[i].projectItem && clips[i].projectItem.nodeId===imported.nodeId &&
                        clips[i].start.ticks===now.clip.start.ticks && clips[i].end.ticks===now.clip.end.ticks)
                        return "OK";
                }
                fail("Placeringen kunde inte verifieras. Kontrollera spåret och dess låsning.");
            } catch (e) { return reply(["ERR",e.message]); }
        }
    };
}());
