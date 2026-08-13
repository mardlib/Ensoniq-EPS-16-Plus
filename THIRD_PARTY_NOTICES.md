# Third-party notices

This project uses or adapts the following third-party work. These notices do
not grant rights to Ensoniq firmware, operating-system images, manuals,
trademarks or other copyrighted material supplied separately by the user.

## Musashi

Musashi is used as the Motorola 68000 execution core.

Project: https://github.com/kstenerud/Musashi

Copyright 1998–2002 Karl Stenerud

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

The VST3 build creates a build-directory-only thread-local adaptation of
Musashi's execution globals so multiple plug-in instances can run
independently. The upstream source is kept as an external dependency and is
not copied into this repository.

## JUCE

JUCE 8 provides the VST3 plug-in wrapper, DAW audio/MIDI integration, plug-in
state, windowing, graphics and GUI framework.

Project: https://github.com/juce-framework/JUCE

JUCE is dual-licensed under AGPLv3 and the commercial JUCE licence. The
applicable JUCE licence must be observed when building or distributing the
plug-in. The binary package includes the `JUCE-LICENSE.md` supplied with the
JUCE checkout used for the build.

The VST3 SDK used through JUCE remains under its own licence as documented by
JUCE.

## MAME Ensoniq devices

MAME is not linked or embedded as this project's emulator framework. Its
Ensoniq system drivers and device implementations were used as technical
hardware and protocol references:

https://github.com/mamedev/mame/tree/master/src/mame/ensoniq

The standalone `native/es5505_core.c` implementation is informed by the
register behavior, interpolation, filtering, looping and volume model of the
BSD-3-Clause MAME ES5505/ES5506 device by Aaron Giles:

https://github.com/mamedev/mame/blob/master/src/devices/sound/es5506.cpp

The standalone `native/es5510_core.c` adapts ES5510 pipeline and instruction
semantics from the BSD-3-Clause MAME ES5510 device by Christian Brunschen:

https://github.com/mamedev/mame/tree/master/src/devices/cpu/es5510

Copyright Aaron Giles, Christian Brunschen and MAMEdev contributors.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
3. Neither the name of the copyright holder nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

MAME is a registered trademark of Gregory Ember. This project is not
affiliated with or endorsed by MAMEdev.

## Ensoniq material and hardware references

The Ensoniq EPS-16 Plus service manual, user-supplied ROM/KPC firmware,
operating-system disks, real hardware photographs and captured OS/KPC traffic
were used to verify device behavior and mappings. The repository and binary
package do not include or redistribute those copyrighted Ensoniq files.

Ensoniq and EPS-16 Plus are trademarks of their respective owners. This
project is an independent preservation and emulation effort and is not
affiliated with or endorsed by Ensoniq.
