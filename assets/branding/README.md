# AhoiBrowser icon source

`ahoi-browser-icon-1024.png` is the canonical raster master for the native
AhoiBrowser application mark. The selected source was created with the built-in
ImageGen workflow and then normalized to a deliberately flat four-entry palette
for deterministic rendering. It was reviewed at 16, 20, 32, 64, 128 and 1024
pixels before integration.

The identity combines two geometric sail planes into an abstract `A`, with one
restrained coral Brause-like accent. It intentionally avoids gradients, gloss,
mascots, retro packaging and third-party browser marks.

The Chromium overlay contains deterministic size derivatives for runtime theme
resources plus a compiled macOS `app.icns` and `Assets.car`. Regenerate every
consumer with:

```sh
./scripts/generate-branding-assets.sh
```

The generator validates the 1024-pixel master, derives every small-size PNG
from it and rebuilds the native asset catalog. This keeps Dock, Finder,
Settings, helper apps and in-product surfaces from drifting apart.
