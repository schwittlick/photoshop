# rawedit

A single-image raw developer with an ACR-style dialog, implemented from
[raw-editor-spec.md](raw-editor-spec.md): open a raw file, fix lens distortion,
perspective, crop, white balance and tone on the GPU, export with an embedded
ICC profile and the source EXIF. No catalog, no sidecars, no persisted edits.

## Build

Dependencies (Arch package names): `qt6-base`, `libraw`, `lensfun`
(+ `lensfun` database), `lcms2`, `libtiff`, `libjpeg-turbo`, `libpng`, `exiv2`,
`meson`, `ninja`, a C++20 compiler and an OpenGL 4.3 driver.

```sh
meson setup build            # add --buildtype=release for an optimised build
meson compile -C build
./build/rawedit data/DSC08912.ARW
```

Tests (the GPU test needs a GL 4.3 context; it runs under Qt's offscreen platform):

```sh
meson test -C build          # add -v to see the per-test output, including the timings
```

Two things to know when adding files: there is no AUTOMOC, so a new header that
declares `Q_OBJECT` has to be listed in `moc_headers` in [meson.build](meson.build),
and a new shader has to be listed in
[src/gpu/shaders/shaders.qrc](src/gpu/shaders/shaders.qrc) to be compiled into
the binary.

Headless helpers, useful for scripting and for checking a change without a
display:

```sh
QT_QPA_PLATFORM=offscreen ./build/rawedit file.ARW --screenshot shot.png   # window grab incl. GL canvas
QT_QPA_PLATFORM=offscreen ./build/rawedit file.ARW --export out.tif        # default export (tif/jpg/png by extension)
QT_QPA_PLATFORM=offscreen ./build/rawedit file.ARW --demo --screenshot s.png  # applies a set of edits + crop tool
    --no-lens   disables the lens profile for the headless run
```

## Using it

| Action | How |
|---|---|
| Open / export | `Ctrl+O`, `Ctrl+E` (or the Export… button) |
| Undo / redo | `Ctrl+Z`, `Ctrl+Shift+Z` / `Ctrl+Y` |
| Reset everything | `R` |
| Zoom | `0` fit, `1`–`4` for 25/50/100/200 %, wheel zooms at the cursor, `Ctrl` `+`/`-`, double-click toggles fit/100 % |
| Tools | `H` hand, `C` crop, `A` straighten, `W` white-balance eyedropper, `P` perspective handles, `Esc` back to hand |
| Crop | drag handles or draw a new rectangle; aspect from the Geometry panel; `X` swaps the aspect orientation; rule-of-thirds while dragging |
| Straighten | drag a line along a horizon or a vertical; the nearer axis wins |
| Perspective | drag the four corner handles, or use the Vertical/Horizontal keystone sliders |
| Before / after | hold `\`, or toggle with `B` |
| Clipping overlay | `J` (blown highlights red, crushed blacks blue) |
| Sliders | double-click resets to default, wheel nudges, the field takes typed values |
| Auto-crop | Geometry and Lens panels: largest rectangle without empty corners, free or keeping the current aspect |
| Display profile | File → Display profile (ICC)…; File → Display as sRGB |

## What the pipeline does

```
file -> LibRaw (black level, as-shot WB pre-scale, AHD demosaic, orientation)      CPU, once
     -> RGBA16F texture + mip chain                                               GPU, once
     -- everything below re-runs on parameter change, from the earliest dirty stage --
  1  colour.comp  undo LibRaw's pre-scale -> normalised raw (clip = 1 per channel)
                  white balance multipliers (temp/tint -> Planckian locus -> camera space)
                  highlight clamp of blown pixels towards neutral (spec §9 "ship the clamp")
                  camera -> XYZ D65 -> Bradford -> XYZ D50 -> linear Rec.2020 (working space)
                  vignetting gain (lensfun radial LUT and/or manual)
  2  warp.comp    ONE inverse mapping per output pixel, resampled once (Catmull-Rom; Lanczos3 for export):
                  output px -> crop -> frame -> rotation^-1 -> homography^-1 -> manual ptlens/CA
                  -> lensfun displacement grid (per channel) -> source; out of bounds = transparent
  3  tone.comp    exposure, black/white point, luminance-keyed highlights/shadows, log-space S-curve
                  contrast, point + parametric curve (1024-entry LUT, display-referred), Oklab
                  saturation/vibrance, output transform (sRGB analytic or ICC 3D LUT), histogram,
                  clipping overlay
```

Everything geometric is expressed in normalised frame coordinates, so the proxy
preview and the full-resolution export are the same function evaluated at two
resolutions. `tests/test_gpu.cpp` renders a synthetic grid at full and quarter
resolution and compares the warp coordinates: they agree to 0.0006 px (budget: 1 px).

Interactive rendering only ever processes viewport-sized textures from the mip
level just above the zoom, so the cost is independent of the file size: on the
Intel UHD iGPU used for development a 45 MP source renders tone changes in
~10 ms, white balance in ~23 ms and rotation in ~17 ms per 2560×1440 frame.

### Deviations from the spec, and why

- **Tone runs after the warp, not before.** §3 lists tone before the warp, but §7
  requires that changing a tone slider does not re-run the warp. Putting the
  per-pixel tone stage last satisfies that, and it also means resampling happens
  in linear light, which is the correct order anyway.
- **`EditParams` grew two fields**: `perspVertical` / `perspHorizontal` keystone
  sliders alongside the four free corner handles, `profileDistortion` /
  `profileVignetting` / `profileCA` to scale or disable parts of a lensfun profile,
  and `outputSharpenRadius`. Still one plain struct; render is a pure function of it.
- **White balance is stored as temp/tint** (the spec offered "or explicit
  multipliers"). The eyedropper solves the inverse mapping numerically, so it
  reproduces the picked neutral to better than 0.1 %.
- **Lens profiles are looked up in three passes** (strict with camera, strict
  without camera, loose with a score threshold). The sample shot is an APS-C lens
  on a full-frame body in crop mode; lensfun's crop-factor filter rejects that
  combination when the camera is given, and its loose search returns garbage for
  unknown lenses (scores 20–35 vs 70+ for real matches). The crop factor passed to
  lensfun comes from `FocalLengthIn35mmFilm / FocalLength` when available.
- **Export readback is RGBA32F** rather than RGBA16F: the last GPU stage writes a
  float texture for export, so quantisation happens exactly once, on the CPU, with
  triangular-PDF dither on the 8-bit paths.

## Verification done

- `test_core`: colour maths (neutral preservation for any illuminant, temp/tint
  round trip, Bradford, working↔sRGB), PCHIP curve, history, homography and
  frame geometry round trips, largest-inscribed-rectangle, lensfun matching and
  grid evaluation.
- `test_gpu`: proxy vs full-res geometry (0.0006 px), proxy vs full-res pixels,
  bit-exact undo, histogram totals, identity mapping, neutral-grey level through
  the whole chain, 45 MP timing.
- Colour against a reference developer: with the lens profile off, the sRGB
  export of `data/DSC08912.ARW` differs from LibRaw's own `dcraw_emu -w -W -H 0 -o 1`
  rendering by a mean of 0.03 of an 8-bit step (99th percentile 0.15), i.e. the
  matrix/white-point/transfer path is equivalent to the reference. This is a
  LibRaw-vs-LibRaw comparison of the colour path; darktable was not installed.
- Export: 16-bit LZW TIFF with an embedded lcms sRGB profile and the source EXIF
  (orientation reset to 1, pixel dimensions updated, structural TIFF tags, DNG
  profile tags and embedded previews dropped), verified with `tiffinfo` and
  `exiv2`; JPEG export verified as 4:4:4 with the APP2 ICC segment.
- Decode breadth: CC0 samples from raw.pixls.us for Canon (CR2, 5D Mark II, a
  colour-checker shot that renders neutral greys and plausible patches),
  Nikon (NEF, D750), Fujifilm (RAF X-Trans, X-T50), Panasonic (RW2, G9),
  Pentax (PEF, K-r), Olympus (ORF, E-M5 III) and a Canon DNG all open, render and
  export headlessly; lensfun matched a profile for six of the seven (the ORF
  carries no lens name).

## Known limitations

- Highlight reconstruction is the stable clamp from §9 (blown pixels are pulled
  to neutral with a soft transition); no inpainting.
- Colour rendition is LibRaw's Adobe matrix plus a linear transform; no DCP
  hue-twist tables, so it will not match ACR's look.
- Preview colour management uses sRGB unless an ICC file is chosen via the File
  menu; Wayland compositors do not expose the display profile.
- X-Trans (Fujifilm) decodes through LibRaw's slower X-Trans path, as the spec warns.
- Panel sections use a small QToolButton-headed collapsible container instead of a
  checkable `QGroupBox`, because the group-box checkbox reads as "enabled".
- The shader resources live in the core static library, so `ShaderProgram` calls
  `Q_INIT_RESOURCE` before the first read; without that reference the linker
  discards the generated resource object and every shader fails to open.
- Output sharpening and resize apply on export only and are not previewed.

## Layout

```
src/core    EditParams, ColourMath, CurveModel, History, Geometry, LensModel (lensfun), OutputProfiles (lcms2)
src/io      RawLoader (LibRaw), MetadataReader (exiv2), Exporter (libtiff/libjpeg/libpng + exiv2)
src/gpu     RenderBackend interface, GLBackend (GL 4.3 compute), ShaderProgram, TexturePool, shaders/
src/ui      EditorSession, MainWindow, CanvasWidget, panels/, widgets/ (SliderRow, CurveEditor, Histogram)
tests       test_core (CPU), test_gpu (offscreen GL)
```
