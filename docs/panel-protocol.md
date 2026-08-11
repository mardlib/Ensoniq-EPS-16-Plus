# EPS-16 Plus panel protocol

Status: framing, control mapping, VFD fields, cursor forms and Sampling Level
Detect transport verified against original-OS traces, 2026-08-11.

The KPC sends each panel transition as two bytes:

- press: `code | 0x80`, `0x00`
- release: `code`, `0x00`

A complete host-generated click therefore contains four bytes.
This zero-valued second byte is specific to panel buttons; keyboard notes use
velocity/pressure bytes and must not be used as the button framing model.
The live browser dispatches the host-generated click on `pointerdown`, so the
KPC press packet reaches the original OS at physical button-down time rather
than after the browser's click/release event.
The live adapter holds ordinary panel buttons for 50 ms of emulated CPU time
before sending the release packet. The earlier 1 ms pulse was shorter than a
physical click and could be rejected by context-dependent OS debounce paths;
UP/DOWN clicks retain their already verified shorter timing. LEFT/RIGHT use
the ordinary 50 ms hold because they also select complete sampling pages; the
short cursor pulse is rejected by that original-OS debounce path.
The VST bridge likewise spaces queued panel transitions by at least 50 ms of
emulated CPU time. Without that interval, a short mouse click can place press
and release in the same DAW block and the KPC firmware never observes the key.

Sampling input/options updates are short KPC display frames terminated by
`f7`, rather than ordinary frames delimited by ASCII `f` (`66`). The KPC model
counts `f7` as a completed frame; otherwise its ready-byte burst continues
indefinitely and the original OS panel parser locks after several updates.

Important correction: the values printed in the service manual are the glyphs
shown by the panel's standalone self-test, not UART matrix indices. The loaded
OS accepts matrix indices `00..25` and translates them through its table at ROM
address `c02252`. Consequently the packet examples below are retained only as
the disproven first hypothesis and must not be used by the live adapter.

The service manual exposes the raw matrix in Display Self-Test Mode. Printable
codes appear as their ASCII character. A displayed character with its decimal
point lit is the corresponding lower control code (`character - 0x20`). Four
control codes are documented as `Home Cursor`; their positions in the same
6-bit character map identify them as `08`, `09`, `0a`, and `0d`.

That standalone self-test map is not the encoding used for editable numeric
fields in the main-CPU-to-KPC protocol. The original OS table at CPU address
`c0228c` maps dotted digits `0.` through `9.` to
`21 23 25 28 29 3a 3b 5b 5c 5d`. The live decoder applies this table only
between the OS field-open (`62`) and field-close (`72`) commands and transports
the decimal point as a per-cell VFD attribute. Bytes after the 22 display cells
remain lamp, meter, or status commands. No page names or parameter choices are
inferred by the browser.

The field-open command `62`, incremental-update command `63`, and field-close
command `72` delimit original-OS field transport, but do not by themselves
select a visible cursor. Hardware photos and byte-for-byte traces of the same
`PAN MOD=LFO   * +25` page identify the additional sequence `62 60 03` as the
lower-segment cursor selection. The constant `03` is not a width: the following
OS-supplied padded content determines the mask. Thus the selected modulation
source lights the lower segment in all five cells `LFO  `; RIGHT removes that
mask and selects the three signed amount cells `+25`. A plain `62 ... 72`
Filter MODE field produces no cursor segments. No page text or parameter value
is inspected to make this distinction.

The original OS also has a cursorless direct-address update form. On the
LOAD/Instrument name-and-volume page it sends the zero-based VFD cell address
`14`, followed by the two replacement volume characters. A traced change from
99 to 21 is therefore `14 32 31`; it updates cells 20 and 21 without redrawing
the instrument name. This sequence contains no `62 60 03` cursor selection,
so the physical display has no lower-segment cursor on this page. The decoder
uses one-byte lookahead to distinguish a direct cell address from other low
transport bytes and renders only the OS-supplied characters.

Name editing uses a second cursor form. The original sequences `63 6a 67` and
`63 69 65 67` move its single lower-segment cursor forward and backward;
`63 72 67 72` reasserts it. A `63` followed by a glyph remains an incremental
write at that selected cell. These meanings agree with both the captured
EPS-16 Plus OS traffic and the independent hardware sniffer. They are decoded
as commands, not inferred from the displayed `NAME=` text.

Sampling Level Detect is another original-OS display mode. The exact setup
frame `15 73 00 f7` enables it; subsequent values `00..0e` are the observed
number of left-to-right vertical VFD bars. Trigger Sensitivity is independent:
the OS writes its star with `<one-based cell> 2a` and erases the former star
with `<one-based cell> 5e`. The meter value is therefore never scaled by the
trigger setting, and neither value is calculated from the DAW input level.
Because a bar count and a trigger cell address share the low-byte range, the
decoder holds one byte of lookahead and commits it only after the following
transport byte distinguishes the two authentic forms.

The byte after the common `60` prefix selects a text/cursor mode and must not
be mistaken for a direct cell address. The physical write cursor wraps to cell
zero after cell 21, as independently observed by the hardware sniffer. This
combination preserves full-width dialogs such as `PICK SAMPLE INSTRUMENT`
while allowing standalone low-byte direct updates.

The keypad/display schematic identifies the physical glass as Futaba
`FIP 22AM5R`: 22 multiplexed alphanumeric cells, each with fourteen directly
driven segments `SA..SN` and a decimal point. The VST editor now draws that
cell topology instead of using a desktop font. The completed text, decimal
mask and 22-bit cursor-segment mask are published together; reading transient
parser state after its trailing `72 00` reset made valid lower segments appear
to disappear intermittently.

The VST decoder also retains the three raw 16-way lamp banks addressed by the
paired command ranges `74..7c`. Each command consumes its following physical
index byte; the resulting on/flash masks are copied to the editor without
examining the 22-character text. The protocol directions are asymmetric:

- `74/75/76` switch the 16 Instrument/Track LEDs off/on/blinking. Indices
  `0..7` are the upper **Loaded** row and `8..15` the lower **Selected** row.
- `77/78/79` switch the left annunciator bank on/off/blinking.
- `7a/7b/7c` switch the right annunciator bank on/off/blinking.

The complete annunciator index map was cross-checked against the original OS
traffic, the photographed rack glass and the independent
[Ensoniq display hardware sniffer](https://github.com/balamutang/ensoniqdisplay).
The editor is a direct electrical mirror: no display string or menu state
selects a lamp.

Left bank (`77..79`): `02 SEQ`, `03 MIDI`, `04 SONG`, `05 EDIT`,
`06 FILTER`, `07 BANK`, `08 WAVE`, `09 LFO`, `0a PITCH`, `0b LAYER`,
`0c SYSTEM`, `0d CMD`, `0e INST`, `0f LOAD`. Indices `00/01` are not printed
on the EPS-16 Plus rack glass.

Right bank (`7a..7c`): `01 BAR`, `02 SONG`, `03 REC`, `04 SEQ`, `05 TRACK`,
`06 REP`, `07 BEAT`, `08 MACRO`, `09 CLOCK`, `0a STEP`, `0b STOP`, `0c PLAY`,
`0d AMP`, `0e ODUB`, `0f ENV`. Index `00` is not printed.

| Control | Self-test glyph code | Deprecated packet hypothesis |
|---|---:|---|
| LOAD | `38` | `b8 00 38 00` |
| COMMAND | `24` | `a4 00 24 00` |
| EDIT | `11` | `91 00 11 00` |
| INSTRUMENT | `2d` | `ad 00 2d 00` |
| SEQ-SONG | `33` | `b3 00 33 00` |
| SYSTEM-MIDI | `39` | `b9 00 39 00` |
| EFFECTS | `27` | `a7 00 27 00` |
| 1 / ENV1 | `2b` | `ab 00 2b 00` |
| 2 / ENV2 | `30` | `b0 00 30 00` |
| 3 / ENV3 | `31` | `b1 00 31 00` |
| 4 / PITCH | `36` | `b6 00 36 00` |
| 5 / FILTER | `37` | `b7 00 37 00` |
| 6 / AMP | `3c` | `bc 00 3c 00` |
| 7 / LFO | `3d` | `bd 00 3d 00` |
| 8 / WAVE | `08` | `88 00 08 00` |
| 9 / LAYER | `09` | `89 00 09 00` |
| 0 / TRACK | `2a` | `aa 00 2a 00` |
| Up | `13` | `93 00 13 00` |
| Down | `14` | `94 00 14 00` |
| Left | `2e` | `ae 00 2e 00` |
| Right | `2f` | `af 00 2f 00` |
| CANCEL-NO | `3f` | `bf 00 3f 00` |
| ENTER-YES | `0d` | `8d 00 0d 00` |
| Instrument-Track 1 | `20` | `a0 00 20 00` |
| Instrument-Track 2 | `26` | `a6 00 26 00` |
| Instrument-Track 3 | `2c` | `ac 00 2c 00` |
| Instrument-Track 4 | `32` | `b2 00 32 00` |
| Instrument-Track 5 | `22` | `a2 00 22 00` |
| Instrument-Track 6 | `0a` | `8a 00 0a 00` |
| Instrument-Track 7 | `15` | `95 00 15 00` |
| Instrument-Track 8 | `34` | `b4 00 34 00` |
| EFFECT SELECT-BYPASS | `12` | `92 00 12 00` |
| SAMPLE | `3e` | `be 00 3e 00` |
| RECORD | `10` | `90 00 10 00` |
| STOP-CONTINUE | `35` | `b5 00 35 00` |
| PLAY | `16` | `96 00 16 00` |

## Two panel faders

The faders are analog inputs, not members of the 36-button UART matrix.

| Control | Hardware source | Range | Encoding | Meaning | Persistence |
|---|---|---:|---|---|---|
| VOLUME | ES5505 parallel ADC, channel 5 | `0..1023` | 10 bits left-aligned in bits 15..6 | Master output level | Physical/runtime state; not an instrument parameter |
| DATA ENTRY | EPS-16 Plus analog input channel 3, scanner phase `OPR & 70 = 10` (`OP7` is the independent LINE/MIC output) | GUI `0..1023` maps to raw ADC `0..715`; calibrated OS span is `28..687`, with 28 counts of analog overtravel at both ends; Analog Test/OS reports `0..255` | Linear electrical mapping; browser transport coalesces pending events to the newest physical position; OS performs field scaling, smoothing and endpoint hysteresis | Context-dependent change of the selected OS field | Original ROM zero reference `0x0700`, `0x00c6` multiplier plus one doubling, and 0..255 scaling verified; DOWN and DATA selection of MIC retain the same scanner channels and original-OS voice setup |

The emulated reset values are `1023` (`0xffc0`) for VOLUME and raw ADC `358`
(`0x5980`) for centered DATA ENTRY. The browser's full visible travel is
mapped to raw ADC `0..715`. The calibrated EPS span is `28..687`; the small,
symmetrical 28-count overtravel makes both endpoints deterministic despite the
original ROM's smoothing and hysteresis, without creating large dead zones.

## Current boundary

The KPC poll and calibration-state model now advances the original OS through
`TUNING KBD - HANDS OFF` and `KEYBOARD TUNED`. WD1772/MC68450 directory reads
and runtime disk swaps now work. An end-to-end sweep against the live OS has
verified these matrix indices against the original OS: COMMAND `06`, EDIT
`05`, INSTRUMENT `0f`, LOAD `1a`, SEQ-SONG `15`, SYSTEM-MIDI `1b`, and
EFFECTS `09`. The numeric page keys are also verified: 0-9 map to `0c`, `0d`,
`12`, `13`, `18`, `19`, `1e`, `1f`, `24`, and `25`.

INSTRUMENT and LOAD were disambiguated with a complete original-OS workflow:
create an instrument, layer, and wavesample, then press COMMAND followed by
the candidate page key. Raw `0f` reaches `CREATE NEW INSTRUMENT`; raw `1a`
returns to the LOAD context and shows `NO INSTRUMENTS`. The earlier conclusion
that `0f` was LOAD came from pressing it while the OS was already in LOAD mode
with the Instrument page retained, where the physical INSTRUMENT key naturally
opens the instrument file browser. The former `05` mapping is EDIT; `04` is
inert after a track is selected and must not be used as LOAD. Matrix index `22`
is not LOAD: in the `PICK INSTRUMENT BUTTON` destination prompt it starts
loading, so it is an Instrument/Track candidate.

Matrix index `21` is live-observed, in the loaded-instrument context, to toggle
the instrument display between name plus volume and name-only. Its physical
control label remains unresolved.

ENTER/YES is live-verified as matrix index `23`. Its press packet `a3 00`
changes `FILE 4  JAZZ BASS` to `PICK INSTRUMENT BUTTON` in the original OS;
the release packet is `23 00`. Instrument/Track 1 is live-verified as matrix
index `02`. In the destination prompt, its press packet `82 00` leaves the
prompt unchanged, while release `02 00` starts `LOADING JAZZ BASS` and reaches
`FILE LOADED`. This behavior matches observation on original hardware.

The original-OS translation table also makes several families structurally
unambiguous before their final live confirmation. Instrument/Track 1-8 are OS
values `00..07`, giving raw indices `02,08,0e,14,04,22,1c,16`. CANCEL/NO is
cursor value `23`, raw `21`. Raw `1b` is SYSTEM-MIDI: in the initial LOAD
context, press `9b 00` reaches the system/disk directory page and reports
`NO DIRECTORIES`. SAMPLE is raw `20`: press `a0 00` from `NO INSTRUMENTS`
reaches `PICK SAMPLE INSTRUMENT` in the original OS without a pre-existing
instrument, layer, or wavesample. A
subsequent live test confirms raw `07` as EFFECT SELECT:
it opens the effect-selection menu. The earlier `COULD NOT BE DOWNLOADED`
response was a downstream ES5510 host-access/execution issue, not a
panel-matrix failure. The device path now pauses the DSP during program upload
and verifier readback; successful live effect selection remains to be retested.
The three sequencer controls are now identified without relying on a menu or
display string.  The service-manual keypad self-test prints `0.`, `5`, and
`6.` for RECORD, STOP/CONTINUE, and PLAY.  Combining those physical glyphs
with the KPC scan order and the ROM translation table below gives raw matrix
positions `03`, `17`, and `1d`, respectively (OS values `40`, `43`, and
`41`).  An original-OS round trip confirms that holding raw `03` while raw
`1d` is pressed creates and records `SEQUENCE 01`; raw `17` then enters STOP.
Raw `00`/`01` are the two remaining non-button matrix events.

## Original-OS matrix translation

The original boot ROM table at CPU address `c02252` translates the raw KPC
matrix index to the OS-internal button value. These bytes are recorded directly
from `eps16plus-rom.bin`; labels are not inferred from them.

| Raw | OS | Raw | OS | Raw | OS | Raw | OS |
|---:|---:|---:|---:|---:|---:|---:|---:|
| `00` | `3a` | `01` | `3b` | `02` | `00` | `03` | `40` |
| `04` | `04` | `05` | `13` | `06` | `11` | `07` | `12` |
| `08` | `01` | `09` | `18` | `0a` | `20` | `0b` | `21` |
| `0c` | `30` | `0d` | `31` | `0e` | `02` | `0f` | `17` |
| `10` | `22` | `11` | `24` | `12` | `32` | `13` | `33` |
| `14` | `03` | `15` | `16` | `16` | `07` | `17` | `43` |
| `18` | `34` | `19` | `35` | `1a` | `14` | `1b` | `15` |
| `1c` | `06` | `1d` | `41` | `1e` | `36` | `1f` | `37` |
| `20` | `10` | `21` | `23` | `22` | `05` | `23` | `25` |
| `24` | `38` | `25` | `39` |  |  |  |  |

The live panel remains a transport adapter: it sends raw press/release packets
and renders OS/KPC output. Mode/page toggling, including EDIT returning between
the last edit page and the layer/wavesample selection display, is owned by the
original OS and must not be reproduced as browser-side menu logic.

The original OS also defines when that EDIT return target exists. While an
EDIT parameter page is active, repeated EDIT presses toggle between that page
and the instrument/layer/wavesample selection display. Entering COMMAND clears
the return target: the next EDIT press opens the selection display and further
EDIT presses remain there. Pressing an EDIT page key such as ENV, PITCH, or LFO
establishes a new return target and restores the toggle. This sequence was
reproduced through the live raw matrix adapter and must not be "fixed" with a
browser-side page-history buffer.

UP is live-verified as matrix index `0a`; DOWN is `0b`. The earlier labels
were reversed: sending `0b` from the UP control counted upward, while `0a`
from DOWN counted downward. Holding either for 100 ms triggers OS auto-repeat.

LEFT is live-verified as matrix index `10` and RIGHT as `11`. In the LOAD file
browser they switch the original OS display between `FILE 3 PIANO 241` and
`241 BLKS`.

Keyboard events use the same two-byte transport but are distinguished from
panel buttons by a nonzero second byte. The first byte is the zero-based key
index with bit 7 set on press and clear on release. The loaded OS adds `0x24`
to the index, so Middle C is index `18`. The verified JAZZ BASS example is
press `98 64` (velocity 100) and release `18 01`; after Track 1 is selected,
this starts and stops a live ES5505 voice.

In the sampling level-detect context, ENTER starts recording when the KPC
completes the release transaction, matching the hardware. Channel-B receive
IRQ must fall immediately after each byte is read; otherwise the sampling
overlay sees a spurious empty interrupt and raises `ERROR 145`. The physical
transition is the four-byte press/release packet followed by one KPC-ready
byte. Sampling-meter display traffic is not allowed to manufacture additional
ready bytes. Pending target-selection readiness is canceled when the original
sampling overlay enters RECORD. The next deliberate ENTER press then stops
recording and reaches `PLAY ROOT KEY`.

The Level-Detect VFD traffic places the trigger-threshold star with the
observed two-byte pair `<one-based cell> 2a` (`02 2a` at the initial
position). When it moves the marker, the OS erases the old cell with
`<one-based cell> 5e` before sending the new star pair. The decoder recognizes
a cell byte only as part of those exact pairs: low KPC/VFD bytes in general are
not character positions. Trigger threshold and the separate, supplementary
Pre-Trigger function must not be conflated.

Four RIGHT presses from Level Detect reach `INPUT LEVEL`. The original OS
stores LINE as low-RAM `0211=01` and MIC as `0211=00`; this value drives the
emulated CD4053 feedback switch directly. Audio routing never infers the mode
from the rendered `INPUT LEVEL=...` text. The machine regression verifies the
default LINE state, changes the original field to MIC with Data Entry, and
restores LINE together with the serialized analog-filter history.

The original Level-Detect program sends `15 73 00 f7` followed by single
low-byte values `00..0e`. Emulator traces at defined input amplitudes and a
physical EPS-16 Plus display photograph verify that this value is the number
of vertical VFD bars lit from left to right. The independently addressed
`<one-based cell> 2a` marker is the Trigger Sensitivity position; moving it
does not change the meter value. The VST renders only these original OS/KPC
values and never infers a meter from host input amplitude.

ENTER must retain the ordinary byte-by-byte display handshake used by OS
dialogs. Disabling that handshake globally leaves the OS waiting after clearing
the VFD, or advances a message only when another button event arrives. The KPC
transport distinguishes sampling ENTER from an ordinary dialog through the
original overlay's active ES5510 ADC polling: sampling ENTER does not arm a
display drain, so asynchronous meter traffic cannot generate an additional
`ff`; ordinary dialogs retain the full handshake.
