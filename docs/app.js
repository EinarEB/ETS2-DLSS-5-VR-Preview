'use strict';
const $=id=>document.getElementById(id);
const selectors=['scene','quality','style','look'].map($);
const qualities={low:'1 pass · 50% resolution · 60% square',medium:'1 pass · 65% resolution · 75% square',high:'2 passes · 80% resolution · 90% square',ultra:'2 passes · full resolution · whole eye'};
let data,request=0,split=50;
const viewer=$('comparison'),status=$('selection-status'),buttons=['show-original','show-split','show-result'].map($);
function setSplit(value){
 split=Math.max(0,Math.min(100,Number(value)));viewer.style.setProperty('--split',split+'%');
 const range=$('comparison-slider');if(range){range.value=split;range.setAttribute('aria-valuetext',split===100?'Original only, no effect':split===0?'Full selected effect':`${Math.round(split)}% of the original revealed`);}
 const captions=viewer.querySelectorAll('.labels span');if(captions.length===2){captions[0].hidden=split===0;captions[1].hidden=split===100;captions[1].style.marginLeft='auto';}
 const divider=viewer.querySelector('.divider');if(divider)divider.hidden=split===0||split===100;
 buttons.forEach((b,i)=>b.setAttribute('aria-pressed',String(split===[100,50,0][i])));
}
function image(src,alt){return new Promise((resolve,reject)=>{const img=new Image();img.width=2504;img.height=2600;img.alt=alt;img.draggable=false;img.onload=()=>resolve(img);img.onerror=reject;img.src=src;});}
async function render(){
 const version=++request,scene=data.scenes[$('scene').value],key=`${$('quality').value}-${$('style').value}-${$('look').value}`,sample=scene.samples[key];
 const label=selectors.map(s=>s.options[s.selectedIndex].text).join(' · ');
 viewer.setAttribute('aria-busy','true');viewer.classList.add('loading');buttons.forEach(b=>b.disabled=true);
 status.textContent='Loading '+label+'…';$('original-link').removeAttribute('href');$('result-link').removeAttribute('href');
 try {
  const [original,result]=await Promise.all([image(scene.original.full,'Original frame — no neural effect or added grading'),image(sample.full,'Full effect — '+label)]);
  if(version!==request)return;
  original.className='original';const divider=document.createElement('div');divider.className='divider';divider.setAttribute('aria-hidden','true');divider.innerHTML='<span>↔</span>';
  const labels=document.createElement('div');labels.className='labels';labels.innerHTML='<span>No effect</span><span>Full effect</span>';
  const range=document.createElement('input');range.id='comparison-slider';range.type='range';range.min='0';range.max='100';range.value=split;range.setAttribute('aria-label','Before and after divider');
  range.addEventListener('input',()=>setSplit(range.value));
  const position=e=>{const bounds=range.getBoundingClientRect();setSplit((e.clientX-bounds.left)/bounds.width*100);};
  range.addEventListener('pointerdown',e=>{if(e.button!==0)return;e.preventDefault();range.focus();range.setPointerCapture(e.pointerId);position(e);});
  range.addEventListener('pointermove',e=>{if(range.hasPointerCapture(e.pointerId))position(e);});
  range.addEventListener('pointerup',e=>{if(range.hasPointerCapture(e.pointerId))range.releasePointerCapture(e.pointerId);});
  viewer.replaceChildren(result,original,labels,divider,range);viewer.classList.remove('loading');viewer.setAttribute('aria-busy','false');setSplit(split);
  status.textContent=label+' · '+qualities[$('quality').value];buttons.forEach(b=>b.disabled=false);
  $('original-link').href=scene.original.full;$('result-link').href=sample.full;
 } catch {
  if(version!==request)return;viewer.replaceChildren();viewer.classList.remove('loading');viewer.setAttribute('aria-busy','false');status.textContent='The selected images could not load. Change a setting or refresh to retry.';
 }
}
buttons.forEach((b,i)=>b.addEventListener('click',()=>setSplit([100,50,0][i])));selectors.forEach(s=>s.addEventListener('change',render));
fetch('assets/comparisons.json?v=3').then(r=>{if(!r.ok)throw Error();return r.json();}).then(v=>{data=v;selectors.forEach(s=>s.disabled=false);render();}).catch(()=>{status.textContent='Comparisons could not load. Refresh to retry.';viewer.setAttribute('aria-busy','false');});
