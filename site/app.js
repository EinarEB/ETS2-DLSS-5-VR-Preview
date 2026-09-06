'use strict';
const qualities={low:'Low · 1 pass · 50% resolution · 60% square',medium:'Medium · 1 pass · 65% resolution · 75% square',high:'High · 2 passes · 80% resolution · 90% square',ultra:'Ultra · 2 passes · 100% resolution · whole eye'};
const styles=['natural','default','cinematic'],looks=['clean','cooler','cold'];
const title=s=>s[0].toUpperCase()+s.slice(1),el=(tag,cls,text)=>{const e=document.createElement(tag);if(cls)e.className=cls;if(text)e.textContent=text;return e;};
let data,quality='medium',opener;
const detail=document.getElementById('detail'),content=document.getElementById('detail-content');
function viewer(original,result,label,large=false){
 const box=el('div','viewer loading');box.style.setProperty('--split','50%');box.setAttribute('aria-busy','true');
 const after=el('img'),before=el('img','before');after.alt=label;before.alt='Original captured image';
 for(const img of [after,before]){img.width=2504;img.height=2600;img.draggable=false;img.decoding='async';if(!large)img.loading='lazy';}
 const captions=el('div','image-labels');captions.append(el('span','','Original'),el('span','','DLSS 5'));
 const divider=el('div','divider');divider.setAttribute('aria-hidden','true');divider.append(el('span','','↔'));
 const range=el('input');range.type='range';range.min='0';range.max='100';range.value='50';range.disabled=true;range.setAttribute('aria-label',label+' — original image coverage');
 const split=()=>{box.style.setProperty('--split',range.value+'%');range.setAttribute('aria-valuetext',`${range.value}% original, ${100-Number(range.value)}% processed`);};range.addEventListener('input',split);split();
 const message=el('span','image-status','Loading…');let loaded=0;
 for(const img of [after,before]){img.onload=()=>{if(++loaded===2){range.disabled=false;box.classList.remove('loading');box.setAttribute('aria-busy','false');message.remove();}};img.onerror=()=>{message.textContent='Image unavailable. Reload to retry.';box.setAttribute('aria-busy','false');};}
 box.append(after,before,captions,divider,range,message);after.src=result;before.src=original;return box;
}
function openDetail(scene,sample,label,button){
 opener=button;document.getElementById('detail-title').textContent=label;
 content.replaceChildren(viewer(scene.original.full,sample.full,label,true));
 document.getElementById('full-original').href=scene.original.full;document.getElementById('full-result').href=sample.full;
 detail.showModal();document.body.classList.add('dialog-open');document.getElementById('close-detail').focus();
}
detail.addEventListener('close',()=>{document.body.classList.remove('dialog-open');content.replaceChildren();if(opener?.isConnected)opener.focus();});
document.getElementById('close-detail').addEventListener('click',()=>detail.close());detail.addEventListener('click',e=>{if(e.target===detail){const r=detail.getBoundingClientRect();if(e.clientX<r.left||e.clientX>r.right||e.clientY<r.top||e.clientY>r.bottom)detail.close();}});
function render(){
 if(!data)return;
 document.querySelectorAll('[data-quality]').forEach(b=>b.setAttribute('aria-pressed',String(b.dataset.quality===quality)));
 document.getElementById('quality-summary').textContent=qualities[quality];
 const fragment=document.createDocumentFragment();
 for(const key of ['exterior','cab']){
  const scene=data.scenes[key],section=el('section','scene');section.id=key;
  const heading=el('div','scene-heading');heading.append(el('h2','',scene.title),el('p','',scene.caption));section.append(heading);
  for(const style of styles){
   const row=el('div','style-row');row.append(el('h3','',title(style)));const grid=el('div','comparison-grid');
   for(const look of looks){
    const sample=scene.samples[`${quality}-${style}-${look}`],label=`${scene.title} · ${title(quality)} · ${title(style)} · ${title(look)}`;
    const card=el('article','comparison-card');const cardHeading=el('div','card-heading');cardHeading.append(el('h4','',title(look)));
    const button=el('button','open-detail','Larger view ↗');button.setAttribute('aria-label','Open '+label);button.addEventListener('click',()=>openDetail(scene,sample,label,button));cardHeading.append(button);
    card.append(cardHeading,viewer(scene.original.preview,sample.preview,label));grid.append(card);
   }
   row.append(grid);section.append(row);
  }
  fragment.append(section);
 }
 document.getElementById('scenes').replaceChildren(fragment);document.getElementById('page-status').hidden=true;
}
document.querySelectorAll('[data-quality]').forEach(b=>b.addEventListener('click',()=>{if(quality!==b.dataset.quality){quality=b.dataset.quality;render();}}));
fetch('assets/comparisons.json?v=2').then(r=>{if(!r.ok)throw Error('Missing comparison index');return r.json();}).then(v=>{data=v;render();}).catch(()=>{document.getElementById('page-status').textContent='The comparisons could not be loaded. Refresh the page to retry.';});
