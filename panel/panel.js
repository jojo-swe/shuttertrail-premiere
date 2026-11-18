// SPDX-License-Identifier: MIT
(() => {
    'use strict';
    const fs = require('fs'), path = require('path');
    const {spawn} = require('child_process');
    const {fileURLToPath} = require('url');
    const worker = path.join(path.dirname(fileURLToPath(location.href)), 'bin', 'MotionBlurWorker.exe');
    const el = id => document.getElementById(id);
    let child = null, cancelled = false;
    // Remember the last shutter angle and sample count. Storage can be
    // unavailable, so every access is optional.
    const saved = key => { try { return localStorage.getItem('shuttertrail.'+key); } catch (_) { return null; } };
    const save = (key,value) => { try { localStorage.setItem('shuttertrail.'+key,value); } catch (_) {} };
    for (const id of ['angle','samples']) {
        const input = el(id), value = saved(id);
        // Restore only a value the control can show.
        const valid = input.options
            ? [...input.options].some(o => o.value === value)
            : value !== null && /^\d+$/.test(value) && +value >= +input.min && +value <= +input.max
              && +value % +input.step === 0;
        if (valid) input.value = value;
        input.addEventListener('change', () => save(id, input.value));
    }
    el('angle').oninput = () => { el('degrees').textContent = el('angle').value + '°'; };
    el('angle').oninput();
    function host(script) {
        return new Promise((resolve,reject) => window.__adobe_cep__.evalScript(script, result => {
            try {
                const fields = String(result).split('|').map(decodeURIComponent);
                if (fields[0] !== 'OK') throw Error(fields[1] || 'Premiere could not complete the timeline call.');
                resolve({raw:result, fields});
            } catch (e) { reject(e); }
        }));
    }
    function run(args) {
        return new Promise((resolve,reject) => {
            child = spawn(worker,args,{windowsHide:true,shell:false});
            let tail='', line='', started=null;
            child.stderr.on('data',data => { tail=(tail+data.toString()).slice(-4096); });
            child.stdout.on('data',data => {
                const lines=(line+data.toString()).split('\n');
                line=lines.pop();
                for (const msg of lines) {
                    const m=/^(\d+)\/(\d+)\s*$/.exec(msg);
                    if (m) {
                        const done=Number(m[1]), total=Number(m[2]);
                        el('progress').value = done/total;
                        let text = 'Rendering frame '+done+' of '+total+'.';
                        // Estimate from the frames since the first report, which
                        // excludes opening the file and seeking.
                        if (!started) started = {time: Date.now(), done};
                        else if (done > started.done) {
                            const left = (Date.now()-started.time)/(done-started.done)*(total-done)/1000;
                            text += left < 60 ? ' About '+Math.max(1,Math.round(left))+' s left.'
                                              : ' About '+Math.round(left/60)+' min left.';
                        }
                        el('status').textContent = text;
                    }
                }
            });
            child.on('error',reject);
            child.on('close',code => {
                child=null;
                if (cancelled) reject(Error('Render cancelled.'));
                else if (code!==0) reject(Error(tail.trim() || 'Renderer exited with code '+code));
                else resolve();
            });
        });
    }
    function cancel() { cancelled=true; if (child) child.kill(); }
    el('cancel').onclick=cancel;
    window.addEventListener('beforeunload',cancel);
    el('render').onclick=async () => {
        let job='', completed='';
        cancelled=false;
        for (const id of ['render','samples','angle']) el(id).disabled=true;
        try {
            const clip=await host('MotionBlurTimeline.inspect()');
            const [,source,project,start,count,ticks,target]=clip.fields;
            if (!fs.existsSync(worker)) throw Error('Renderer is missing. Run Install.cmd first.');
            const dir=path.join(path.dirname(project),'Motion Blur Renders');
            fs.mkdirSync(dir,{recursive:true});
            // Each job owns its own folder, including simultaneous renders in other projects.
            job=fs.mkdtempSync(path.join(dir,'render-'));
            const output=path.join(job,path.parse(source).name+'_'+el('angle').value+'deg.mp4');
            el('cancel').hidden=false;
            el('progress').hidden=false;
            el('progress').removeAttribute('value');
            el('status').textContent='Opening the source clip …';
            await run([source,output,start,count,ticks,el('angle').value,el('samples').value]);
            completed=output;
            el('cancel').hidden=true;
            el('status').textContent='Importing and verifying placement …';
            // URI encoding keeps filenames separate from executable ExtendScript syntax.
            await host('MotionBlurTimeline.place('+JSON.stringify(clip.raw)+','+
                       JSON.stringify(encodeURIComponent(output))+')');
            el('status').textContent='Done on V'+target+'.\n'+output;
        } catch (error) {
            let message=error.message;
            if (completed) message+='\nRender file: '+completed;
            else if (job) {
                try { fs.rmSync(job,{recursive:true,force:true}); }
                catch (_) { message+='\nWorking folder: '+job; }
            }
            el('status').textContent=message;
        } finally {
            el('progress').hidden=true; el('cancel').hidden=true;
            for (const id of ['render','samples','angle']) el(id).disabled=false;
        }
    };
})();
