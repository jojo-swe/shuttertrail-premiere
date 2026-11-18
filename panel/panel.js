// SPDX-License-Identifier: MIT
(() => {
    'use strict';
    const fs = require('fs'), path = require('path'), os = require('os');
    const {spawn} = require('child_process');
    const {fileURLToPath} = require('url');
    const worker = path.join(path.dirname(fileURLToPath(location.href)), 'bin', 'MotionBlurWorker.exe');
    const el = id => document.getElementById(id);
    let child = null, cancelled = false, previewDir = '', images = null;
    const controls = ['render','preview','settings'];
    const busy = on => { for (const id of controls) el(id).disabled = on; };
    // Remember the last settings. Storage can be unavailable, so every
    // access is optional.
    const saved = key => { try { return localStorage.getItem('shuttertrail.'+key); } catch (_) { return null; } };
    const save = (key,value) => { try { localStorage.setItem('shuttertrail.'+key,value); } catch (_) {} };
    for (const id of ['angle','samples','strength']) {
        const input = el(id), value = saved(id);
        // Restore only a value the control can show.
        const valid = input.options
            ? [...input.options].some(o => o.value === value)
            : value !== null && /^\d+$/.test(value) && +value >= +input.min && +value <= +input.max
              && +value % +input.step === 0;
        if (valid) input.value = value;
        input.addEventListener('change', () => save(id, input.value));
    }
    const radios = [...document.querySelectorAll('input[name=position]')];
    for (const radio of radios) {
        if (radio.value === saved('position')) radio.checked = true;
        radio.addEventListener('change', () => save('position', radio.value));
    }
    const position = () => radios.find(r => r.checked).value;
    // Shutter phase for the worker, in degrees: where the open time sits around the frame.
    const phase = () => Math.round({behind:-1, centered:0, ahead:1}[position()] * el('angle').value / 2);
    // Keeps the shutter disc, the read-outs and the presets in step with the controls.
    function update() {
        const angle = +el('angle').value;
        el('degrees').textContent = angle + '°';
        el('percent').textContent = el('strength').value + '%';
        el('disc').style.setProperty('--open', angle + 'deg');
        el('disc').style.setProperty('--start', (phase() - angle/2) + 'deg');
        const open = angle === 0 ? 'Shutter off: no blur.'
                   : angle === 360 ? 'Open for the whole frame.'
                   : angle === 180 ? 'Open for half of each frame.'
                   : 'Open for ' + Math.round(angle/3.6) + '% of each frame.';
        el('exposure').textContent = open + (angle === 0 ? '' : {
            behind: ' The blur trails behind moving objects.',
            centered: ' The blur is centered on each frame.',
            ahead: ' The blur runs ahead of moving objects.'}[position()]);
        for (const b of document.querySelectorAll('[data-angle]'))
            b.setAttribute('aria-pressed', String(+b.dataset.angle === angle));
    }
    for (const b of document.querySelectorAll('[data-angle]'))
        b.addEventListener('click', () => {
            el('angle').value = b.dataset.angle;
            el('angle').dispatchEvent(new Event('change', {bubbles:true}));
        });
    // Any setting change redraws, and refreshes a preview that is on screen.
    let refresh = null, stale = false;
    function schedulePreview() {
        if (el('figure').hidden) return;
        clearTimeout(refresh);
        refresh = setTimeout(() => { if (child) stale = true; else el('preview').click(); }, 350);
    }
    el('settings').addEventListener('input', update);
    el('settings').addEventListener('change', () => { update(); schedulePreview(); });
    update();
    const shutter = () => [el('angle').value, el('samples').value, String(phase()), el('strength').value];
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
                if (cancelled) reject(Error('Cancelled.'));
                else if (code!==0) reject(Error(tail.trim() || 'Renderer exited with code '+code));
                else resolve();
            });
        });
    }
    function cancel() { cancelled=true; if (child) child.kill(); }
    function removePreview() {
        if (previewDir) { try { fs.rmSync(previewDir,{recursive:true,force:true}); } catch (_) {} }
        previewDir='';
    }
    el('cancel').onclick=cancel;
    window.addEventListener('beforeunload',() => { cancel(); removePreview(); });

    // Renders the frame under the playhead to two small images: blurred and original.
    el('preview').onclick=async () => {
        cancelled=false;
        busy(true);
        try {
            const clip=await host('MotionBlurTimeline.preview()');
            const [,source,frame,ticks,at,count,inside]=clip.fields;
            if (!fs.existsSync(worker)) throw Error('Renderer is missing. Run Install.cmd first.');
            removePreview();
            previewDir=fs.mkdtempSync(path.join(os.tmpdir(),'shuttertrail-preview-'));
            const base=path.join(previewDir,'frame'), angle=el('angle').value;
            el('cancel').hidden=false;
            el('status').textContent='Rendering preview …';
            await run(['--preview',source,base,frame,ticks,...shutter()]);
            const image=file => 'data:image/bmp;base64,'+fs.readFileSync(file).toString('base64');
            images={blurred:image(base+'.bmp'), original:image(base+'-original.bmp')};
            removePreview();
            el('image').src=images.blurred;
            el('figure').hidden=false;
            el('compare').hidden=false;
            el('caption').textContent='Clip frame '+(Number(at)+1)+' of '+count+' at '+angle+'°.'+
                (inside==='1' ? '' : ' The playhead is outside the clip, so the nearest clip frame is shown.');
            el('status').textContent='Ready.';
        } catch (error) {
            removePreview();
            el('status').textContent=error.message;
        } finally {
            el('cancel').hidden=true;
            busy(false);
            if (stale) { stale=false; schedulePreview(); }
        }
    };
    // Hold the button (mouse, touch or Space/Enter) to see the original frame.
    const show = which => { if (images) el('image').src=images[which]; };
    for (const [event,which] of [['pointerdown','original'],['pointerup','blurred'],['pointerleave','blurred'],
                                 ['keydown','original'],['keyup','blurred'],['blur','blurred']])
        el('compare').addEventListener(event, e => {
            if (e.type.startsWith('key') && e.key!==' ' && e.key!=='Enter') return;
            show(which);
        });
    el('render').onclick=async () => {
        let job='', completed='';
        cancelled=false;
        el('reveal').hidden=true;
        busy(true);
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
            await run([source,output,start,count,ticks,...shutter()]);
            completed=output;
            el('cancel').hidden=true;
            el('status').textContent='Importing and verifying placement …';
            // URI encoding keeps filenames separate from executable ExtendScript syntax.
            await host('MotionBlurTimeline.place('+JSON.stringify(clip.raw)+','+
                       JSON.stringify(encodeURIComponent(output))+')');
            el('status').textContent='Done on V'+target+'.\n'+output;
            el('reveal').hidden=false;
            el('reveal').onclick=() => {
                // Explorer needs the quotes inside its /select argument.
                spawn('explorer.exe',['/select,"'+output+'"'],{windowsVerbatimArguments:true}).on('error',() => {});
            };
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
            busy(false);
        }
    };
})();
