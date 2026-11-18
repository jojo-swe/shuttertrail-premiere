// SPDX-License-Identifier: MIT
(() => {
    'use strict';
    const fs = require('fs'), path = require('path');
    const {spawn} = require('child_process');
    const {fileURLToPath} = require('url');
    const worker = path.join(path.dirname(fileURLToPath(location.href)), 'bin', 'MotionBlurWorker.exe');
    const el = id => document.getElementById(id);
    let child = null, cancelled = false;
    el('angle').oninput = () => { el('degrees').textContent = el('angle').value + '°'; };
    function host(script) {
        return new Promise((resolve,reject) => window.__adobe_cep__.evalScript(script, result => {
            try {
                const fields = String(result).split('|').map(decodeURIComponent);
                if (fields[0] !== 'OK') throw Error(fields[1] || 'Premiere kunde inte utföra tidslinjeanropet.');
                resolve({raw:result, fields});
            } catch (e) { reject(e); }
        }));
    }
    function run(args) {
        return new Promise((resolve,reject) => {
            child = spawn(worker,args,{windowsHide:true,shell:false});
            let tail='', line='';
            child.stderr.on('data',data => { tail=(tail+data.toString()).slice(-4096); });
            child.stdout.on('data',data => {
                const lines=(line+data.toString()).split('\n');
                line=lines.pop();
                for (const msg of lines) {
                    const m=/^(\d+)\/(\d+)\s*$/.exec(msg);
                    if (m) {
                        el('progress').value = Number(m[1])/Number(m[2]);
                        el('status').textContent = 'Renderar bildruta '+m[1]+' av '+m[2]+'.';
                    }
                }
            });
            child.on('error',reject);
            child.on('close',code => {
                child=null;
                if (cancelled) reject(Error('Renderingen avbröts.'));
                else if (code!==0) reject(Error(tail.trim() || 'Renderaren avslutades med kod '+code));
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
            if (!fs.existsSync(worker)) throw Error('Renderaren saknas. Kör Install.cmd först.');
            const dir=path.join(path.dirname(project),'Motion Blur Renders');
            fs.mkdirSync(dir,{recursive:true});
            // Each job owns its own folder, including simultaneous renders in other projects.
            job=fs.mkdtempSync(path.join(dir,'render-'));
            const output=path.join(job,path.parse(source).name+'_'+el('angle').value+'deg.mp4');
            el('cancel').hidden=false;
            el('progress').hidden=false;
            el('progress').removeAttribute('value');
            el('status').textContent='Öppnar källklippet …';
            await run([source,output,start,count,ticks,el('angle').value,el('samples').value]);
            completed=output;
            el('cancel').hidden=true;
            el('status').textContent='Importerar och kontrollerar placeringen …';
            // URI encoding keeps filenames separate from executable ExtendScript syntax.
            await host('MotionBlurTimeline.place('+JSON.stringify(clip.raw)+','+
                       JSON.stringify(encodeURIComponent(output))+')');
            el('status').textContent='Klar på V'+target+'.\n'+output;
        } catch (error) {
            let message=error.message;
            if (completed) message+='\nRenderfil: '+completed;
            else if (job) {
                try { fs.rmSync(job,{recursive:true,force:true}); }
                catch (_) { message+='\nArbetsmapp: '+job; }
            }
            el('status').textContent=message;
        } finally {
            el('progress').hidden=true; el('cancel').hidden=true;
            for (const id of ['render','samples','angle']) el(id).disabled=false;
        }
    };
})();
