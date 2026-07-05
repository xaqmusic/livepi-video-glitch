# Sample clips

`docs/architecture.md`'s asset strategy calls for 2-4 short (5-15s), modestly
downscaled (720p or lower) sample clips here, checked into git, so the repo
runs from a fresh clone with zero external fetch step -- `config/app.json`'s
default scene points at `clips/samples/sample_crt_loop_01.mp4`.

`sample_crt_loop_01.mp4` here is currently a synthetic `ffmpeg testsrc` color-bar
pattern (generated with `ffmpeg -f lavfi -i testsrc=duration=10:size=1280x720:rate=30
-pix_fmt yuv420p sample_crt_loop_01.mp4`), used only to smoke-test the
ClipPlayer/ShaderChain pipeline end-to-end. It has no CRT/VHS texture of its
own -- swap in real lo-fi footage whenever it's available (doesn't need to be
final setlist material, just something with real motion/edges to glitch).
