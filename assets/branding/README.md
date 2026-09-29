# AhoiBrowser icon source

`ahoi-browser-icon-1024.png` is the shared opaque raster master for AhoiBrowser
on macOS and iOS. On 8 September 2026 the user explicitly requested integration
of the refined ImageGen direction: two ivory sail planes, an integrated wave
and a restrained coral accent on cobalt blue. The original generated artwork
and prompts are preserved in
[`concepts/2026-09-08-integrated-wave`](concepts/2026-09-08-integrated-wave/README.md).
The 1024-pixel master is a mechanical Lanczos resize, not a new design variant.

`macos-icon-mask-1024.png` preserves the previous desktop alpha footprint. It is
applied only to desktop derivatives. iOS receives the full opaque square and
uses the platform mask. Both platforms therefore use the same artwork without
introducing transparent iOS corners or a square macOS Dock tile. The superseded
mobile SVG is archived in `legacy/mobile-appicon-source-20260830.svg`; it is no
longer an alternative source inside the active app-icon catalog.

The Chromium overlay contains deterministic size derivatives for runtime theme
resources plus a compiled macOS `app.icns` and `Assets.car`. Regenerate every
consumer with:

```sh
./scripts/generate-branding-assets.sh
```

The generator validates the opaque 1024-pixel master and mask, derives all
desktop PNG sizes, rebuilds `app.icns`/`Assets.car`, and updates the existing
iOS AppIcon PNG in the same staged/rollback-safe publication. Dock, Finder,
Settings, helper apps and the Mobile app no longer have separate artwork.
An application build/install is still required to display changed source assets.

Source integration verification on 8 September: generator EXIT0, 1024-pixel
Mobile artwork without alpha, desktop alpha mask retained, generated 32/128-pixel
icons visually inspected, shell syntax and scoped whitespace clean. The previous
mobile SVG remains recoverable from the archive and Git. No installed bundle was
modified by this asset-generation step.
