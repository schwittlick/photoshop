# rawedit

A raw developer with an ACR-style dialog, implemented from
[raw-editor-spec.md](raw-editor-spec.md): open one raw file or a handful, fix lens
distortion, perspective, crop, white balance and tone on the GPU, copy the
settings of one image to the others, export with an embedded ICC profile and the
source EXIF. No catalog: the edit state of each raw lives in a JSON sidecar next to
it (`DSC08912.ARW.json`), written automatically and read back when the file is opened.

## Build

Dependencies (Arch package names): `qt6-base`, `libraw`, `lensfun`
(+ `lensfun` database), `lcms2`, `libtiff`, `libjpeg-turbo`, `libpng`, `exiv2`,
`meson`, `ninja`, a C++20 compiler and an OpenGL 4.3 driver.

```sh
meson setup build            # add --buildtype=release for an optimised build
meson compile -C build
./build/photoshop data/DSC08912.ARW              # one file
./build/photoshop data/*.ARW                     # a set; a folder argument adds every raw in it
```

Install system-wide (binary `photoshop` in `/usr/local/bin`, launcher entry, icon
and raw-file associations so it shows up in menus and "open with"):

```sh
sudo meson install -C build
```

For a user-only install without sudo: `meson setup build --prefix ~/.local` (or
`meson configure build --prefix ~/.local`) before the same install command.
Uninstall with `sudo ninja -C build uninstall`.

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
QT_QPA_PLATFORM=offscreen ./build/photoshop file.ARW --screenshot shot.png   # window grab incl. GL canvas
QT_QPA_PLATFORM=offscreen ./build/photoshop file.ARW --export out.tif        # default export (tif/jpg/png by extension)
QT_QPA_PLATFORM=offscreen ./build/photoshop file.ARW --demo --screenshot s.png  # applies a set of edits + crop tool
QT_QPA_PLATFORM=offscreen ./build/photoshop a.ARW b.ARW c.ARW --demo --sync --export outdir/ --format jpg
    # several files: --sync copies the demo edits to the other images, an --export *folder* exports every
    # image into it named after its raw (--format tif|jpg|png, default tif)
    --no-lens   disables the lens profile for the headless run
    --reset     resets the active image to its defaults (removes its sidecar)
    --auto      runs Auto tone on the active image and prints the resulting slider values
    --guides "x1,y1,x2,y2;..."   adds upright guides (frame coordinates 0..1 of the loaded view) and applies them
    # note that --demo, --sync and --auto write sidecars next to the files, like any edit
```

## Using it

| Action | How |
|---|---|
| Open / export | `Ctrl+O` (multi-select, or drop files/folders on the window; opening adds to what is loaded), `Ctrl+E` (or the Export… button) exports the selection: one image to a file, several into a folder |
| Switch image | click a thumbnail in the filmstrip, `PgUp` / `PgDn`; the filmstrip appears with the second image |
| Select images | `Ctrl`-click toggles a thumbnail, `Shift`-click extends from the active image, `Ctrl+A` selects all; the active image is always selected |
| Close image | `Ctrl+W`, or the × on a hovered thumbnail; File → Close all |
| Sync settings | `Ctrl+Shift+S` (or the Sync… button): copies groups of the active image's settings to every other loaded image, see below |
| Export all | `Ctrl+Shift+E`: every image with the same format settings into one folder, named after its raw |
| Undo / redo | `Ctrl+Z`, `Ctrl+Shift+Z` / `Ctrl+Y` (per image; a sync is one step on each target) |
| Auto tone | `Ctrl+U` or the Auto button in the Tone group: exposure, contrast, highlights, shadows and blacks from the histogram of the crop, see below |
| Reset everything | `R` (the active image; also removes its sidecar) |
| Edit state | saved automatically to `<raw>.json` next to the raw, see below |
| Zoom | `0` fit, `1`–`4` for 25/50/100/200 %, wheel zooms at the cursor, `Ctrl` `+`/`-`, double-click toggles fit/100 % |
| Tools | `H` hand, `C` crop, `A` straighten, `W` white-balance eyedropper, `P` perspective handles, `Esc` back to hand |
| Crop | drag handles or draw a new rectangle; aspect from the Geometry panel; `X` swaps the aspect orientation; rule-of-thirds while dragging |
| Straighten | drag a line along a horizon or a vertical; the nearer axis wins |
| Perspective | drag the four corner handles, or use the Vertical/Horizontal keystone sliders |
| Guides (upright) | `G`: draw two to four lines along things that should be vertical or horizontal; the perspective is corrected as soon as two exist and the crop follows; drag an end to adjust, right-click removes a guide, `Backspace` the last |
| Before / after | hold `\`, or toggle with `B` |
| Clipping overlay | `J` (blown highlights red, crushed blacks blue) |
| Sliders | double-click resets to default, wheel nudges, the field takes typed values |
| Auto-crop | Geometry and Lens panels: largest rectangle without empty corners, free or keeping the current aspect |
| Display profile | File → Display profile (ICC)…; File → Display as sRGB |

### Several images at once

Every loaded file has its own decoded raw, lens profile, settings and undo history;
the panels and tools always act on the active one. Files decode one at a time in the
background (about 2 s each for a 10 MP ARW, decoded data stays in RAM: 6 bytes per
pixel, so ~270 MB per 45 MP file — meant for a handful of images, not a whole shoot).
The first file opened is shown as soon as it is decoded; clicking a thumbnail that is
still queued moves it to the front of the queue and switches when it is ready.
Thumbnails are the raws' embedded previews, so they show the file, not the edit.

**Sidecars.** Every change is written, a quarter of a second after the last one, to a
JSON file next to the raw (`DSC08912.ARW.json`), and the file is read back when the raw
is opened. The raw itself is never touched. The values are the edit parameters verbatim
(normalised frame coordinates, so they are resolution independent); nothing else can
render them, which is why it is not an XMP file: the tone and lens models here do not
map onto Camera Raw's. An image that is back at its as-shot defaults gets its sidecar
removed, so an untouched folder stays clean. A sidecar that cannot be read (broken, or
written by a newer format version) is left alone and the image's edits are not saved
until it is fixed or removed; the status bar says so. Thumbnails of edited images carry
a blue dot.

**Auto tone** is the histogram-based kind, what Camera Raw's Auto was before it
became a neural network. It renders the crop at 640 px and bisects each slider
against a measured target on that render (about 100 renders of the tone stage, a
few tens of milliseconds): exposure so the mean/median blend moves 80 % of the way
to 0.45 (18 % grey encodes to 0.46; the missing 20 % keeps high-key and low-key
scenes from being flattened to medium), highlights until at most 2 % of pixels sit
above 0.94 and 0.3 % clip, shadows until at most 3 % sit below 0.06, blacks so the
0.5th percentile lands at 0.02, contrast raised (never lowered, at most +35) halfway
towards a 10–90 % spread of 0.6, then a second pass for exposure and the ends. Whites is reset
to 0 and not solved: in this pipeline it is a plain gain, the same lever as
exposure, so solving both would only make them fight. Because it measures the real
render, curves you have set are accounted for, and the result is an ordinary edit:
undoable, synced and saved like any other. Pressing it again gives the same values.
Its known weakness is inherited from the original: scenes that should stay dark or
bright are pulled towards medium, though only 80 % of the way.

**Guides** are Lightroom's Guided Upright: each guide is a line in the picture that
should end up exactly vertical or horizontal (the tool decides which from the angle
you draw it at, blue for vertical, orange for horizontal). Two guides of one kind fix
that direction's keystone and straighten the image; one of each squares the picture
up without a keystone; two of each fix the full perspective. The maths is the
vanishing-point construction: a pair of guides meets at a vanishing point, which the
homography sends to infinity; the horizon through two such points becomes the line at
infinity, and an affine step squares the directions up. The frame centre stays where
it is and the scale there is unchanged, then the crop shrinks to the largest area
without empty corners at the same aspect (auto-crop). The guides live in the edit
state, so they are undoable, saved in the sidecar, part of Sync's Perspective group,
and can be adjusted later. Straight lines have to be straight first: keep the lens
profile on. Guides that converge inside the frame are refused with a message.

**Sync** copies the active image's settings to all the others, by group, with one
checkbox each: white balance, tone, curves, lens corrections, rotation, perspective,
crop, output sharpening. Tone, curves, lens and output sharpening are on by default;
white balance, rotation, perspective and crop are off because they are usually
specific to one frame (white balance is safe to sync for shots in the same light: it
is stored as an absolute temperature/tint, not as camera multipliers). The choice is
remembered. An image that is still decoding receives the sync once it is ready.

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
  4  sharpen.comp preview only: separable unsharp mask on the encoded image, radius x zoom
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
  round trip, Bradford, working↔sRGB), PCHIP curve, history, settings sync by
  group, sidecar JSON and file round trips (exact, tolerant of missing and unknown
  keys, rejects newer versions and broken curves), Guided Upright (a photographed
  rectangle's edges come out vertical and horizontal to better than 0.001 px for
  every guide combination, the centre stays put, crossing guides are refused),
  homography and frame geometry round trips, largest-inscribed-rectangle, lensfun
  matching and grid evaluation.
- `test_gpu`: proxy vs full-res geometry (0.0006 px), proxy vs full-res pixels,
  bit-exact undo, histogram totals, identity mapping, neutral-grey level through
  the whole chain, Auto tone (reaches its brightness and black-point targets,
  pulls back the highlights it blows, idempotent, leaves other settings alone,
  gives a three-stops-darker source much more exposure), 45 MP timing.
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
- Resize applies on export only. Output sharpening is previewed with its radius scaled by the zoom, so it is only judgeable at 100 % or more.
- With several images loaded, all of them stay decoded in RAM (see above) and only the active one is on the GPU; Export all uploads each image in turn. Filmstrip thumbnails do not reflect the edits. Undo is per image, so undoing a sync means visiting each image. There is no live "auto sync" mode: Sync is a one-shot copy.

## Layout

```
src/core    EditParams, ColourMath, CurveModel, History, Geometry, LensModel (lensfun), OutputProfiles (lcms2), AutoTone
src/io      RawLoader (LibRaw), MetadataReader (exiv2), Exporter (libtiff/libjpeg/libpng + exiv2), Sidecar (JSON edit state)
src/gpu     RenderBackend interface, GLBackend (GL 4.3 compute), ShaderProgram, TexturePool, shaders/
src/ui      EditorSession (the loaded documents + the active one), MainWindow, CanvasWidget, FilmstripWidget,
            SyncDialog, ExportDialog, panels/, widgets/ (SliderRow, CurveEditor, Histogram)
tests       test_core (CPU), test_gpu (offscreen GL)
```
