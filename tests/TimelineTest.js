// SPDX-License-Identifier: MIT
// Run the actual ExtendScript in a mock host. No Adobe modules are copied.
const fs=require('fs'), path=require('path'), vm=require('vm'), assert=require('assert');
const step=8475667200, time=n=>({ticks:String(n*step), seconds:n*1001/30000});
const list=items=>Object.assign(items,{numItems:items.length});
// The non-ASCII name and the '|' delimiter in the path exercise the URI-encoded fields.
const file='C:\\footage\\Åsa | shot.mp4', output='C:\\test\\render.mp4';
const item={nodeId:'src',isSequence:()=>false,isOffline:()=>false,getMediaPath:()=>file,
            getFootageInterpretation:()=>({frameRate:30000/1001})};
const clip={projectItem:item,nodeId:'c1',start:time(75),end:time(80),inPoint:time(14),outPoint:time(19),
            isSelected:()=>true,getSpeed:()=>1,isSpeedReversed:()=>false};
const imported={nodeId:'out',getOutPoint:()=>time(5),getInPoint:()=>time(0)};
let placed=0;
const emptyTrack=()=>({id:2,clips:list([]),transitions:list([]),isMuted:()=>false,isLocked:()=>false});
const target=emptyTrack();
target.overwriteClip=(p,t)=>{
    ++placed;
    assert.strictEqual(t,clip.start.ticks);
    target.clips=list([{projectItem:p,start:clip.start,end:clip.end}]);
};
const tracks=[{clips:list([clip]),transitions:list([])},target];
tracks.numTracks=2;
let playhead=time(77);
const seq={sequenceID:'seq',timebase:String(step),videoTracks:tracks,getPlayerPosition:()=>playhead};
const project={path:'C:\\edit.prproj',activeSequence:seq,getInsertionBin:()=>({
    findItemsMatchingMediaPath:(file)=>file===output?[imported]:[]}),importFiles:()=>true};
const host=vm.createContext({app:{project}});
vm.runInContext(fs.readFileSync(path.join(__dirname,'../panel/jsx/Timeline.jsx'),'utf8'),host);
const api=host.MotionBlurTimeline, snapshot=api.inspect();
assert.deepStrictEqual(snapshot.split('|').map(decodeURIComponent).slice(0,7),
                       ['OK',file,project.path,'14','5',String(step),'2']);
target.clips=list([{start:time(78),end:time(90)}]);
assert(api.place(snapshot,encodeURIComponent(output)).startsWith('ERR|'));
target.clips=list([]); target.transitions=list([{start:time(71),end:time(76)}]);
assert(api.inspect().startsWith('ERR|'));
target.transitions=list([]); target.isLocked=()=>true;
assert(api.inspect().startsWith('ERR|'));
target.isLocked=()=>false;
tracks.numTracks=1; assert(api.inspect().startsWith('ERR|')); tracks.numTracks=2;
seq.sequenceID='changed'; assert(api.place(snapshot,encodeURIComponent(output)).startsWith('ERR|'));
seq.sequenceID='seq'; assert.strictEqual(placed,0);
assert.strictEqual(api.place(snapshot,encodeURIComponent(output)),'OK');
assert.strictEqual(placed,1);
// Preview: the playhead two frames into the clip maps to source frame 16.
target.clips=list([]);
const fields=r=>r.split('|').map(decodeURIComponent);
assert.deepStrictEqual(fields(api.preview()),['OK',file,'16',String(step),'2','5','1']);
// Outside the clip the nearest clip frame is used and flagged.
playhead=time(90); assert.deepStrictEqual(fields(api.preview()).slice(2),['18',String(step),'4','5','0']);
playhead=time(10); assert.deepStrictEqual(fields(api.preview()).slice(2),['14',String(step),'0','5','0']);
// Preview needs neither a saved project nor a free track above.
const savedPath=project.path; project.path='';
target.clips=list([{start:time(70),end:time(90),isSelected:()=>false}]);
assert(fields(api.preview())[0]==='OK');
assert(api.inspect().startsWith('ERR|'));
project.path=savedPath; target.clips=list([]);
console.log('Timeline tests passed');
