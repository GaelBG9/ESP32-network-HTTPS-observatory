const HEADERS=['id','timestamp','device','target_ip','status','rtt_ms','gateway_ms','rssi_dbm','http_ms','http_code'];
function setup() {
  const p=PropertiesService.getScriptProperties();
  if(!p.getProperty('SHEET_ID')) {
    const ss=SpreadsheetApp.create('MET — ESP32 Network Observatory');
    p.setProperty('SHEET_ID',ss.getId());
  }
  ['WRITE_TOKEN','READ_TOKEN'].forEach(k=>{if(!p.getProperty(k))p.setProperty(k,Utilities.getUuid()+Utilities.getUuid());});
  console.log('Tableur : https://docs.google.com/spreadsheets/d/'+p.getProperty('SHEET_ID'));
}
function output(x){return ContentService.createTextOutput(JSON.stringify(x)).setMimeType(ContentService.MimeType.JSON);}
function doPost(e){
 const lock=LockService.getScriptLock();
 try{
  const b=JSON.parse(e.postData.contents),p=PropertiesService.getScriptProperties();
  const read=b.action==='read', key=p.getProperty(read?'READ_TOKEN':'WRITE_TOKEN');
  if(!key||b.token!==key)return output({ok:false,error:'unauthorized'});
  if(!lock.tryLock(25000))return output({ok:false,error:'busy_retry'});
  const ss=SpreadsheetApp.openById(p.getProperty('SHEET_ID'));
  if(read){
   const rows=[];
   ss.getSheets().filter(s=>/^data_/.test(s.getName())).sort((a,b)=>b.getName().localeCompare(a.getName())).slice(0,8).forEach(s=>{
    if(s.getLastRow()>1)s.getRange(2,1,s.getLastRow()-1,HEADERS.length).getValues().forEach(v=>rows.push(Object.fromEntries(HEADERS.map((k,i)=>[k,v[i]]))));
   });
   return output({ok:true,rows:rows.map(r=>({...r,timestamp:new Date(r.timestamp).toISOString()}))});
  }
  if(!Array.isArray(b.rows)||b.rows.length<1||b.rows.length>120)throw Error('batch_size_1_to_120');
  const groups={};
  b.rows.forEach(r=>{
   if(!/^[A-Za-z0-9_-]{1,100}$/.test(r.id)||!/^[A-Za-z0-9_-]{1,40}$/.test(r.device))throw Error('invalid_id');
   const t=new Date(r.timestamp);
   if(isNaN(t)||t.getTime()>Date.now()+300000||t.getTime()<Date.now()-7*86400000)throw Error('timestamp_outside_7_days');
   if(!['ok','timeout','wifi_disconnected','dns_error','error'].includes(r.status))throw Error('invalid_status');
   if(r.target_ip&&!/^[0-9a-fA-F:.]{1,45}$/.test(r.target_ip))throw Error('invalid_ip');
   HEADERS.slice(5).forEach(k=>{if(r[k]!=null&&(typeof r[k]!=='number'||!Number.isFinite(r[k])))throw Error('invalid_number');});
   const name='data_'+Utilities.formatDate(t,'UTC','yyyy_MM_dd');
   (groups[name] ||= []).push(HEADERS.map(k=>k==='timestamp'?t.toISOString():(r[k]??'')));
  });
  let inserted=0;
  Object.entries(groups).forEach(([name,rows])=>{
   let s=ss.getSheetByName(name);
   if(!s){s=ss.insertSheet(name);s.getRange(1,1,1,HEADERS.length).setValues([HEADERS]).setBackground('#172238').setFontColor('#ffffff');s.setFrozenRows(1);}
   const ids=new Set(s.getLastRow()>1?s.getRange(2,1,s.getLastRow()-1,1).getValues().flat():[]);
   const fresh=rows.filter(r=>{if(ids.has(r[0]))return false;ids.add(r[0]);return true;});
   if(fresh.length)s.getRange(s.getLastRow()+1,1,fresh.length,HEADERS.length).setValues(fresh);
   inserted+=fresh.length;
  });
  return output({ok:true,inserted,accepted_ids:b.rows.map(r=>r.id)});
 }catch(err){return output({ok:false,error:String(err.message||err)});}
 finally{if(lock.hasLock())lock.releaseLock();}
}
