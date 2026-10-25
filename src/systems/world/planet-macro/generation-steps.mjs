export function drainSteps(steps){
  let next;
  do{next=steps.next();}while(!next.done);
  return next.value;
}

// 同步调用与异步任务共用生成步骤
export async function runSteps(steps,{cancelled=()=>false,budgetMs=3,
  yieldControl=()=>new Promise(resolve=>setTimeout(resolve,0)),now=()=>performance.now()}={}){
  let started=now();
  try{
    while(true){
      if(cancelled())return null;
      const next=steps.next();
      if(next.done)return cancelled()?null:next.value;
      if(now()-started>=budgetMs){
        await yieldControl();
        started=now();
      }
    }
  }finally{steps.return?.();}
}
