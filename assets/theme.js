(()=>{
 'use strict';
 const key='worktool-theme',root=document.documentElement,system=window.matchMedia('(prefers-color-scheme: dark)');
 let preference='system';try{const saved=localStorage.getItem(key);if(['light','dark','system'].includes(saved))preference=saved;}catch{}
 function apply(){root.dataset.theme=preference==='system'?(system.matches?'dark':'light'):preference;root.style.colorScheme=root.dataset.theme;const select=document.getElementById('themeMode');if(select)select.value=preference;}
 apply();
 system.addEventListener('change',()=>{if(preference==='system')apply();});
 window.addEventListener('storage',e=>{if(e.key===key){preference=['light','dark'].includes(e.newValue)?e.newValue:'system';apply();}});
 function mount(){
  const bar=document.querySelector('.toolnav,.topnav,.topbar');if(!bar)return;
  const label=document.createElement('label');label.className='theme-control';label.append(document.createTextNode('화면'));
  const select=document.createElement('select');select.id='themeMode';select.setAttribute('aria-label','화면 모드');
  [['system','자동'],['light','라이트'],['dark','다크']].forEach(([value,text])=>select.add(new Option(text,value)));
  select.addEventListener('change',()=>{preference=select.value;try{localStorage.setItem(key,preference);}catch{}apply();});
  label.append(select);bar.append(label);apply();
 }
 if(document.readyState==='loading')document.addEventListener('DOMContentLoaded',mount,{once:true});else mount();
})();
