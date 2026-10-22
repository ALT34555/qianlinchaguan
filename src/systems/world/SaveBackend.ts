import { BrowserSaveStore } from './BrowserSaveStore';
import { FileSaveStore } from './FileSaveStore';
import type { SaveStore } from './LocalSaveStore';
import { setWorldDataBackend } from './WorldDataStore';
import { t } from '../../i18n';

export async function resolveSaveStore(desktop: boolean, label: string, request: typeof fetch = fetch): Promise<SaveStore> {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), 5000);
  try {
    const response = await request('/api/world-data', { cache: 'no-store', signal: controller.signal });
    if (!response.ok || (await response.json()).worldData !== true) throw new Error('World data service unavailable');
    const store = new FileSaveStore(request, '/api/saves', label);
    if (desktop) await new FileSaveStore((url, init) => request(url, { ...init, signal: controller.signal })).list();
    setWorldDataBackend('file');
    return store;
  } catch (error) {
    if (desktop) throw new Error(`${t('startup.saveUnavailable')} ${error instanceof Error ? error.message : String(error)}`);
    setWorldDataBackend('browser');
    return new BrowserSaveStore();
  } finally { clearTimeout(timer); }
}
