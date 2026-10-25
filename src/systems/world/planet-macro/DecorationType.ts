/** 仅用于旧植物/岩石内容池选材，不写入区块或存档。 */
export function macroDecorationType(type:number):number {
  if(type===0)return 0;
  if(type===2||type===32)return 2;
  if(type===4)return 10;
  if(type>=5&&type<100&&type%10===5)return 5;
  if(type===22)return 11;
  const climate=Math.floor(type/100),material=Math.floor(type/10)%10;
  return climate===1?409:material===1||type%10===4?3:material===4||material===3?104:material===5?8:6;
}
