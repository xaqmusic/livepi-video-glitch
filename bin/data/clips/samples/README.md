# Sample clips

`docs/architecture.md`'s asset strategy calls for 2-4 short (5-15s), modestly
downscaled (720p or lower) sample clips here, checked into git, so the repo
runs from a fresh clone with zero external fetch step -- `config/app.json`'s
default scene points at `clips/samples/sample_crt_loop_01.mp4`.

**Not yet added.** These need real (or at least real-ish) video content,
which can't be fabricated as part of scaffolding -- add a few short clips
here (any lo-fi/CRT/VHS-textured footage works fine for exercising the
shader chain in Phases 1-4; they don't need to be final setlist material).
