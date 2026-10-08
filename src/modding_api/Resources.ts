export interface ModResource { path: string; url: string }
export interface ModFont { id: string; label: string; family: string; url: string }
export interface ModCatalog { resources: ModResource[]; fonts: ModFont[] }

const fonts = new Map<string, ModFont>();
const faces: FontFace[] = [];
let resources: ModResource[] = [];

export function mountedFonts(): readonly ModFont[] { return [...fonts.values()]; }
export function mountedResources(): readonly ModResource[] { return [...resources]; }
export function modResourceURL(path: string): string | undefined {
  return resources.find(resource => resource.path === path)?.url;
}
export function fontFamily(id: string | undefined): string {
  const family = id && fonts.get(id)?.family;
  return family ? `"${family}", system-ui, sans-serif` : 'system-ui, sans-serif';
}

export async function mountModResources(fetcher: typeof fetch = fetch): Promise<void> {
  try {
    const response = await fetcher('/api/mods', { signal: AbortSignal.timeout(5000) });
    if (!response.ok) return;
    const catalog = await response.json() as ModCatalog;
    for (const face of faces) document.fonts.delete(face);
    faces.length = 0; fonts.clear();
    resources = catalog.resources;
    for (const font of catalog.fonts) {
      try {
        const face = new FontFace(font.family, `url(${JSON.stringify(font.url)})`);
        document.fonts.add(face);
        faces.push(face);
        fonts.set(font.id, font);
        void face.loaded.catch(error => console.warn(`字体 mod 加载失败：${font.label}`, error));
      } catch (error) { console.warn(`字体 mod 加载失败：${font.label}`, error); }
    }
  } catch (error) { console.warn('mod 资源接口不可用', error); }
}
