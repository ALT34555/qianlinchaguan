// 二叉堆：按值取最小，值相同取 id 小
export class Heap {
  constructor(capacity=1024) { this.ids=new Uint32Array(capacity);this.values=new Float64Array(capacity);this.size=0;this.last=0; }
  less(a,b) { return this.values[a]<this.values[b] || this.values[a]===this.values[b]&&this.ids[a]<this.ids[b]; }
  push(id,value) {
    if(this.size===this.ids.length){const ids=new Uint32Array(this.size*2),values=new Float64Array(this.size*2);ids.set(this.ids);values.set(this.values);this.ids=ids;this.values=values;}
    const ids=this.ids,values=this.values;let i=this.size++;
    while(i){const p=(i-1)>>1,vp=values[p],ip=ids[p];if(value>vp||value===vp&&id>=ip)break;ids[i]=ip;values[i]=vp;i=p;}
    ids[i]=id;values[i]=value;
  }
  pop() {
    const ids=this.ids,values=this.values,id=ids[0];this.last=values[0];const n=--this.size;if(!n)return id;
    const nid=ids[n],nv=values[n];let i=0;
    while(i*2+1<n){let child=i*2+1;if(child+1<n&&(values[child+1]<values[child]||values[child+1]===values[child]&&ids[child+1]<ids[child]))child++;
      const vc=values[child],ic=ids[child];if(nv<vc||nv===vc&&nid<=ic)break;ids[i]=ic;values[i]=vc;i=child;}
    ids[i]=nid;values[i]=nv;return id;
  }
}
