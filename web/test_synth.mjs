// SPDX-License-Identifier: GPL-3.0-only
// Parse INFO emitted by the real C handler and exercise the editor's tab rendering.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
import {bkManifest, bkU32, bkCrc, readBackup, restoreBackup} from './fm1backup.js';
const html = readFileSync(new URL('editor.html', import.meta.url), 'utf8');
const proto = html.split('/*PROTO-BEGIN*/')[1].split('/*PROTO-END*/')[0];
const E = vm.runInNewContext(`${proto}; ({parse,CMD,engineOrder})`, {setTimeout,clearTimeout,setInterval,clearInterval,console});
const info = E.parse[E.CMD.INFO](Array.from(readFileSync('build/host/synth-info.bin')));
assert(info.synthOnly && info.ntrk === 1 && info.nstep === 0);
assert(info.fm6 && info.cz && info.prophet && info.backupCaps === 3);
assert.equal(info.uiCaps & 16, 0); // No recording/count-in preferences.
assert.equal(info.native[12], 64); assert.equal(info.native[15], 128); assert.equal(info.native[19], 128);
assert.equal(E.engineOrder(info.engines).map(i=>info.engines[i]).join(), 'PROPHET,FM6,CZ-1,SID');
assert(!info.motionMax && !info.patterns && !info.chainRows);
const elements = new Map();
const element = id => {
  if (!elements.has(id)) elements.set(id, {id,hidden:false,dataset:{},attributes:{},
    setAttribute(k,v){this.attributes[k]=v;},append(child){child.parentNode=this;}});
  return elements.get(id);
};
const tabs = ['sound','fm6','sequencer','tracks','library','projects','settings'].map(name=>{
  const b=element(name); b.dataset.tab=name; return b;
});
const panels = tabs.map(b=>{const p=element(`p-${b.id}`);p.dataset={tab:b.id,dev:''};return p;});
element('tabs').querySelectorAll=()=>tabs;
element('backup-panel').parentNode=element('p-projects');
const render = html.slice(html.indexOf('function renderPanels()'),html.indexOf('$("tabs").addEventListener("click"'));
const ctx = {dev:{info,dump:{}},tab:'sequencer',$:element,TRACK_TABS:new Set(['sound','sequencer','tracks']),
  hasTracks:()=>true,t:k=>k,document:{querySelectorAll:()=>panels}};
vm.runInNewContext(`${render};renderPanels()`,ctx);
for (const name of ['sequencer','tracks','projects']) assert(element(name).hidden);
assert.equal(element('sound').attributes['aria-selected'],'true');
assert(!element('p-sound').hidden && element('trackbar').hidden);
assert.equal(element('backup-panel').parentNode.id,'p-settings');

// A synth archive may carry saved projects belonging to the main firmware even though live project 0 is empty.
const ids=Array.from({length:29},(_,i)=>i), objects=ids.map(id=>{
  const bytes=id===2?Uint8Array.from([1,2,3,4]):id===28?readFileSync('build/host/synth-state.bin'):id===1?readFileSync('build/host/synth-settings.bin'):new Uint8Array(0);
  return {id,size:bytes.length,crc:bytes.length?bkCrc(bytes):0,data:Buffer.from(bytes).toString('base64')};
});
const file={format:'felucca-backup',version:1,objects}; readBackup(file);
const corrupt=structuredClone(file), raw=Buffer.from(corrupt.objects[28].data,'base64'); raw[0]^=1;
corrupt.objects[28].data=raw.toString('base64'); corrupt.objects[28].crc=bkCrc(raw);
assert.throws(()=>readBackup(corrupt),/Invalid synth state/);
const manifest=[1,0,29,...objects.flatMap(o=>[o.id,...bkU32(o.size),...bkU32(o.crc)])];
assert.equal(bkManifest(manifest).length,29);
const committed=[];
await restoreBackup(async([cmd,a])=>{
  if(cmd===65)return manifest;
  if(a[0]===2)committed.push(a[1]);
  return [a[0],a[1],0];
},file);
assert(committed.includes(2));
console.log('SYNTH WEB: real INFO/native capabilities, hidden retired tabs, Settings backup, foreign saved-project restore passed');
