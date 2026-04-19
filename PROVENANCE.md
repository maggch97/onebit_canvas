# Provenance

Raw Cairo source snapshots live under [upstream/cairo-1.18.4](upstream/cairo-1.18.4).

Those files were copied directly from the Cairo 1.18.4 source tree so later diffs can cleanly answer two questions:
- what came from upstream
- what was changed for `onebit_canvas`

Current adapted files:
- [include/onebit_canvas_cairo_mono.hpp](include/onebit_canvas_cairo_mono.hpp)
  Derived mainly from:
  - `upstream/cairo-1.18.4/src/cairo-fixed-type-private.h`
  - `upstream/cairo-1.18.4/src/cairo-fixed-private.h`
  - `upstream/cairo-1.18.4/src/cairo-polygon.c`
  - `upstream/cairo-1.18.4/src/cairo-rectangle.c`
  - `upstream/cairo-1.18.4/src/cairo-mono-scan-converter.c`

Main adaptations on top of upstream:
- packed into a single header-only C++17 implementation
- namespaced under `onebit::detail::cairo_mono`
- reduced to the polygon fill path needed by `onebit_canvas`
- switched the working fixed storage to signed 64-bit
- kept Cairo's current `24.8` raster semantics so pixel tests match Cairo
- replaced narrow intermediate multiply/divide with wide multiply/divide support so the new backend does not inherit Cairo's old fixed-size coordinate ceiling

Support/reference files copied from upstream and kept for investigation:
- `upstream/cairo-1.18.4/src/cairo-path-fill.c`
- `upstream/cairo-1.18.4/src/cairo-path-bounds.c`
- `upstream/cairo-1.18.4/src/cairo-composite-rectangles.c`
- `upstream/cairo-1.18.4/src/cairo-spans-compositor.c`

License files copied from upstream:
- `upstream/cairo-1.18.4/COPYING-LGPL-2.1`
- `upstream/cairo-1.18.4/COPYING-MPL-1.1`
