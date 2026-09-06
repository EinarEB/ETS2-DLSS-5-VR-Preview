'use strict';
const presets={low:{name:'Low',passes:1,resolution:50,crop:'60%',description:'A smaller neural region, with the lightest processing.'},medium:{name:'Medium',passes:1,resolution:65,crop:'75%',description:'The starting point for smoother frame delivery.'},high:{name:'High',passes:2,resolution:80,crop:'90%',description:'A larger neural region and a second pass.'},ultra:{name:'Ultra',passes:2,resolution:100,crop:'Whole eye',description:'Two passes at full resolution across the entire eye.'}};
const byId=id=>document.getElementById(id);
let data,scene='cab',preset='medium',request=0,ready=false;
function split(value){const n=Math.max(0,Math.min(100,Number(value)));byId('wipe').value=n;byId('viewer').style.setProperty('--split',n+'%');byId('wipe').setAttribute('aria-valuetext',`${n} percent original, ${100-n} percent neural edit`);}
function loadImage(url){return new Promise((resolve,reject)=>{const img=new Image();img.onload=()=>resolve(img);img.onerror=()=>reject(new Error('Image could not be loaded'));img.src=url;});}
async function render(){
 if(!data)return;const id=++request,s=data.scenes[scene],p=presets[preset],sample=s.presets[preset],viewer=byId('viewer');ready=false;
 document.querySelectorAll('[data-scene]').forEach(b=>b.setAttribute('aria-pressed',String(b.dataset.scene===scene)));
 document.querySelectorAll('[data-preset]').forEach(b=>b.setAttribute('aria-pressed',String(b.dataset.preset===preset)));
 byId('preset-title').textContent=p.name;byId('preset-description').textContent=p.description;byId('passes').textContent=p.passes;byId('resolution').textContent=p.resolution+'%';byId('crop').textContent=p.crop;
 byId('wipe').disabled=true;byId('show-neural').disabled=true;byId('show-original').disabled=true;byId('viewer-message').hidden=false;byId('viewer-message').textContent='Loading the captured frame…';viewer.setAttribute('aria-busy','true');viewer.classList.add('no-result');byId('after-image').hidden=true;byId('before-image').hidden=true;
 byId('result-link').hidden=true;byId('original-link').removeAttribute('href');byId('sample-kind').textContent=sample?'REAL VR CAPTURE':'COMPARISON QUEUED';
 try{
  await Promise.all([loadImage(s.original),...(sample?[loadImage(sample.image)]:[])]);if(id!==request)return;
  byId('before-image').src=s.original;byId('before-image').alt=`Original ${s.title}, captured in the left eye before the neural edit`;byId('before-image').hidden=false;byId('original-link').href=s.original_full||s.original;
  byId('capture-details').textContent=`${s.width} × ${s.height} pixels · left eye · ${s.captured}. Before optional color grading.`;
  if(sample){byId('after-image').src=sample.image;byId('after-image').alt=`${s.title} with the ${p.name} neural-rendering preset`;byId('after-image').hidden=false;viewer.classList.remove('no-result');byId('after-label').textContent=p.name+' · neural edit';byId('viewer-message').hidden=true;byId('result-link').href=sample.image_full||sample.image;byId('result-link').hidden=false;byId('sample-note').textContent=sample.note;byId('wipe').disabled=false;byId('show-neural').disabled=false;byId('show-original').disabled=false;byId('drag-hint').textContent='Drag to compare · use ← → on the slider';ready=true;split(50);}
  else{byId('viewer-message').textContent=`${p.name} comparison is awaiting processing. This is the original image.`;byId('sample-note').textContent='The same captured input will be replayed through this preset. No result is shown until that run has been verified.';byId('drag-hint').textContent='Choose Medium to use the comparison slider.';}
 }catch(error){if(id!==request)return;byId('viewer-message').textContent='The image could not be loaded. Refresh the page to retry.';byId('sample-note').textContent='Image unavailable.';}
 finally{if(id===request)viewer.setAttribute('aria-busy','false');}
}
document.querySelectorAll('[data-scene]').forEach(b=>b.addEventListener('click',()=>{scene=b.dataset.scene;render();}));
document.querySelectorAll('[data-preset]').forEach(b=>b.addEventListener('click',()=>{preset=b.dataset.preset;render();}));
byId('wipe').addEventListener('input',e=>split(e.target.value));byId('show-original').addEventListener('click',()=>{if(ready)split(100);});byId('show-neural').addEventListener('click',()=>{if(ready)split(0);});
fetch('assets/comparisons.json').then(r=>{if(!r.ok)throw new Error('Manifest unavailable');return r.json();}).then(v=>{data=v;render();}).catch(()=>{byId('viewer-message').textContent='Comparison data could not be loaded. Refresh the page to retry.';byId('viewer').setAttribute('aria-busy','false');});
