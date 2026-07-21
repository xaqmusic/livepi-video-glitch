# Generator thumbnails

Drop one PNG per generator here, named by its **generator key** (from
`backend/effects_manifest.json`). They show in the layer source picker's 64×36
slot (and scale down cleanly), the same way clip thumbnails do.

- **Format:** PNG (transparency welcome)
- **Dimensions:** 320×180 (16:9); 640×360 for extra crispness on retina/phone
- **Served at:** `/thumbs/generators/<key>.png` (Vite folds `public/` into the build)

Expected files:

| Key         | Label         |
|-------------|---------------|
| `plasma`    | Plasma        |
| `copper`    | Copper Bars   |
| `laser`     | Note Lasers   |
| `fire`      | Fire          |
| `starfield` | Starfield     |
| `blobs`     | Note Blobs    |

Until a file exists the picker falls back to a ✳ glyph, so you can add them one
at a time.
