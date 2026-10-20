import {DailyRainModel} from './model.mjs';
let model;
const yieldTask=()=>new Promise(resolve=>{const c=new MessageChannel();c.port1.onmessage=()=>{c.port1.close();c.port2.close();resolve();};c.port2.postMessage(0);});
let chain=Promise.resolve();
self.onmessage=({data:m})=>{chain=chain.then(async()=>{
  try{
    const start=performance.now();if(m.kind==='init')model=new DailyRainModel(m.config,m.day,m.saved);
    if(!model)throw Error('降水模型未初始化');
    if(m.day<model.day)throw Error('天气时间不能倒退，请恢复对应存档');
    while(model.day<m.day){
      const next=model.day+1;
      for(let row=0;row<model.config.rows;row+=32){model.stepRows(next,row,Math.min(row+32,model.config.rows));await yieldTask();}
      model.day=next;model.updates++;
    }
    const snapshot=model.snapshot();self.postMessage({kind:'state',id:m.id,snapshot,ms:performance.now()-start,updates:model.updates},[snapshot.water.buffer,snapshot.rain.buffer,snapshot.phase.buffer]);
  }catch(error){self.postMessage({kind:'error',id:m.id,message:error.message});}
});};
