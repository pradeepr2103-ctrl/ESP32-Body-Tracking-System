# third_party/glad -- setup required before first build

This folder is intentionally **not populated with generated GLAD source**
in this delivery. GLAD (https://glad.dav1d.de / https://gen.glad.sh) is a
code generator that produces an OpenGL function-pointer loader directly
from the official Khronos GL registry XML. Hand-writing or reproducing
that generated output from memory risks subtly wrong enum values or
function signatures that would be very painful to debug, so it's safer
and more correct for you to generate it fresh (takes under a minute).

## Option A -- web generator (simplest)

1. Go to https://gen.glad.sh
2. Settings:
   - Language: **C/C++**
   - Specification: **OpenGL**
   - API gl: **Version 3.3**
   - Profile: **Core**
   - Generate a loader: **checked**
3. Click **Generate**, download the zip.
4. Extract it so you end up with exactly this layout:
   ```
   third_party/glad/include/glad/glad.h
   third_party/glad/include/KHR/khrplatform.h
   third_party/glad/src/glad.c
   ```

## Option B -- pip-installable generator (scriptable / offline-repeatable)

```bash
pip install glad2 --break-system-packages
python -m glad --api gl:core=3.3 --out-path third_party/glad c
```

This produces the same three files in the same layout as Option A.

## Why vendor it instead of using system packages?

Ubuntu's `libglfw3-dev` ships GLFW itself but not a GL loader -- you still
need GLAD (or GLEW) separately to resolve OpenGL 3.3+ function pointers at
runtime. Vendoring GLAD as generated source (rather than depending on a
system package) keeps the exact GL version/profile pinned and matching
what `Renderer.cpp` requests via `glfwWindowHint(GLFW_CONTEXT_VERSION_*)`.

## Verifying it's wired correctly

Once the 3 files above exist, `cmake .. && make` from `build/` should
compile `third_party/glad/src/glad.c` as part of the
`body_tracking_visualizer` target automatically (already wired in the
top-level `CMakeLists.txt`). If you see undefined-reference linker errors
mentioning `glad_gl*` symbols, double-check the file landed at exactly
`third_party/glad/src/glad.c` (not nested one folder deeper, which is a
common extraction mistake).
