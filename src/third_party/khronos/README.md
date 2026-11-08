# Khronos OpenGL headers

Vendored on 2026-11-08 from the official registries:

- `GL/glcorearb.h`: https://raw.githubusercontent.com/KhronosGroup/OpenGL-Registry/main/api/GL/glcorearb.h
- `KHR/khrplatform.h`: https://raw.githubusercontent.com/KhronosGroup/EGL-Registry/main/api/KHR/khrplatform.h

Both files retain upstream copyright and MIT license notices. The only local change to glcorearb.h is replacing `<KHR/khrplatform.h>` with `"../KHR/khrplatform.h"` to support the existing build include path. No runtime library is bundled.

SHA256 of vendored files:

```text
711a4ea5f3e7cc71763d2ba16b275de0a012893cb44a9ef0617d9c78c461e611  GL/glcorearb.h
fcc8209e15fa2558fa7f389d26e9fb02f6534c8dd36e30b6b6aa8e781aca08a8  KHR/khrplatform.h
```
