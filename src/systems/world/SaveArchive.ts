import { zipSync, unzipSync, strToU8, strFromU8 } from 'fflate';
import { SAVE_FORMAT_VERSION } from '../../core/version.ts';
import { parseWorldSave, type WorldSave } from './WorldSave.ts';
import type { LocalSave } from './LocalSaveStore.ts';

export const MAX_SAVE_BYTES = 16 * 1024 * 1024;
export const VALID_SAVE_ID = /^[A-Za-z0-9][A-Za-z0-9_-]{0,79}$/;
const PARTS = ['world.json', 'player.json', 'human-activity.json'];

export function saveFilename(name: string, id: string): string {
  if (!VALID_SAVE_ID.test(id)) throw new Error('存档编号无效。');
  const safe = name.replace(/[<>:"/\\|?*\x00-\x1f\x7f]/g, '_').replace(/[ .]+$/g, '').slice(0, 80) || '未命名世界';
  return `${safe}_${id}.sav`;
}

export function encodeSaveArchive(item: LocalSave): Uint8Array {
  if (!VALID_SAVE_ID.test(item.id) || !Number.isFinite(item.createdAt) || !Number.isFinite(item.updatedAt)) throw new Error('存档元数据无效。');
  const save = parseWorldSave(JSON.stringify({ ...item.save, worldName: item.name }));
  const { ren, ren_ming, playerFaction, playerState, player, flying, artificialState, ...world } = save;
  if (!!world.generation.artificial?.enabled !== !!artificialState) throw new Error('世界类型与人类活动信息不一致。');
  const files: Record<string, Uint8Array> = {
    'world.json': strToU8(JSON.stringify({ ...world, _localSave: { version: SAVE_FORMAT_VERSION, id: item.id, createdAt: item.createdAt, updatedAt: item.updatedAt } })),
    'player.json': strToU8(JSON.stringify({ ...playerState, position: player, flying: flying ?? false })),
  };
  if (artificialState) {
    const { playerFactionId, ...human } = artificialState;
    files['human-activity.json'] = strToU8(JSON.stringify(human));
  }
  if (Object.values(files).reduce((size, file) => size + file.length, 0) > MAX_SAVE_BYTES) throw new Error('存档内容过大。');
  const result = zipSync(files, { level: 6 });
  if (result.length > MAX_SAVE_BYTES) throw new Error('存档文件过大。');
  return result;
}

export function decodeSaveArchive(bytes: Uint8Array): LocalSave {
  if (bytes.length > MAX_SAVE_BYTES || bytes[0] !== 0x50 || bytes[1] !== 0x4b) throw new Error('请选择有效的 .sav 压缩存档。');
  let total = 0;
  const names = new Set<string>();
  let files: Record<string, Uint8Array>;
  try {
    files = unzipSync(bytes, { filter: entry => {
      if (!PARTS.includes(entry.name) || names.has(entry.name)) throw new Error('存档包含无效或重复的文件。');
      names.add(entry.name);
      total += entry.originalSize;
      if (total > MAX_SAVE_BYTES) throw new Error('解压后的存档过大。');
      return true;
    } });
  } catch (error) { throw new Error(`无法解压存档：${error instanceof Error ? error.message : '文件损坏'}`); }
  const read = (name: string) => {
    if (!files[name]) throw new Error(`存档缺少 ${name}。`);
    try { return JSON.parse(strFromU8(files[name])); }
    catch { throw new Error(`存档中的 ${name} 损坏。`); }
  };
  const world = read('world.json'), player = read('player.json');
  const metadata = world?._localSave;
  if (!metadata || metadata.version !== SAVE_FORMAT_VERSION || !VALID_SAVE_ID.test(metadata.id) ||
      typeof metadata.id !== 'string' || !Number.isFinite(metadata.createdAt) || !Number.isFinite(metadata.updatedAt)) throw new Error('存档封装版本或元数据无效。');
  if (!world.mystery || typeof world.worldName !== 'string' || !world.generation || !player || typeof player !== 'object' ||
      ['player', 'playerState', 'artificialState', 'ren', 'ren_ming', 'playerFaction', 'flying'].some(key => key in world)) throw new Error('存档分区结构无效。');
  const human = files['human-activity.json'] ? read('human-activity.json') : undefined;
  if (typeof world.generation.artificial?.enabled !== 'boolean' || world.generation.artificial?.enabled !== (human !== undefined)) throw new Error('世界类型与人类活动信息不一致。');
  if (human && 'playerFactionId' in human) throw new Error('人类活动文件不能包含玩家归属。');
  const { position, flying, ...playerState } = player;
  const active = playerState.factions?.find((f: { id: string; relationship: string }) => f.id === playerState.activeFactionId && f.relationship === 'owned');
  const save: WorldSave = parseWorldSave(JSON.stringify({ ...world, playerState, player: position, flying,
    ...(human ? { artificialState: { ...human, ...(active ? { playerFactionId: active.id } : {}) } } : {}) }));
  return { id: metadata.id, name: save.worldName!, createdAt: metadata.createdAt, updatedAt: metadata.updatedAt, save };
}
