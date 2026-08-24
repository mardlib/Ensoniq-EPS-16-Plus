# EPS-16 Plus GUI parameter mapping

Status: draft 1, 2026-07-02

This document is the contract between the host GUI and the emulated EPS-16
Plus. The fidelity-first rule is: an OS-owned value is accessed through panel
automation until a direct RAM mapping has been independently located, verified
in more than one context, and covered by a read/write/restore test.

## Status vocabulary

- `GUI model`: implemented only in the diagnostic browser panel.
- `Transport proven`: panel bytes are consumed by the unmodified OS.
- `Manual verified`: page name, order, and direct number match the supplied
  EPS-16 Plus manual.
- `Round-trip pending`: the real OS value has not yet been read, changed, read
  back, and restored through the GUI bridge.
- `Not exposed`: internal implementation state, not a user parameter.

`Unknown` is intentional. It must not be replaced with an inferred RAM address
or numeric scale.

## Currently exposed GUI controls

| GUI parameter/control | Source | Read method | Write method | Value range | Scaling | Persistence | Known risks | Test status |
|---|---|---|---|---|---|---|---|---|
| Virtual disk image / EFE import | Panel automation / host media adapter | `OS` reinserts the discovered system disk; `NEW` creates a formatted empty EPS disk; `LOAD` chooses an EFE or disk image; parsed EPS directory | Mount logical 800 KiB image, or validate an EFE header and construct its directory/FAT image; issue the physical one-shot disk-change input; accept WD1772/MC68450 sector writes; `SAVE` exports IMG or HFE v1 | `.EFE`, `.IMG`, decoded/encoded HFE v1 | EFE header plus unchanged native 512-byte file blocks | Mounted bytes are included in VST machine state; explicit Save writes a new external image | Ejecting unsaved in-memory changes, malformed/oversized EFE, wrong image, or overwriting the wrong host file | Synthetic import and EpsLin image comparison automated; RX5_CHINA reaches `FILE 1`, loads into two tracks and exposes its original-OS edit pages; blank disk reports `NO INSTRUMENTS`; full-disk HFE roundtrip automated |
| Master Volume fader | Hardware register | Read ES5505 parallel ADC channel 5 | Set virtual 10-bit analog input | `0..1023` | Left shift 6 to ES5505 bits 15..6 | Runtime/host state; not instrument data | Host gain must not be confused with OS instrument volume | ES5505 ADC read tested; live GUI bridge pending |
| Data Entry fader | EPS-16 Plus analog input / panel automation | Observe channel 3 during scanner phase `OPR & f0 = 90` and the selected OS field | Map GUI `0..1023` linearly to raw ADC `0..715`; serialize requests and coalesce pending browser events to the newest position | GUI `0..1023`; raw ADC `0..715`; calibrated span `28..687`; OS/Analog Test `0..255` | ROM subtracts `0x0700`, multiplies by `0x00c6`, doubles once, then applies field scaling; 28-count overtravel defeats endpoint hysteresis | Fader position is transient; selected OS parameter owns persistence | Sending the entire raw 10-bit ADC range creates a large upper dead zone; exact calibrated endpoints may not cross a hysteretic boundary after reversal | Original-ROM phase, zero reference, multiplier, and 0..255 scaling verified; repeated-sweep live retest pending |
| Mode: Load / Command / Edit | Panel automation | Decode OS mode indicator | Send four-byte matrix press/release packet | 3 states | Identity | OS session | Self-test glyphs are not wire indices | All three matrix indices live-verified |
| Page selection (14 buttons) | Panel automation | Decode OS page indicator | Send four-byte matrix press/release packet | 14 page IDs | Identity | OS session/context | Same physical buttons also act as numeric direct-select keys | All 14 matrix indices live-verified |
| Up / Down | Panel automation | Observe changed 22-character display | Send matrix indices `0a` / `0b` | Previous/next item or value step | OS-defined | Parameter-dependent | Auto-repeat starts on a 100 ms hold | Live-verified in ED-001 file browser; reversed labels corrected 2026-07-13 |
| Left / Right | Panel automation | Observe changed field/display | Send matrix indices `10` / `11` | Previous/next field; LOAD toggles name/blocks | OS-defined | Parameter-dependent | Meaning changes by page | Live-verified with `FILE 3 PIANO 241` / `241 BLKS` |
| Enter / Yes | Panel automation | Observe prompt/state transition | Send matrix index `23` press/release | Trigger | None | Operation-dependent | May execute destructive COMMAND operation | Press live-verified: ED-001 file selection reaches `PICK INSTRUMENT BUTTON` |
| Cancel / No | Panel automation | Observe return state | Send raw matrix index `21` (OS cursor value `23`) | Trigger | None | Operation-dependent | Context-sensitive cancellation | Mapping structurally derived from original-OS cursor-value sequence; live confirmation pending |
| Instrument/Track 1 | Panel automation | Read selected/loaded LED and display | Send matrix index `02` press/release | Track 1; context-sensitive press/release semantics | Identity | OS session; data persists only when saved | Replaces occupied slot during load | Release live-verified: loads ED-001 `JAZZ BASS` and reaches `FILE LOADED` |
| Instrument/Track 2-8 | Panel automation | Read selected/loaded LEDs and display | Send raw indices `08,0e,14,04,22,1c,16` | 2-8; context-sensitive press/release semantics | OS values are contiguous `01..07` | OS session; data persists only when saved | Replaces occupied slot during load | Structurally derived from original-OS translation table; live confirmation pending |
| Instrument Loaded/Selected LEDs | KPC lamp bank | Decode raw `74/75/76` plus physical index | Read-only GUI mirror | Upper indices `0..7` loaded; lower `8..15` selected; flashing selected means stacked | Identity | OS performance state | Must not be inferred from instrument names or RAM | Protocol sniffer, OS traces and physical rack layout verified |
| Virtual keyboard note | KPC panel transport | Observe OS voice allocation and ES5505 registers | Send key index plus nonzero velocity/release byte | 61 key indices; velocity/pressure byte | OS adds `0x24` to key index | Runtime performance state | Loaded track must be selected after leaving LOAD mode | Middle C `98 64` / `18 01` live-verified with JAZZ BASS and nonzero stereo PCM |
| Pitch Bend / Mod Wheel / Poly Aftertouch | Physical keyboard controller inputs plus MC68681 channel-A MIDI receiver | Original OS reads pitch-wheel ADC channel 0, modulation-wheel ADC channel 2 and MIDI RHRA | Invert conventional channel-1 14-bit Pitch Bend onto ADC channel `0`; invert channel-1 CC1 onto ADC channel `2`; pass Note On/Off directly to the KPC path; serialize channel-1 `A0` through the MIDI receiver; ignore member-channel MPE expression and Channel Pressure | Pitch `0..16383`; CC1 and pressure `0..127` | Hardware ADC span `0..1023`; MIDI at 31.25 kbit/s; OS owns modulation routing and per-voice response | Runtime performance state | Physical EPS wheels are global; direct Note-On/Off remains separated from pressure for Push compatibility | Original-OS regressions prove ascending pitch, all pressure bytes consumed through RHRA, and an immediate KPC Note Off after a dense pressure stream without artificial key-down packets |
| Record / Stop-Continue / Play | Physical KPC matrix | Read sequencer status indicator | Raw `03` / `17` / `1d`; Shift-click PLAY sends overlapping `03`+`1d` press/release edges | Trigger/toggle | None | Sequence/song state | Can modify an armed sequence | Service self-test glyphs plus ROM matrix translation verified; original OS creates `SEQUENCE 01`, enters REC, then STOP |
| DAW MIDI Clock / Transport | MC68681 channel-A MIDI receiver | Original OS reads RHRA/SRA and IRQ3 | Host PPQ/tempo/transport becomes `f8` at 24 PPQN plus `fa/fb/fc` | MIDI realtime | Host sample position to serial byte timing | Original sequencer state | EPS CLOCK SOURCE must be MIDI; host loop/seek restarts transport | Generator timing unit-tested; original OS consumes serialized bytes through RHRA without changing the direct Note-On/Off path |
| Sample | DAW/browser input + panel automation | Source PCM remains at host rate through the component-derived analog frontend; ADC conversions enter ES5510 serial input 0 and the original sampling program writes the filtered result to GPR 80 | Sampling Input bus or local `getUserMedia`; send Sample packet | Signed mono PCM plus low-byte valid marker | Physical LINE `2.2874x`; MIC `57.5868x` (`25.1758x` relative); original ES5510 cutoff table | Instrument file after save; analog filter history is in plug-in state | The original OS owns MIC/LINE, rate, cutoff, allocation, recording and stop processing | Original-OS LINE/MIC state, circuit response, recording/playback and effect regressions automated |
| Sampling input monitor | Board Level-Detect route | Observe original level-meter mode, trigger marker and current ADC polling | Route the filtered mono Sampling Input equally to both outputs only on the `#`/Level-Detect page | Signed mono PCM | Physical VOLUME gain | None | Must remain silent on other sampling pages and during RECORD | Level-Detect, RECORD and post-stop gate transitions covered by machine regression |
| Effect Select / Bypass | Panel automation | Read effect/status display | Send effect button packet | Effect-dependent | OS-defined | Instrument/bank/effect-file dependent | ES5510 execution and routing are implemented; DADR uses the active MEMSIZ address conversion | Original-OS sample recording plus `10/11/12/13/11/10` repeated-switch bus, ESP-return and DAC-output regression passes; ROM 11/13 table-address conversion has a focused unit test |
| Four-line VFD display | Panel automation | Decode DUART channel-B VFD stream | Read-only GUI mirror | Three printed annunciator rows plus 22 characters | Raw lamp banks + character mapping | None | Unverified lamp indices remain dark; no display-text inference | Characters, decimal points, cursor and verified annunciators live-tested |

## OS editor parameters planned for the comfortable GUI

Unless a row says otherwise, the common mapping is:

- **Source:** panel automation.
- **Read:** select Mode/Page/direct number, then parse the real OS display;
  use Left/Right for additional fields.
- **Write:** select the same field and send Up/Down or emulated Data Entry.
- **Scaling:** identity in displayed OS units initially; normalized plug-in
  scaling is added only after endpoint and step-size tests.
- **Test status:** page catalog manual-verified; real OS round-trip pending.

### Instrument and system

| GUI parameter | Source | Read method | Write method | Value range | Scaling | Persistence | Known risks | Test status |
|---|---|---|---|---|---|---|---|---|
| Patch layer enable set | Panel automation | EDIT/Instrument/0 display | Cursor + Data Entry | OS-defined layer/patch set | Identity | Instrument file | Patch-select context | Manual verified; round-trip pending |
| Keydown layers | Panel automation | EDIT/Instrument/1 | Cursor + Data Entry | OS-defined layer set | Identity | Instrument file | Can silence keydown voices | Manual verified; round-trip pending |
| Keyup layers | Panel automation | EDIT/Instrument/2 | Cursor + Data Entry | OS-defined layer set | Identity | Instrument file | Release-trigger behavior | Manual verified; round-trip pending |
| MIDI out channel | Panel automation | EDIT/Instrument/3 | Data Entry | OS enumeration | Identity | Instrument file | Off/base/channel semantics | Manual verified; round-trip pending |
| MIDI out program | Panel automation | EDIT/Instrument/4 | Data Entry | OS enumeration | Identity | Instrument file | May transmit external program change | Manual verified; round-trip pending |
| Pressure mode | Panel automation | EDIT/Instrument/5 | Data Entry | OS enumeration | Enum | Instrument file | Poly/channel pressure distinction | Manual verified; round-trip pending |
| MIDI status | Panel automation | EDIT/Instrument/6 | Data Entry | OS enumeration | Enum | Instrument file | Local/MIDI routing changes | Manual verified; round-trip pending |
| Instrument size | Panel automation | EDIT/Instrument/7 | Read-only | Blocks | 1 block = 256 samples | Derived; not written | Must remain read-only | Manual verified; round-trip pending |
| Instrument name | Panel automation | EDIT/Instrument/8 | Character entry | OS name length/charset | Character mapping | Instrument file | Character encoding | Manual verified; round-trip pending |
| Patch Select assignment | Panel automation | EDIT/Instrument/9 | Cursor + Data Entry | OS enumeration | Enum | Instrument file | Four patch states | Manual verified; round-trip pending |
| Instrument key range | Panel automation | EDIT/Instrument, scroll | Cursor + Data Entry | Keyboard notes | Note-name mapping | Instrument file | Low/high ordering | Manual verified; round-trip pending |
| Instrument transpose | Panel automation | EDIT/Instrument, scroll | Data Entry | OS semitone range | Semitones | Instrument file | MIDI and audio behavior may differ | Manual verified; round-trip pending |
| System free blocks | Panel automation | EDIT/System-MIDI/0 | Read-only | Blocks | Identity | Derived | Must remain read-only | Manual verified; round-trip pending |
| Disk free blocks | Panel automation | EDIT/System-MIDI, scroll | Read-only | Blocks | Identity | Derived | Media changes invalidate value | Manual verified; round-trip pending |
| Master tune | Panel automation | EDIT/System-MIDI/1 | Data Entry | OS-defined | Display units initially | Global RAM; Save Global Parameters | Global audio impact | Manual verified; round-trip pending |
| Global bend range | Panel automation | EDIT/System-MIDI, scroll | Data Entry | OS-defined semitones | Semitones | Global RAM; optionally saved | Affects all instruments | Manual verified; round-trip pending |
| Touch response | Panel automation | EDIT/System-MIDI, scroll | Data Entry | OS enumeration | Enum | Global RAM; optionally saved | Velocity/pressure response | Manual verified; round-trip pending |
| Pedal mode | Panel automation | EDIT/System-MIDI/2 | Data Entry | Volume / Mod | Enum | Global RAM; optionally saved | Controller routing | Manual verified; round-trip pending |
| Sustain/Aux foot-switch modes | Panel automation | EDIT/System-MIDI, scroll | Data Entry | OS enumerations | Enum | Global RAM; optionally saved | Patch-select/start-stop side effects | Manual verified; round-trip pending |
| Auto-loop finding | Panel automation | EDIT/System-MIDI/3 | Data Entry | Off / On | Boolean | Global RAM; optionally saved | Sampling behavior | Manual verified; round-trip pending |
| FX send Bus2/Bus3 | Panel automation | EDIT/System-MIDI/4 | Cursor + Data Entry | OS-defined | Display units | Global RAM; optionally saved | DSP/routing dependency | Manual verified; round-trip pending |
| MIDI base channel and routing controls | Panel automation | EDIT/System-MIDI/5-9 + scroll | Cursor + Data Entry | OS MIDI enumerations | Enum/channel identity | Global RAM; optionally saved | External MIDI side effects | Manual verified; round-trip pending |

### Voice, wavesample, and layer

| GUI parameter | Source | Read method | Write method | Value range | Scaling | Persistence | Known risks | Test status |
|---|---|---|---|---|---|---|---|---|
| Root key / fine tune | Panel automation | EDIT/Pitch/1, fields | Cursor + Data Entry | Note + OS fine units | Note/OS units | Instrument file | Selected wave/layer scope | Manual verified; round-trip pending |
| LFO pitch amount | Panel automation | EDIT/Pitch/2 | Data Entry | OS-defined bipolar | Display units | Instrument file | Modulation polarity | Manual verified; round-trip pending |
| ENV1 pitch amount | Panel automation | EDIT/Pitch/3 | Data Entry | OS-defined bipolar | Display units | Instrument file | Modulation polarity | Manual verified; round-trip pending |
| Random frequency / amount | Panel automation | EDIT/Pitch/5, fields | Cursor + Data Entry | Frequency `0..99`; amount `-99..+99` | Quantized rate code + signed amount | Instrument file | Deterministic global sequence; held independently per voice | Original-OS algorithm, timing, seeds and per-voice application verified |
| Pitch-bend range | Panel automation | EDIT/Pitch/6 | Data Entry | OS-defined semitones | Semitones | Instrument file | Per-wave versus global bend | Manual verified; round-trip pending |
| Pitch mod source / amount | Panel automation | EDIT/Pitch/7, fields | Cursor + Data Entry | 15 modulators + bipolar amount | Enum/display units | Instrument file | Source enumeration | Manual verified; round-trip pending |
| Wavesample key range low/high | Panel automation | EDIT/Pitch/8, fields | Cursor + Data Entry | Keyboard notes | Note-name mapping | Instrument file | Low/high ordering | Manual verified; round-trip pending |
| ENV1/2/3 hard velocity levels 1-5 | Panel automation | EDIT/Env1-3/1, five fields | Cursor + Data Entry | 0-99 | Linear display units initially | Instrument file | Correct envelope/wave selection | Manual verified; round-trip pending |
| ENV1/2/3 soft velocity levels 1-5 | Panel automation | EDIT/Env1-3/2, five fields | Cursor + Data Entry | 0-99 | Linear display units initially | Instrument file | Velocity interpolation | Manual verified; round-trip pending |
| ENV1/2/3 times 1-5 | Panel automation | EDIT/Env1-3/3, five fields | Cursor + Data Entry | 0-99 | Nonlinear OS time table | Instrument file | Must not map linearly to seconds | Manual verified; round-trip pending |
| ENV1/2/3 second release time/level | Panel automation | EDIT/Env1-3/4, two fields | Cursor + Data Entry | OS-defined | Time table + level units | Instrument file | Two-stage release semantics | Manual verified; round-trip pending |
| ENV1/2/3 attack-time velocity | Panel automation | EDIT/Env1-3/5 | Data Entry | 0-99 | OS-defined response | Instrument file | Velocity-dependent timing | Manual verified; round-trip pending |
| ENV1/2/3 keyboard time scaling | Panel automation | EDIT/Env1-3/6 | Data Entry | OS-defined | OS-defined response | Instrument file | Root-key dependence | Manual verified; round-trip pending |
| ENV1/2/3 soft velocity curve | Panel automation | EDIT/Env1-3/7 | Data Entry | Off, Vel1, Vel2, Vel3 | Enum | Instrument file | Changes all soft/hard interpolation | Manual verified; round-trip pending |
| ENV1/2/3 mode | Panel automation | EDIT/Env1-3/8 | Data Entry | Normal, Cycle, Repeat | Enum | Instrument file | Repeat may sustain indefinitely | Manual verified; round-trip pending |
| ENV1/2/3 template | Panel automation | EDIT/Env1-3/0 | Data Entry | Current, Saved, 14 templates | Enum | Instrument file after save | Selecting/editing can replace current values | Manual verified; round-trip pending |
| LFO wave / speed | Panel automation | EDIT/LFO/1, fields | Cursor + Data Entry | 7 waves + OS speed range | Enum + OS rate units | Instrument file | Rate conversion unknown | Manual verified; round-trip pending |
| LFO depth / delay | Panel automation | EDIT/LFO/2, fields | Cursor + Data Entry | OS-defined | Display/time units | Instrument file | Delay time nonlinear | Manual verified; round-trip pending |
| LFO mode | Panel automation | EDIT/LFO/3 | Data Entry | OS enumeration | Enum | Instrument file | Key-sync/free-run semantics | Manual verified; round-trip pending |
| LFO depth-mod source/amount | Panel automation | EDIT/LFO/4, fields | Cursor + Data Entry | Modulator + bipolar amount | Enum/display units | Instrument file | Source enumeration | Manual verified; round-trip pending |
| LFO rate-mod source/amount | Panel automation | EDIT/LFO/5, fields | Cursor + Data Entry | Modulator + bipolar amount | Enum/display units | Instrument file | Source enumeration | Manual verified; round-trip pending |
| Filter mode | Panel automation | EDIT/Filter/0 | Data Entry | 4 OS modes | Enum | Instrument file | Must match ES5505 control topology | Manual verified; round-trip pending |
| F1/F2 cutoff | Panel automation | EDIT/Filter/1, fields | Cursor + Data Entry | OS-defined | OS cutoff table; not Hz yet | Instrument file | Direct K1/K2 register writes are prohibited | Manual verified; round-trip pending |
| F1/F2 ENV2 amount | Panel automation | EDIT/Filter/2, fields | Cursor + Data Entry | OS-defined bipolar | Display units | Instrument file | Dynamic per-voice result | Manual verified; round-trip pending |
| F1/F2 keyboard amount | Panel automation | EDIT/Filter/3, fields | Cursor + Data Entry | OS-defined bipolar | Display units | Instrument file | Key tracking | Manual verified; round-trip pending |
| F1/F2 mod source/amount | Panel automation | EDIT/Filter/7-8, fields | Cursor + Data Entry | Modulator + bipolar amount | Enum/display units | Instrument file | Source enumeration | Manual verified; round-trip pending |
| Wavesample volume / pan | Panel automation | EDIT/Amp/1-2, fields | Cursor + Data Entry | OS-defined | OS volume/pan law | Instrument file | ES5505 exponential volume is downstream | Manual verified; round-trip pending |
| Volume/Pan mod source/amount | Panel automation | EDIT/Amp/7-8, fields | Cursor + Data Entry | Modulator + bipolar amount | Enum/display units | Instrument file | Dynamic downstream values | Manual verified; round-trip pending |
| A-B fade-in / C-D fade-out / curve | Panel automation | EDIT/Amp/3-5 | Cursor + Data Entry | OS-defined | OS crossfade law | Instrument file | Key/velocity crossfade context | Manual verified; round-trip pending |
| Boost | Panel automation | EDIT/Amp/6 | Data Entry | Off / On (+12 dB) | Boolean | Instrument file | Clipping/headroom | Manual verified; round-trip pending |
| Output bus | Panel automation | EDIT/Amp/9 | Data Entry | OS output enumeration | Enum | Instrument file | Requires complete DSP/output routing | Manual verified; round-trip pending |
| Playback/loop mode | Panel automation | EDIT/Wave/0 | Data Entry | OS mode enumeration | Enum | Instrument file | Loop/reverse/transwave semantics | Manual verified; round-trip pending |
| Sample start/end | Panel automation | EDIT/Wave/1-2 | Data Entry | 0..sample length | Sample index | Instrument file | Must preserve start <= end | Manual verified; round-trip pending |
| Loop start/end/position | Panel automation | EDIT/Wave/3-5 | Data Entry | Within sample bounds | Sample index / OS position units | Instrument file | Bounds and mode dependency | Manual verified; round-trip pending |
| Wave modulation type/source/amount/range | Panel automation | EDIT/Wave/6-9, fields | Cursor + Data Entry | OS enumerations/ranges | Enum/display units | Instrument file | Transwave-specific behavior | Manual verified; round-trip pending |
| Layer glide mode/time and legato | Panel automation | EDIT/Layer/0-2 | Data Entry | OS enumerations/range | Enum/time units | Instrument file | Voice allocation behavior | Manual verified; round-trip pending |
| Layer velocity low/high | Panel automation | EDIT/Layer/3, fields | Cursor + Data Entry | MIDI velocity domain | Identity | Instrument file | Low/high ordering | Manual verified; round-trip pending |
| Pitch table | Panel automation | EDIT/Layer/4 | Data Entry | Table enumeration | Enum | Instrument file | Custom tuning dependency | Manual verified; round-trip pending |
| Layer name | Panel automation | EDIT/Layer/5 | Character entry | OS name length/charset | Character mapping | Instrument file | Character encoding | Manual verified; round-trip pending |
| Layer delay / velocity amount | Panel automation | EDIT/Layer/6, fields | Cursor + Data Entry | OS-defined | Time/display units | Instrument file | Trigger timing | Manual verified; round-trip pending |
| Layer restrike | Panel automation | EDIT/Layer/7 | Data Entry | OS enumeration | Enum | Instrument file | Voice retrigger behavior | Manual verified; round-trip pending |

### Sequencer and effects

| GUI parameter | Source | Read method | Write method | Value range | Scaling | Persistence | Known risks | Test status |
|---|---|---|---|---|---|---|---|---|
| Current sequence/song and Goto | Panel automation | EDIT/Seq-Song/0 | Cursor + Data Entry | Existing sequence/song positions | Enum/position | Song/bank | Changes editing context | Manual verified; round-trip pending |
| Tempo / loop | Panel automation | EDIT/Seq-Song/1, fields | Cursor + Data Entry | OS-defined BPM / loop enum | BPM + enum | Sequence/song | Playback timing | Manual verified; round-trip pending |
| Clock source | Panel automation | EDIT/Seq-Song/2 | Data Entry | OS enumeration | Enum | Sequence/song/global context | External sync dependency | Manual verified; round-trip pending |
| Click, volume, pan, output | Panel automation | EDIT/Seq-Song/3-5 | Cursor + Data Entry | OS-defined | OS units | Sequence/song | Audio routing dependency | Manual verified; round-trip pending |
| Countoff / record mode / record source | Panel automation | EDIT/Seq-Song/6-8 | Data Entry | OS enumerations | Enum | Sequence/song | Can alter recording behavior | Manual verified; round-trip pending |
| Track status | Panel automation | EDIT/Track/0 | Data Entry | Mute / Play / Solo | Enum | Sequence/song | Audible state | Manual verified; round-trip pending |
| Track mix / pan / output | Panel automation | EDIT/Track/1-2 | Cursor + Data Entry | OS-defined | OS volume/pan/output law | Sequence/song | Routing and headroom | Manual verified; round-trip pending |
| Track effect control | Panel automation | EDIT/Track/3 | Data Entry | OS-defined | Enum/display units | Sequence/song | DSP dependency | Manual verified; round-trip pending |
| Multi-In MIDI channel | Panel automation | EDIT/Track/4 | Data Entry | MIDI channel enumeration | Identity | Sequence/song | External MIDI routing | Manual verified; round-trip pending |
| Effect algorithm parameters | Panel automation | Effects page; dynamic fields | Cursor + Data Entry | Algorithm-dependent | Algorithm-specific | Instrument/bank/effect file | Cannot use one fixed parameter schema | Manual verified at algorithm level; round-trip pending |

## Low-level hardware observability (not direct GUI ownership)

| Internal value | Source | Read method | Write method | Value range | Scaling | Persistence | Known risks | Test status |
|---|---|---|---|---|---|---|---|---|
| ES5505 voice control | Hardware register `0x200000`, paged reg 0 | Emulator register read/trace | OS writes only | 16-bit bitfield | Bitfield | Reconstructed by OS | Direct GUI write can desynchronize OS | Core read/write tested; not exposed |
| ES5505 frequency | Hardware register, paged reg 1 | Emulator trace | OS writes only | 15 effective bits, stored shifted | Phase increment | Runtime voice | Not equivalent to edited pitch parameter | Core tested; not exposed |
| ES5505 start/end/accumulator | Hardware registers, paged regs 2-5/10-11 | Emulator trace | OS writes only | 31-bit normalized address | 20 integer + internal fraction | Runtime voice | Transient; unsafe as editor state | Core tested; not exposed |
| ES5505 K1/K2 | Hardware registers, paged regs 7/6 | Emulator trace | OS writes only | `0x0000..0xfff0`, step `0x10` | Chip coefficient, not Hz | Runtime voice | Modulated per voice; not stored cutoff | Core topology tested; not exposed |
| ES5505 left/right volume | Hardware registers, paged regs 8/9 | Emulator trace | OS writes only | `0x00..0xff` | 4-bit exponent + 4-bit mantissa | Runtime voice | Exponential, dynamic, downstream value | Volume law tested; not exposed |
| F1 cutoff (wavesample) | Relocatable sample-RAM object | Slot table + instrument wavesample table + fixed object offset | Diagnostic direct RAM write only | `0..255` | Stored as big-endian `value << 8` | Instrument file; save/reload not tested | Address changes whenever objects relocate; production write path and live-audio response remain unverified | Direct read/write and original-OS display verified for two instrument slots, Layer 1 / WS 1 |
| Other OS RAM parameter structures | RAM address | Targeted before/after snapshots | No GUI writes permitted yet | Unknown | Unknown | OS session / file-dependent | Layout, relocation, selection context unknown | Discovery not started |
| Enhanced resonance/features | Enhanced engine | N/A | N/A | N/A | N/A | N/A | Outside current scope | Not exposed / deferred |

## Original-OS random pitch modulation

The `RANDM` pitch source is generated entirely by the original EPS-16 Plus OS,
not by the ES5505 and not by emulator-added noise. Reverse engineering against
the running original OS found two global 16-bit state words at `$0001EC` and
`$0001EE`. Their boot values are `$06E1` and `$000A`; the same four bytes occur
at offset `$1FEC` in `EPS130OS.img`.

Whenever a voice needs a new random value, the OS executes this exact
big-endian, 16-bit wrapping recurrence:

```text
B = (B + A) & $FFFF
A = (A + B) & $FFFF       ; this uses the newly updated B
voice.random = signed16(A)
```

The recurrence is the code at ROM `$C0C100..$C0C110`. For the OS seed it
returns to the complete `(A,B)` state after 49,152 generated values. Its signed
output is bipolar with an exactly zero mean across that period. This is a
deterministic additive generator, so loading the same initial state reproduces
the sequence exactly.

There is one shared generator, but each active voice has its own held value
and countdown in its runtime voice structure. A refresh consumes the next
value from the shared sequence. Consequently simultaneous notes do not receive
the same modulation, and voice allocation and refresh order influence which
random value each note gets.

The original OS services the countdown every 1 ms. The wavesample stores an
internal 7-bit rate code `r`; the hold time is:

```text
r = 1..127: 128 - r milliseconds
r = 0:      65,536 milliseconds
```

The displayed `RANDOM FREQ=0..99` is a quantized editor representation of that
internal code and is not a millisecond value. The display can map more than
one internal representation onto the same number, so code derived from the
live wavesample object is authoritative.

For pitch, the OS treats `voice.random` as signed, multiplies it by the signed
wavesample random-amount coefficient, shifts the product, and adds it to the
other pitch modulators before converting the combined pitch to the ES5505
frequency register. The held random value itself is stepped: no smoothing or
interpolation is added. The global stream, per-voice sample-and-hold timing,
bipolar signed result and interaction with voice allocation explain much of
the source's musically useful, organic behavior.

## Required promotion test for a direct RAM mapping

A parameter may move from panel automation to direct RAM access only after all
of the following pass:

1. Capture before/after RAM snapshots while changing only that parameter.
2. Repeat with at least two instruments, two layers/wavesamples where relevant,
   and two non-adjacent values.
3. Identify selection/context pointers and prove the address is not incidental.
4. Read the value without changing OS behavior.
5. Write a value, confirm it in the original OS display and audio/register
   output, then restore the original value.
6. Save and reload the owning file and confirm persistence.
7. Add an automated regression test before the GUI uses the RAM path.

## Confirmed diagnostic mapping: Filter 1 cutoff

The original EPS-16 Plus OS does not keep one fixed cutoff address per
instrument slot. Instrument and wavesample objects are relocatable. The
diagnostic probe therefore resolves the live object graph on every run:

1. Read the selected instrument object's absolute base address from the
   eight-entry OS slot table. Slot 1 is at `$FFDC9C`, Slot 2 at `$FFDC98`,
   continuing downward in four-byte steps through Slot 8 at `$FFDC80`.
2. In the instrument block, resolve the packed relative pointer for the
   required wavesample from the table beginning at block word 61. Each of the
   128 table entries occupies two words. The allocator header preceding the
   instrument block is five words (10 bytes).
3. Add the unpacked relative offset to the instrument object base.
4. Filter 1 cutoff is the 16-bit word at wavesample object offset `$BC`.
   The stored representation is big-endian `cutoff << 8`.

The August 11, 2026 probe loaded the original ED-001 bank and independently
measured two unrelated objects:

| Slot / instrument | Instrument base | WS 1 relative offset | WS 1 base | F1 cutoff address | Measured OS edits |
|---|---:|---:|---:|---:|---|
| 1 / FLUTE 1 | `$7D9000` | `$009580` | `$7E2580` | `$7E263C` | `$7F00 -> $8000 -> $8100 -> $7F00` |
| 2 / PIANO 241 | `$7BAE00` | `$000370` | `$7BB170` | `$7BB22C` | `$4600 -> $4700 -> $4800 -> $4600` |

The same diagnostic then wrote the two addresses directly, without panel,
MIDI, or SysEx input. Both values coexisted and were read back by the original
OS display: Slot 1 showed `F1=100`; Slot 2 showed `F1=90`. This proves
independent direct addressing of two loaded instrument slots.

Cutoff is owned by a wavesample, not by the layer record itself. A layer's
effective cutoff therefore depends on which wavesample(s) it selects. Layer
membership/ranges, live voice response, dirty-file state, save/reload behavior,
and safe audio-thread scheduling are deliberately still unverified. The
diagnostic write API must not be exposed as a production GUI control until
those remaining promotion tests pass.
