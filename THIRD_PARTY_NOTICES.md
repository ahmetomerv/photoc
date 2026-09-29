# Third-party notices

photoc's own source is licensed under the MIT License in `LICENSE`.
The following libraries are installed separately and dynamically linked;
release archives do not bundle their binaries or source. Build tools and
optional test tools are not runtime dependencies.

## libxml2

[libxml2](https://gitlab.gnome.org/GNOME/libxml2) parses standard XMP packets
for selective privacy removal. It is installed separately and dynamically
linked. libxml2 uses the MIT License; retain the notice supplied by the
installed package when redistributing its library. No libxml2 binary or source
is bundled with photoc.

## libexif

[libexif](https://github.com/libexif/libexif) is used under the GNU Lesser
General Public License version 2.1. Some source files permit LGPL 2.0 or later;
the upstream `COPYING` supplies version 2.1, reproduced in
`licenses/libexif/COPYING`. libexif retains its upstream authors' copyrights.
Its source and individual notices are available from the upstream repository
and from the package manager providing the installed library.

The ARW metadata backend reuses libexif's byte-order utilities, entry ownership,
and common field interpretation. Its bounded TIFF directory reader is original
photoc code; no RAW decoder, new library, or third-party implementation is
bundled. The dependency and licensing terms are unchanged.

Compatible replacement shared libraries can be installed without rebuilding
photoc. photoc source and build instructions are also available so users can
rebuild against a modified library. If distributing a statically linked build
or bundling dependency libraries, review their additional distribution terms.

## libjpeg-turbo (TurboJPEG and libjpeg APIs)

This software is based in part on the work of the Independent JPEG Group.

[libjpeg-turbo](https://github.com/libjpeg-turbo/libjpeg-turbo) supplies both the
TurboJPEG API used by image operations and the streaming libjpeg API used by
the read-only JPEG audit. Both shared libraries are installed separately and
dynamically linked; no additional upstream source or library binary is bundled.

libjpeg-turbo uses the IJG
license and the Modified BSD License; its SIMD implementation also uses the
zlib license, whose conditions are subsumed by the IJG license as explained
upstream. The TurboJPEG API wraps the libjpeg implementation, so both IJG
and Modified BSD terms apply. The unaltered IJG README is included in
`licenses/libjpeg-turbo/README.ijg`.

The full upstream license summaries for 3.2.0 (the audited macOS library)
and 2.1.2 (the Ubuntu 22.04 release build dependency) are included in
`licenses/libjpeg-turbo/`. Package managers can update these versions; check
their installed notices as well. Both BSD notices follow so these materials
also accompany raw executable downloads in GitHub release notes.

### Modified BSD notice from libjpeg-turbo 3.2.0

Source: https://github.com/libjpeg-turbo/libjpeg-turbo/blob/3.2.0/LICENSE.md

Copyright (C) 2009-2026 D. R. Commander<br>
Copyright (C) 2018-2023 Randy <randy408@protonmail.com>

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

- Redistributions of source code must retain the above copyright notice,
  this list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.
- Neither the name of the libjpeg-turbo Project nor the names of its
  contributors may be used to endorse or promote products derived from this
  software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS",
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

### Modified BSD notice from libjpeg-turbo 2.1.2

Source: https://github.com/libjpeg-turbo/libjpeg-turbo/blob/2.1.2/LICENSE.md

Copyright (C)2009-2021 D. R. Commander.  All Rights Reserved.<br>
Copyright (C)2015 Viktor Szathmáry.  All Rights Reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

- Redistributions of source code must retain the above copyright notice,
  this list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.
- Neither the name of the libjpeg-turbo Project nor the names of its
  contributors may be used to endorse or promote products derived from this
  software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS",
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
