
const owners=new WeakMap();
let previewChannel,previewQueue=[];
export const pausePreview=()=>globalThis.scheduler?.yield?globalThis.scheduler.yield():new Promise(resolve=>{
  if(!previewChannel){
    previewChannel=new MessageChannel();
    previewChannel.port1.onmessage=()=>{
      const list=previewQueue;previewQueue=[];
      if(!previewQueue.length)previewChannel.port1.unref?.();
      for(const fn of list)fn();
    };
  }
  previewQueue.push(resolve);
  if(previewQueue.length===1){previewChannel.port1.ref?.();previewChannel.port2.postMessage(0);}
});
export function* retrySample(sample){return sample();}
export function runSync(task){
  let next;
  do{next=task.next();if(next.value instanceof Promise){task.return();throw new Error('同步采样不能等待异步任务');}}while(!next.done);
  return next.value;
}
export async function runPreview(world,task,{signal,progress=()=>{},pause=pausePreview}={}){
  const owner={};owners.set(world,owner);
  const check=()=>{if(signal?.aborted||owners.get(world)!==owner)throw new DOMException('预览已取消','AbortError');};
  let deadline=performance.now()+8;
  try{
    progress({stage:'区域采样'});await pause();
    while(true){
      check();const next=task.next();if(next.done)return next.value;
      if(next.value instanceof Promise){await next.value;check();deadline=performance.now()+8;}
      else if(performance.now()>=deadline){
        progress(next.value);await pause();deadline=performance.now()+8;
      }
    }
  }finally{
    task.return();
    if(owners.get(world)===owner)owners.delete(world);
  }
}
