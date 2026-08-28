# Third-party notices

LivePi VideoGlitcher itself is MIT licensed (see [`LICENSE`](LICENSE)). It links
against, vendors, and ships alongside the software listed here, each under its
own terms.

This file exists to travel **with the binaries**: several of these licenses
require their copyright notice to be reproduced in the documentation or other
materials accompanying a binary distribution. It is therefore included in the
golden image (`scripts/build-image.sh`) and in every update bundle
(`scripts/deploy-update.sh`), not just in the source tree.

Nothing listed below imposes a copyleft obligation on LivePi's own source. Every
component is permissively licensed, dynamically linked, or both.

---

## Vendored into this repository

### QR Code generator library — `src/third_party/qrcodegen/`

Used to render the Wi-Fi onboarding QR code on the splash screen.

> Copyright (c) Project Nayuki. (MIT License)
> https://www.nayuki.io/page/qr-code-generator-library
>
> Permission is hereby granted, free of charge, to any person obtaining a copy of
> this software and associated documentation files (the "Software"), to deal in
> the Software without restriction, including without limitation the rights to
> use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
> the Software, and to permit persons to whom the Software is furnished to do so,
> subject to the following conditions:
> - The above copyright notice and this permission notice shall be included in
>   all copies or substantial portions of the Software.
> - The Software is provided "as is", without warranty of any kind, express or
>   implied, including but not limited to the warranties of merchantability,
>   fitness for a particular purpose and noninfringement. In no event shall the
>   authors or copyright holders be liable for any claim, damages or other
>   liability, whether in an action of contract, tort or otherwise, arising from,
>   out of or in connection with the Software or the use or other dealings in the
>   Software.

---

## Linked into the renderer binary

### openFrameworks — MIT

The creative-coding framework the renderer is built on. Not vendored here; built
from a separate checkout (`OF_ROOT`, see `scripts/setup-pi.sh`).

> Copyright (c) 2025 - openFrameworks Community
>
> Permission is hereby granted, free of charge, to any person obtaining a copy of
> this software and associated documentation files (the "Software"), to deal in
> the Software without restriction, including without limitation the rights to
> use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
> of the Software, and to permit persons to whom the Software is furnished to do
> so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in all
> copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
> AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
> OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
> SOFTWARE.

openFrameworks in turn bundles or links a mix of libraries whose licenses are
their own, including: **glm** (MIT), **FreeType** (FTL or GPLv2 — used under the
FTL), **FreeImage** (FIPL / GPLv2 / GPLv3 — used under the FIPL, and dynamically
linked from the system `libfreeimage` on Linux rather than the bundled copy),
**libpng** (PNG Reference Library License), **zlib** (zlib), **pugixml** (MIT),
**kissfft** (BSD-3-Clause), **tess2** (SGI FreeB / MIT), **utf8cpp** (BSL-1.0),
**uriparser** (BSD-3-Clause), **brotli** (MIT), **fmt** (MIT), and **Poco**
(BSL-1.0). See `$OF_ROOT/LICENSE.md` and `$OF_ROOT/docs/libraries.md` for the
authoritative list for your openFrameworks version.

### ofxMidi — Standard Improved BSD License

MIDI input/output (CC + notes), the renderer's primary live control surface.

> Copyright (c) 2011-2023 Dan Wilcox <danomatika@gmail.com>
> All rights reserved.
>
> The following terms (the "Standard Improved BSD License") apply to all files
> associated with the software unless explicitly disclaimed in individual files:
>
> Redistribution and use in source and binary forms, with or without
> modification, are permitted provided that the following conditions are
> met:
>
> 1. Redistributions of source code must retain the above copyright
>    notice, this list of conditions and the following disclaimer.
> 2. Redistributions in binary form must reproduce the above
>    copyright notice, this list of conditions and the following
>    disclaimer in the documentation and/or other materials provided
>    with the distribution.
> 3. The name of the author may not be used to endorse or promote
>    products derived from this software without specific prior
>    written permission.
>
> THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY
> EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
> THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
> PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR
> BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
> EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
> TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
> DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
> ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
> LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
> IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
> THE POSSIBILITY OF SUCH DAMAGE.

### RtMidi — MIT-style (bundled inside ofxMidi)

> RtMidi: realtime MIDI i/o C++ classes
> Copyright (c) 2003-2023 Gary P. Scavone
>
> Permission is hereby granted, free of charge, to any person obtaining a copy of
> this software and associated documentation files (the "Software"), to deal in
> the Software without restriction, including without limitation the rights to
> use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
> of the Software, and to permit persons to whom the Software is furnished to do
> so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in all
> copies or substantial portions of the Software.
>
> Any person wishing to distribute modifications to the Software is asked to send
> the modifications to the original developer so that they can be incorporated
> into the canonical version. This is, however, not a binding provision of this
> license.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
> AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
> OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
> SOFTWARE.

### System libraries (dynamically linked, provided by the OS)

**GStreamer** and its base/good plugin sets (LGPL-2.1+) for video decode,
**Mesa** (MIT) for GL/GLES, **libX11** and the X server (MIT), **GLFW** (zlib),
**Cairo** (LGPL-2.1 or MPL-1.1), **ALSA** (LGPL-2.1+). All are dynamically
linked against the copies Raspberry Pi OS ships, and none are redistributed as
part of the source tree or an update bundle.

---

## Backend (Python), installed by pip into `backend/pylib/`

| Package | License |
|---|---|
| FastAPI | MIT |
| Starlette | BSD-3-Clause |
| Pydantic / pydantic-core | MIT |
| Uvicorn | BSD-3-Clause |
| `uvicorn[standard]` extras — uvloop (MIT/Apache-2.0), httptools (MIT), websockets (BSD-3-Clause), watchfiles (MIT), python-dotenv (BSD-3-Clause), PyYAML (MIT) | as noted |
| python-multipart | Apache-2.0 |
| itsdangerous | BSD-3-Clause |
| anyio | MIT |
| h11 | MIT |
| click | BSD-3-Clause |
| sniffio, idna, typing-extensions, annotated-types | MIT / BSD-3-Clause / PSF |

`backend/pylib/` is a build artifact (a relocatable `pip --target` tree), not
checked into this repository, but it **is** shipped inside the golden image and
inside update bundles. Each package's own license file travels with it in its
`*.dist-info/` directory.

## Frontend (JavaScript/TypeScript)

| Package | License |
|---|---|
| React, React DOM | MIT |
| React Router | MIT |
| Zustand | MIT |
| Vite, `@vitejs/plugin-react` | MIT |
| TypeScript | Apache-2.0 |
| `@types/*` (DefinitelyTyped) | MIT |

Build-time only, except React/React Router/Zustand, which are bundled into
`frontend/dist/` by Vite and shipped.

---

## The golden image

`scripts/build-image.sh` produces a flashable `.img.xz` containing a full
**Raspberry Pi OS Lite (Trixie, arm64)** userland — several thousand Debian
packages under GPL-2.0, GPL-3.0, LGPL, and many permissive licenses. Those
packages are installed unmodified from the official Raspberry Pi OS and Debian
archives; LivePi patches none of them.

**Written offer of source.** Complete corresponding source for every GPL/LGPL
component in a LivePi image is available from the upstream archives it was
installed from:

- Raspberry Pi OS — https://archive.raspberrypi.com/debian/
- Debian — https://deb.debian.org/debian/ (`apt-get source <package>` on the
  running box reproduces any of them exactly)

Anyone distributing a LivePi image — including a pre-flashed box — inherits this
obligation and can satisfy it by passing along this notice. Ship it with the
product.

The image additionally installs **ffmpeg** (LGPL-2.1+ core; Debian's build is
GPL-licensed as a whole due to enabled GPL components) and, on Pisound-equipped
builds, **Blokas' Pisound driver package** (see https://blokas.io/pisound/) from
their apt repository, under their own terms. ffmpeg is invoked as a separate
process for clip transcoding and proxy generation — never linked — so its terms
do not reach LivePi's own code.

---

## Sample content

`bin/data/clips/samples/sample_crt_loop_01.mp4` is a synthetic `ffmpeg testsrc`
color-bar pattern generated for this repository. It is covered by this project's
MIT license along with everything else here.

## Shaders

Everything in `bin/data/shaders/` is original work under this project's MIT
license. `palette.glslinc` implements the well-known cosine-palette *technique*
popularized by Iñigo Quilez (https://iquilezles.org/articles/palettes/); the
credit is to the idea, not to copied code.
