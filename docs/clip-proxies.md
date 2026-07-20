# Low-resolution clip proxies (spec)

Status: **proposed** (not built). Owner decision to fold this into the existing
all-intra "keyframe" prep — this spec follows that.

## Why

`Scene.renderScale` (per-scene internal render resolution) already cuts the
**GPU** cost of a scene: YUV→RGB conversion, the contain-fit blit, per-layer
effects, compositing and the post chain all run at the reduced render size
(`SceneRenderer` sizes every layer chain / compositor / post FBO to
`width × height`). That is the Pi's usual bottleneck (V3D fill-rate).

What `renderScale` does **not** reduce is **decode**. `ClipPlayer` wraps
`ofGstVideoPlayer`, which decodes each clip at its **native** resolution
regardless of render scale — a 1080p clip in a 50% scene is still decoded at
1080p, and its full-res frame is still uploaded every frame. On the Pi each
layered clip is its own `v4l2` decode session competing for the one hardware
decoder block, so clip-dense scenes are exactly where this residual cost bites.

A **proxy** — a lower-resolution copy of the clip that the renderer plays
*instead of the original when the scene renders small* — removes that decode
waste.

## Key idea: the proxy IS the keyframe encode, smaller

The all-intra re-encode already exists (`transcode._transcode(intra=True)`,
`-g 1`, triggered by `POST /api/clips/{id}/smooth-reverse`). It makes every
frame independently decodable so ping-pong reverse and tight loop-wrap /
stutter seeks land instantly. A proxy is the **same encode at a smaller
resolution**:

- Encode the proxy all-intra (`-g 1`) too → it inherits smooth reverse + tight
  seeks at the low res. So a low-res scene using stutter / fast loops gets *both*
  wins from one file.
- Generate it from the **same opt-in** as the keyframe prep, so one user action
  ("optimize this clip") yields both the full-res all-intra (in place, as today)
  and the low-res all-intra proxy (new, separate file).

Full-res sharpness stays available for Full/near-Full scenes; the proxy is only
selected when a scene is authored to render small.

## Storage & naming (by convention, like boomerangs)

- Directory: `clips/.proxies/` on `DATA_DIR`. Already excluded from deploys —
  `.rsyncfilter`'s `- bin/data/clips/*` rule covers it (derived, Pi-authored,
  like `.pingpong/` and `.thumbs/`).
- Filename: `<stem>__p<height>.mp4`, e.g. `myclip__p540.mp4`. Deterministic so
  the renderer finds it by convention with no registry read — mirrors
  `transcode.boomerang_path()` / `ShowLoader::boomerangRelPath()`.
- New `config.py` values:
  - `PROXY_DIR = CLIPS_DIR / ".proxies"`
  - `PROXY_HEIGHT = 540` (one tier for v1; see Future)
  - `PROXY_SELECT_MAX_SCALE = 0.66` (renderer uses the proxy when a scene's
    `renderScale` is ≤ this)

## Generation (backend)

Reuse the single-worker queue and the all-intra recipe. Two touch points:

1. `transcode._transcode()` gains an optional `scale_h: int | None`. When set,
   prepend `-vf scale=-2:'min(<scale_h>,ih)'` to the intra args (never upscales;
   keeps even dims). Everything else (nice-19, capped threads, `-g 1`,
   progress parsing, the libx265-teardown-SIGILL tolerance) is unchanged.
2. New job mode `"proxy"`: `src` is the library clip, `dest` is
   `PROXY_DIR/<stem>__p<H>.mp4`. On success, record it on the clip
   (`_record_proxy`) — e.g. `clip["proxy"] = {"height": H, "path":
   "clips/.proxies/<stem>__p<H>.mp4"}` (an object, not a bool, so multi-tier is
   a later list). No in-place replace (unlike `intra`); the original stays.

Endpoint: fold into the existing prep rather than adding a separate button.
Rename the action "Optimize clip" (keep the `/smooth-reverse` path or add
`/optimize`): it enqueues the in-place full-res intra **and** the proxy job, both
visible in the jobs banner. (If a clip is already `intra`, just enqueue the
missing proxy.) Guard like `smooth_reverse`: clip exists, file on disk,
idempotent (re-baking overwrites).

Lifecycle: add `prune_proxies(clip_path)` (sibling of `prune_boomerangs`),
called from `delete_clip` and whenever a clip is re-ingested/replaced.

## Selection (renderer, `ShowLoader`)

At the clip-resolution point (`ShowLoader.cpp` ~line 194, right after
`layer.resolvedPath = it->second`), before the ping-pong check:

```
if (scene.renderScale <= PROXY_SELECT_MAX_SCALE) {
    std::string proxy = "clips/.proxies/" + baseName(layer.resolvedPath)
                        + "__p" + proxyTag + ".mp4";
    if (ofFile::doesFileExist(livepi::userDataPath(proxy)))
        layer.resolvedPath = proxy;
}
```

`scene.renderScale` is already parsed on the scene here. `proxyTag`/threshold
come from config keys mirroring the backend (`clips.proxy_height`,
`clips.proxy_select_max_scale`) so the two stay in sync, same discipline as the
ping-pong key.

Two deliberate v1 boundaries:

- **Selection follows the AUTHORED `renderScale`, not the thermal cap.** Thermal
  is a runtime GPU emergency; swapping the decoded file mid-scene would respin
  the decoder. So thermal keeps cutting GPU cost, and the proxy cuts decode cost
  for scenes *authored* low-res. (A hot box running a full-res scene still
  decodes full-res — acceptable; thermal's job is the fill-rate, not the codec.)
- **Ping-pong takes precedence over the proxy.** A baked boomerang is full-res;
  if a low-res scene also uses `video.pingpong`, use the boomerang (correctness
  of the baked reversal beats decode savings). Low-res boomerangs are Future.

## UI (`ClipLibrary`)

- The clip's "smooth reverse" control becomes "Optimize" (intra + proxy). Show a
  `proxy 540p` badge when `clip.proxy` is present, next to the existing
  intra/ping-pong indicators.
- Nice-to-have: flag a clip that is used by a scene with `renderScale ≤
  threshold` but has no proxy ("this clip is decoded full-res in a low-res
  scene — optimize it").

## Cost

- One extra transcode pass, on the same nice-19 / capped-threads / one-at-a-time
  queue, so it never fights the renderer. 540p all-intra is far cheaper to
  produce than the full-res intra.
- Decode win at play time: ~4× fewer luma samples per frame at 540p vs 1080p —
  the point of the whole thing for clip-dense low-res scenes.
- Storage: all-intra is bitrate-heavy, but at 540p the file stays modest; lives
  on writable `/data`.

## Future (out of v1 scope)

- **Multiple proxy tiers** (e.g. 540p + 360p), `clip.proxies` a list, renderer
  picks the smallest tier ≥ its render res. v1's single 540p tier + a `≤0.66`
  threshold is the 80/20.
- **Low-res boomerangs** (proxy × ping-pong): bake the boomerang from the proxy
  when a scene is both low-res and ping-pong.
- **Auto-generate** proxies for any clip dropped into a low-res scene, instead
  of an explicit opt-in.
- **Display-res awareness**: pick/skip the proxy against the actual panel size
  (720p vs 1080p) rather than a scale threshold alone.

## Implementation checklist

Backend:
- `config.py`: `PROXY_DIR`, `PROXY_HEIGHT`, `PROXY_SELECT_MAX_SCALE`.
- `transcode.py`: `scale_h` arg on `_transcode`; `enqueue_proxy` + `"proxy"`
  worker branch; `_record_proxy`; `prune_proxies`.
- `clips.py`: fold proxy into the optimize endpoint; call `prune_proxies` on
  delete.

Renderer:
- `Config`/`app.json`: `clips.proxy_height`, `clips.proxy_select_max_scale`.
- `ShowLoader`: proxy selection by convention before the ping-pong check.

Frontend:
- `types.ts`: `Clip.proxy?: { height: number; path: string }`.
- `ClipLibrary`: "Optimize" action + `proxy` badge (+ optional low-res-scene
  hint).
