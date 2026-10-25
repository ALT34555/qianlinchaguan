/** 创建页字段读取窄接口：布局与表单只依赖它 */
export interface FieldAccess {
  el<T extends HTMLElement = HTMLElement>(id: string): T;
  input(id: string): HTMLInputElement;
}
