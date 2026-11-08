const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const props = {SHEET_ID:'test', WRITE_TOKEN:'write', READ_TOKEN:'read'};
const sheets = [];
function makeSheet(name) {
  const values = [];
  return {getName:()=>name, getLastRow:()=>values.length, setFrozenRows:()=>{},
    getRange(row,col,n,m) {return {
      getValues:()=>Array.from({length:n},(_,i)=>Array.from({length:m},(_,j)=>values[row-1+i]?.[col-1+j] ?? '')),
      setValues(rows) { rows.forEach((r,i)=>{values[row-1+i] ??= []; r.forEach((v,j)=>values[row-1+i][col-1+j]=v);}); return this; },
      setBackground(){return this;},setFontColor(){return this;}
    };}
  };
}
const ss = {getSheets:()=>sheets,getSheetByName:n=>sheets.find(s=>s.getName()===n),insertSheet:n=>{const s=makeSheet(n);sheets.push(s);return s;}};
const context = {
  PropertiesService:{getScriptProperties:()=>({getProperty:k=>props[k]})},
  LockService:{getScriptLock:()=>({tryLock:()=>true,hasLock:()=>true,releaseLock:()=>{}})},
  SpreadsheetApp:{openById:()=>ss},
  Utilities:{formatDate:t=>t.toISOString().slice(0,10).replaceAll('-','_')},
  ContentService:{MimeType:{JSON:'json'},createTextOutput:text=>({setMimeType:()=>text})},
};
vm.createContext(context);
vm.runInContext(fs.readFileSync(path.join(__dirname,'../apps-script/Code.gs'),'utf8'), context);
const post = body => JSON.parse(context.doPost({postData:{contents:JSON.stringify(body)}}));
const row = {id:'boot_0',timestamp:new Date().toISOString(),device:'probe',target_ip:'192.0.2.1',status:'ok',rtt_ms:10,gateway_ms:2,rssi_dbm:-50,http_ms:null,http_code:null};
assert.equal(post({token:'bad',rows:[row]}).error,'unauthorized');
assert.equal(post({token:'read',rows:[row]}).error,'unauthorized');
assert.equal(post({token:'write',rows:[row]}).inserted,1);
assert.equal(post({token:'write',rows:[row]}).inserted,0);
assert.deepEqual(post({token:'write',rows:[row]}).accepted_ids,['boot_0']);
assert.equal(post({action:'read',token:'read'}).rows.length,1);
assert.equal(post({token:'write',rows:[{...row,id:'next',rtt_ms:'bad'}]}).error,'invalid_number');
assert.equal(post({token:'write',rows:[{...row,id:'old',timestamp:'2000-01-01T00:00:00Z'}]}).error,'timestamp_outside_7_days');
assert.equal(post({token:'write',rows:[]}).error,'batch_size_1_to_120');
assert.equal(post({action:'read',token:'read'}).rows.length,1);
console.log('Receiver contract tests passed: auth, write, retry/dedup, read, numeric/time/batch validation.');
