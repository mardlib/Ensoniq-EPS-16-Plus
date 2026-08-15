# Bring-up notes

## Inputs (not stored in the project)

| Input | SHA-256 |
|---|---|
| EPS130OS.hfe | `218e618c67985540043ae6743f9a304c7a8d9441637bef362bc289dc8a064e03` |
| eps16plus-100f-upper.u28 | `91ac82ef131008bd714c3d831d22a374fce33695e899bcee1ded1410379394d0` |
| eps16plus-100f-lower.u27 | `1906e9929fe310bc08eb430ed5e83097542cabc8fd6cb57bd4ac7659847d1674` |

The combined 128 KiB ROM has SHA-256
`6b08d3c0350b48ca40bdcffad77258e92dcca5802c3d7230ad4577485e875615`.

## Confirmed boot facts

- U28 supplies the high byte and U27 supplies the low byte of the 16-bit ROM.
- The ROM maps at `0xC00000` through `0xC1FFFF`.
- Initial supervisor stack pointer: `0x00001600`.
- Reset program counter: `0x00C11604`.
- Reset begins with `MOVE #$2700,SR`, disabling interrupts during early setup.
- Firmware copies code into low RAM and executes it; a probe reaches low-RAM
  program counters without patching the ROM.
- Confirmed base map: OTIS/ES5505 at `0x200000`, HD63450-compatible DMA at
  `0x240000`, DUART at `0x280000`, WD1772 at `0x2C0000`, sample RAM at
  `0x580000-0x7FFFFF`, ROM at `0xC00000-0xC1FFFF`, and OS RAM at
  `0xFF0000-0xFFFFFF`.
- With a protocol-compatible minimal DUART/panel stub, the unmodified firmware
  emits `ENSONIQ EPS-16 PLUS` followed by `PLEASE INSERT DISK`.
- The bootloader then polls the WD1772 and reports `BAD DISK/NOT EPS DISK`, as
  expected while no floppy image is connected to the controller.
- DUART IP0 disk-ready and OP1 floppy-side signals are active-low at the
  firmware/hardware boundary. With those polarities modeled, the boot ROM
  recognizes the supplied disk and displays `LOADING SYSTEM`.
- The WD1772 probe implements sector reads plus RESTORE, SEEK, STEP, STEP-IN
  and STEP-OUT. The original loader reads 124 sectors across multiple tracks
  and hands control to the loaded OS without patching ROM or disk data.
- The loaded scheduler reaches its normal `STOP #$2000` idle loop. A minimal
  MC68681 timer source now supplies masked, vectored level-3 interrupts using
  the programmed IVR. A 500-million-cycle run processes roughly 9,300 timer
  ticks without an OS error or reboot request.
- The EPS front panel uses DUART channel B at 62,500 baud, 8N2. It replies once
  per motherboard byte: normally with an echo, while `0xE7` and `0x71` receive
  a zero reply. The probe implements this handshake and decodes the 1x22 VFD
  stream; the stable boot display is `    LOADING SYSTEM    `.
- Panel receive data now uses a ring queue and the MC68681 channel-B receive
  interrupt. Scripted two-byte button/key packets are consumed by the original
  OS, proving that a modern GUI can inject controls without hardware-layout
  emulation.
- Browser and raw-probe clicks now hold ordinary panel buttons for 50 ms of
  emulated CPU time before release. The former generic 1 ms pulse was not a
  realistic hardware click and could disappear in context-dependent OS
  debounce paths; cursor timing remains separately verified.
- A minimal paged ES5505 register model supplies correct reset/readback values:
  stopped voice control bits, active-voice count, page and idle IRQ vector.
- A minimal ES5510 host model implements its 24-bit GPR latches, 48-bit
  instruction latches, program upload/readback and 20-bit external DRAM path.
  During boot the OS uploads 398 GPR values and 322 DSP instructions.
- Musashi instruction profiling after the final floppy read proves execution
  in both ROM and loaded OS RAM. In a 100-million-cycle run, 59,478
  instructions execute from `0xFF0000-0xFFFFFF`; the hottest loaded routines
  perform voice construction and ES5505 parameter updates.
- `LOADING SYSTEM` is status code `0xFF` in the boot ROM's message table. It is
  simply the last VFD message written; it is not evidence that loading stopped.
- The ES5505 is now a separate render module connected to the live OS bus. It
  implements 32 paged voices, the verified 20-integer/9-fraction address
  format, internal 11-bit interpolation phase, PCM interpolation, the 4+4-bit
  exponent/mantissa volume law, forward/reverse/bidirectional loops and the
  original four-pole K1/K2 filter topology.
- A boot-only run renders 127,808 frames with a zero peak. This is expected and
  desirable: the OS disk loads no instrument waveform into sample RAM, so the
  emulator does not synthesize placeholder audio.
- A minimal MC68450-compatible register and transfer model now preserves all
  four DMA channels, memory/device addresses, transfer counts, address modes,
  completion state and level-2 interrupt vectors. Channel 0 is connected to
  the WD1772 data register. The path remains idle during OS-only boot, as
  expected, and is ready for the first instrument read.
- Runtime disk swapping and timed panel scripts allow the original OS to stay
  active while an instrument image replaces the boot disk. The documented
  hardware workflow is: `LOAD`, `INSTRUMENT`, select a file with `UP/DOWN`,
  press `ENTER/YES`, then select one of eight destination tracks. Selecting the
  track starts loading immediately; no second Enter press is involved.
- A real-hardware observation confirms the common shortcut after boot and disk
  insertion: the Instrument page is already retained, so pressing `LOAD` once
  immediately exposes the current instrument file. `ENTER/YES` then displays
  `PICK INSTRUMENT BUTTON`; pressing the destination track starts loading.
- `panel/index.html` provides an original-layout diagnostic panel and event
  recorder. It distinguishes verified raw panel codes from mappings that still
  need confirmation before they become part of the GUI adapter. Its LOAD page
  models the real 22-character file display, direct file numbers, Up/Down file
  browsing, Left/Right name/block-size switching, `PICK INSTRUMENT BUTTON`,
  `LOADING FILE...`, and `FILE LOADED` states using the supplied manual.
- Placeholder editor menus have been removed. `panel/menu-catalog.js` now
  transcribes every documented populated EDIT and COMMAND page, including
  direct parameter numbers where the manual specifies them. Each of ENV1,
  ENV2 and ENV3 contains nine display pages (`1-8` and `0`), not a generic ADSR
  approximation. Effect parameters remain type-dependent as on the hardware.
- The service manual's Display Self-Test table exposes the complete 36-button
  matrix. `docs/panel-protocol.md` records every raw code and the KPC framing:
  press is `code|0x80,00`, release is `code,00`. The browser panel now has no
  unknown buttons and logs the complete four-byte click packet.
- VOLUME uses the ES5505 analog path. DATA ENTRY is analog input channel 3.
  The EPS board does not select it from the DUART's low three output bits;
  the original ROM polls it during the automatically-driven scanner phase
  `OPR & 0x70 = 0x10`. `OP7` independently controls the sampling board's
  LINE/MIC path, so both `0x9x` (LINE) and `0x1x` (MIC) select DATA ENTRY.
  Including `OP7` in the phase decode made every MIC scanner read fall through
  to the reference channel, which corrupted the OS-created voice volume and
  pitch after recording. Masking only `OP4-OP6` preserves the scanner while
  leaving LINE/MIC entirely under original-OS control. The ROM scales it to
  the service manual's 0..255 "MR. KNOB"
  range. Treating the low bits as a mux selected channel 7/reference after
  boot, which explained the false layer errors during fast movement. The
  ROM's calibrated electrical span is raw ADC 28..687 (`0x0700` is zero).
  The GUI maps to 0..715, providing 28 counts of symmetric analog overtravel
  so the ROM's smoothing/hysteresis crosses both endpoints reliably, while
  avoiding the large upper dead zone caused by raw 0..1023. The browser
  serializes ADC requests and
  coalesces pending motion events to the newest physical position; an earlier
  artificial ramp could lag behind rapid direction changes. The OS remains
  responsible for contextual scaling. The ES5505 core now
  implements the parallel ADC read path and has a regression test for it.
- A byte/PC/cycle UART trace can be enabled with `EPS16_TRACE_PANEL=1`. It
  confirms that injected panel packets are consumed by the loaded OS handler.
  Illegal-instruction tracing also identified and eliminated a false DUART
  channel-A transmit interrupt caused by reporting TX-ready while disabled.
- Live display draining follows complete `f`-delimited KPC frames rather than
  assuming that a 22-cell VFD frame contains only 22 transport bytes. The
  original OS also uses `63 ... 72` to overwrite the active cursor field
  without sending a new frame; decoding that update fixes stale parameter
  displays and the stray trailing `F`, `G`, or `#` characters.
- The user-supplied 32 KiB KPC 2.33 image is structurally valid 68HC11 code
  with reset vector `e005`. Its parser confirms an `e7 -> c8` early-loader
  handshake and `e7 -> c7/ff` normal-state responses. The SHA-256 and detailed
  findings are in `docs/kpc-reference.md`; the copyrighted image is external.
- The provisional KPC behavior has been separated from `rom_probe.c` into
  `native/kpc_legacy.c`. `native/kpc_firmware.c` optionally loads the external
  32 KiB EPROM with `EPS16_KPC_ROM`, validates the blank prefix, programmed
  `e000-ffff` window, and reset vector, but does not yet execute it. Startup
  reports `execution=legacy-model` until a 68HC11 CPU/peripheral core replaces
  the provisional model. With the split in place, the unchanged legacy path
  still reaches `NO INSTRUMENTS` with zero illegal 68000 instructions.
- The KPC's asynchronous `ff` poll token is now modeled. It drains the loaded
  OS display queue and exposes the genuine `TUNING KBD - HANDS OFF` and
  `KEYBOARD TUNED` sequence. Three `fd -> c9` busy replies followed by
  `fd -> ff` reproduce the calibration transition without copying KPC code.
- The service-manual self-test characters were proven not to be UART matrix
  indices. The OS accepts `00..25` and maps them through ROM table `c02252`;
  the GUI mapping is therefore marked provisional until end-to-end validation.
  Disassembly of the loaded UART-B handler at `ffffa2ce` confirms this lookup
  directly (`D2 = table[raw_index]`). End-to-end tests distinguish the two
  previously confused controls: raw `1b` is SYSTEM-MIDI and reaches the disk
  directory path (`NO DIRECTORIES`) in the initial LOAD context; raw `20` is
  SAMPLE and reaches `PICK SAMPLE INSTRUMENT` directly from `NO INSTRUMENTS`.
  End-to-end
  menu tests now verify the
  three mode buttons, four principal pages, and all ten numbered pages. The
  structurally contiguous Track 2-8 family and CANCEL are enabled provisionally
  from the same original-OS table. Raw `07` is
  live-confirmed as EFFECT SELECT because it
  opens the effect-selection menu. A subsequent `COULD NOT BE DOWNLOADED`
  response belongs to the downstream ES5510/effect-download path. The
  service-manual self-test glyphs plus the ROM matrix table identify RECORD,
  STOP/CONTINUE, and PLAY as raw `03`, `17`, and `1d`; an original-OS test
  records `SEQUENCE 01` with the overlapping RECORD+PLAY edges and stops it
  with raw `17`.
- ENTER/YES is now end-to-end verified as matrix index `23`. Press `a3 00`
  advances the original OS from `FILE 4  JAZZ BASS` to
  `PICK INSTRUMENT BUTTON`.
- Instrument/Track 1 is now end-to-end verified as matrix index `02`. Its
  press `82 00` leaves the destination prompt unchanged; release `02 00`
  starts the load, matching original-hardware behavior. The OS displays
  `LOADING JAZZ BASS` and reaches `FILE LOADED` without illegal instructions.
- The original-OS translation table identifies the complete Track family as
  raw `02,08,0e,14,04,22,1c,16` for Tracks 1-8. The live KPC transport applies
  the same press duration, display drain, release framing, and load-readiness
  handling to every Track button rather than special-casing Track 1.
- Runtime disk change is a one-shot active-low DUART IP1 pulse. Holding IP1
  low lets the OS read the new directory but later invalidates its disk state;
  leaving it high prevents the runtime swap from being noticed.
- Loading ED-001 `JAZZ BASS` reads 154 additional sectors and transfers exactly
  78,848 additional bytes through the MC68450 path. At equal run length, the
  OS-only sample RAM remains at 1,787 nonzero bytes with FNV-1a `f955d1cd`;
  after loading it contains 75,614 nonzero bytes with FNV-1a `46c96f26`.
- After `FILE LOADED`, clicking Instrument/Track 1 again exits the LOAD browser
  and selects the performance instrument; the genuine display becomes
  `JAZZ BASS    VOLUME=99`. A KPC keyboard press `98 64` (key index `18`,
  Middle C, velocity 100) then starts ES5505 voice 18. Release is `18 01`.
- EPS-16 sample memory is wired as a 512 KiB BS=0 region followed by a 2 MiB
  BS=1 region. The previous `bank * 0x200000` reader missed the waveform;
  BS=1 must start at CPU sample-RAM offset `0x80000`. With that correction,
  voice 18 reads the loaded JAZZ BASS waveform and the rendered 20-bit peak is
  71,305 instead of zero.
- The probe can write the live ES5505 mix as 16-bit stereo WAV using
  `EPS16_AUDIO_WAV`, optionally bounded by `EPS16_AUDIO_START_CYCLE` and
  `EPS16_AUDIO_END_CYCLE`. Audio rendering is scheduled from elapsed 68000
  cycles instead of emitting a fixed block per CPU slice. Both the EPS-16 Plus
  CPU and ES5505 run at 10 MHz; with 21 active voice slots the output rate is
  29,761 Hz. The corrected one-second Middle-C note produces a 4.004-second
  WAV with 32,260 active frames, a 20-bit mix peak of 112,787, and SHA-256
  `f1d6942aa48f579fcec22ebeee42f89562c9006fc5aeff294e1dddef28cefa7d`.
- JAZZ BASS intentionally applies a downward pitch envelope: while Middle C
  is held, the original OS writes decreasing values to the live ES5505
  frequency register. The earlier fixed-64-frame scheduler and incorrect
  90,702 Hz WAV rate exaggerated that envelope; neither value belongs to the
  EPS-16 Plus hardware.

The probe currently returns neutral values for unknown MMIO. It is expected to
take incorrect diagnostic branches until each peripheral's reset state is
implemented. An apparent access is therefore evidence of an address region,
not yet proof of a register's semantics.

## Confirmed disk facts

- OS disk version: 1.30.
- Minimum boot ROM version recorded by the disk: 1.00.
- Main file `EPS-16+ O.S.`: type `0x1B`, 167 blocks, blocks 15-181.
- Effect file `PARALLEL EFX`: type `0x18`, 10 blocks, blocks 182-191.
- Extracted OS file SHA-256:
  `7729b8cc8f0ef8fe563102844b5a480234febb74a67db4c406b4ee08766251b5`.

## Next technical gate

The ROM-to-OS boot path, display queue, and keyboard calibration transition are
now proven. The WD1772-to-MC68450 path now models active-low PCL0, falling-edge
status interrupts and DMA completion. The original OS reads its runtime
directory through DMA and displays `NO INSTRUMENTS` for the OS disk.

A runtime swap to the external `ED-001.IMG` test disk is also proven. The
unmodified OS reads its directory and reaches `FILE 4  JAZZ BASS`; a
350-million-cycle run completes without fatal or illegal instructions. An
earlier `ERROR 145` came from prematurely clearing a level-sensitive DUART
timer interrupt. `ERROR 032` came from incomplete ES5510 special-register
reads during the firmware's effect-program verification. Implementing DIL and
the constant/control registers (`-1`, `MIN`, `MAX`, `ZERO`) removes both
failures. All native and Python regression tests pass at this checkpoint.

Live ENTER and Track-1 navigation, the full ED-001 `JAZZ BASS` disk transfer,
sample-RAM population, virtual Middle-C key event, ES5505 voice construction,
and nonzero stereo PCM output are now proven. The next gate is a real-time host
keyboard/MIDI bridge and continuous audio callback instead of the probe's
deterministic WAV capture. The remaining provisional panel controls should
still be promoted only through independent state-transition tests.

The EPS-16 Plus OS writes ES5510 Host Serial Control `0x48`: Sony serial
format, with SER1 configured as the output and SER0, SER2, and SER3 configured
as inputs. Its effect programs agree with that hardware configuration: they
read the three input ports and write their result to SER1L/SER1R. ES5505 route
codes `0`, `1`, and `3` (Bus1..3) feed SER0/SER2/SER3, the OS-selected SER1
output feeds the main DAC, and route code `2` (Aux1) remains separate.
A deterministic original-OS instrument run now measures ES5505 Bus1 peak
`44697` and nonzero ESP output peak `3527` (previously exactly zero), with no
illegal 68000 instructions.  This verifies the digital route, but audible
effect character still requires a live user check.

The sampling overlay polls ES5510 GPR `0x80` as a 16-bit input sample followed
by a low-byte valid marker. The ADC enters ES5510 serial input 0. The original
sampling overlay loads its independently selectable cutoff coefficients into
GPR `3b..69`, executes the uploaded program once per conversion and leaves the
filtered 24-bit result in GPR `80`; the host-visible low byte remains the
conversion-ready marker. The normal DAC-rate ESP pass is suspended while that
overlay is driven by ADC conversions, because injecting additional zero serial
samples changes the filter response. Allocation and sample-RAM writes remain
original-OS work.

Before that digital program, the input follows analog schematic sheet 3. The
TL072/CD4053 feedback network gives LINE a DC gain of approximately `2.2874`
and MIC approximately `57.5868`, a relative MIC sensitivity of `25.1758x`.
The plug-in boundary retains the physical LINE gain rather than normalizing it
away: the original ES5510 sampling program has its own passband scaling, and
normalizing before it discarded the circuit's approximately 7.2 dB LINE gain.
`C128=100pF`
in parallel with `R137=220k` supplies the high-frequency shelf. The following
two buffered filter sections are solved as complete loaded third-order
networks using `R=1.78k`, `C0=4700pF`, `C2=470pF`, and two parallel `8200pF`
feedback capacitors per section. They must not be split into an independent RC
plus Sallen-Key approximation. The circuit runs at 2x/4x DAW rate where
required and is part of the checksummed machine snapshot. Unmeasured noise and
guessed op-amp coloration are deliberately omitted.

The sampling overlay communicates that selection through ES5510 GPR `0x81`,
not through the browser. Selectors `8..2` choose conversion dividers
`56,49,42,35,28,21,14` from the 625 kHz board clock (the seven approximately
11.2 through 44.6 kHz hardware rates). The input bridge resamples native host
PCM to that selected rate. Live mode currently gates the GPR `0x80` valid byte
from the host monotonic clock; deterministic offline runs use emulated cycles.
Moving the live path directly to bus-cycle gating is not valid yet: it stalls
the original-OS record transition after 512 samples. Live DAC output paces the
CPU in buffered bursts of about 34 ms. ADC conversions accumulated during such
a normal burst must remain available to the continuously polling OS; only a
real gap in OS polling resets the converter to its newest value.

The record loop at `ffe506` selects GPR `0x80` before every conversion, polls
the selected latch's valid byte, and reads the sample with `MOVEP.W`. Therefore
the host must refresh the ADC latch on the GPR-select operation only. Refreshing
it again on the subsequent low-byte read consumes two input conversions for one
RAM word, producing skipped samples, crackle and shortened playback. The live
oscillator keeps an absolute wall-clock phase across short scheduler delays but
drops stale backlog only after more than 250 ms without an OS ADC poll. The
previous test against 10 ms of oscillator lag misclassified every normal DAC
buffer pause and reduced the observed selector-2 conversion rate to about
17.4 kHz instead of 44.6 kHz. A 430.66 Hz deterministic
input at selector `2` measures 103.66 stored samples per period (expected
approximately 103.66), with ordered signed 16-bit words in sample RAM.

The live panel reports microphone peak at both ends as `MIC B` (browser Web
Audio) and `EPS` (PCM packets accepted by the host ADC bridge), together with
backend rate/buffer counters in `/api/state`. The MediaStream source node is
retained for the full enabled session and its AudioContext is explicitly
resumed, preventing an apparently active microphone whose processing graph has
been suspended or garbage-collected.
The browser exposes an optional `1x..32x` host-input preamp before PCM
submission, but defaults to unity (`1x`). `MIC B` remains pre-gain while `EPS`
reports the PCM accepted by the host, making optional gain and clipping visible
without altering OS sample data after conversion. Browser audio blocks are
queued and serialized; an in-flight localhost request must not discard the next
1024-sample block and punch periodic holes into the recording. Buffered PCM is
consumed until the queue is empty; packet age is only a UI freshness indicator
and no longer turns an already buffered tail into silence after 500 ms. The
large host ring absorbs scheduler and HTTP jitter without punching holes into
the waveform. When the unmodified sampling overlay enters RECORD at `ffe4f0`,
inactive history is trimmed to the newest four 1024-sample Chrome blocks. This
bounded transport cushion prevents zero-filled gaps at record start without
admitting a long stale pre-roll into a new sample. Adjacent browser blocks are
combined into one ordered PCM request when a backlog develops. Each microphone
activation has a unique host session. Activating a newer page invalidates older
tabs and reload remnants, while button-off and `pagehide` explicitly deactivate
the current session. This prevents two independent 48 kHz browser streams from
being interleaved in one ADC ring. ENTER remains an ordinary panel event; the
original OS decides its press/release behavior and exact recording endpoint.

An empty browser PCM ring is not a completed ADC conversion. GPR `0x80`
therefore keeps its low-byte valid marker clear until a real input sample is
available; returning a valid zero here wrote short silence islands at the start
of a recording while Chrome's first blocks were still arriving.

A complete post-fix recording/playback check used 468.75 Hz signed 16-bit PCM
at unity gain. The original OS programmed ES5505 bank 1 with start
`0x00644000`, end `0x05f70800`, accumulator `0x00644000`, and frequency
`0x00000c00`. This is 45,657 source samples and an expected non-looping output
duration of 1.02275 seconds at 29,761 Hz. The captured EPS output measured
468.75 Hz and 1.01287 seconds above the analysis threshold (about 10 ms, or
less than 1 percent, shorter because of the boundary windows). The selected
sample-RAM range measured 468.76 Hz and contained signal throughout except for
the expected leading boundary window. This validates the OS range, bank/word
addressing, frequency conversion, big-endian signed samples, interpolation and
natural ES5505 end handling for the controlled path.

The original OS currently allocates approximately `0x601910..0x7ff800` for a
new recording, nearly 2 MiB of 16-bit sample storage. That is about 1,024K
samples, so a full recording is approximately 23 seconds at 44.6 kHz and 34.4
seconds at 29.8 kHz. ENTER may end it earlier; reaching the allocation limit
continues to `PLAY ROOT KEY` under original-OS control.

The selector occupies GPR 81's low byte (`000002` is the highest rate). During
sampling target selection, a Track button must not leave the disk-load KPC
ready burst running indefinitely. The original overlay needs that handshake to
reach its RECORD entry at `ffe4f0`; it is canceled there so a subsequent ready
byte cannot be mistaken for an immediate stop request. A later ENTER click
provides the deliberate stop transition and continues to `PLAY ROOT KEY`.
Changing this sampling-rate selector is not expected to transpose the finished
sample or change its apparent duration: the OS compensates the recorded rate
with the ES5505 frequency increment at playback. The audible difference is
bandwidth/aliasing. For example selector 2 records at approximately 44.64 kHz
and uses an increment of 1.5 source samples per 29.761 kHz ES5505 output frame;
selector 3 records at approximately 29.76 kHz and should use unity increment.

The deterministic/plugin ADC clock must retain an absolute emulated-cycle
oscillator phase, just as the live path retains an absolute wall-clock phase.
Scheduling each next conversion relative to the CPU cycle at which the OS
noticed the preceding conversion accumulated the polling latency. During
RECORD at selector 3 this produced 28,735.6 conversions per second instead of
29,761.9 (-3.448 percent), which made the correctly programmed unity playback
increment sound approximately 60.7 cents sharp. The fixed clock advances from
the preceding oscillator deadline and drops only ticks missed between polls.
A dedicated original-OS regression now records on all seven rate selections:
their measured ADC clocks remain within 0.01 percent of the board divisors and
the OS still programs the expected ES5505 increments (from 1.5 at selector 2
through 0.375 at selector 8). The small -1.13 and -3.39 cent residuals at
selectors 5 and 7 respectively are the original frequency-register
quantization, not host resampling or a correction table in the emulator.

The ES5510 host model also keeps the writable special registers used by effect
and sampling overlays: `DLENGTH` (`f5`), `ABASE` (`f6`), `BBASE` (`f7`),
`DBASE` (`f8`), `SIGREG` (`f9`), `CCR` (`fa`), and `CMR` (`fb`). Their host
readback follows the hardware register map instead of treating `f5..fb` as
ordinary GPR storage; sampling-start verification specifically reads `ABASE`.
The board's unimplemented low address bits in `DLENGTH`, `ABASE`, and `BBASE`
read high. The ROM verifier requires this `...0f` readback; returning the raw
written zero bits makes a valid sampling-program load fail with `ERROR 145`.

The native audio path now preserves all four ES5505 stereo routes and executes
the original OS-uploaded ES5510 GPR/instruction program with its external
delay RAM. ES5505 route codes `0`, `1`, and `3` for Bus1, Bus2, and Bus3 feed
ES5510 serial inputs 0, 2, and 3; route code `2` is the separate Aux1 pair.
The OS-configured serial output 1 feeds the main EPS DAC. This is device-side
routing rather than a browser reverb substitute.
During effects, Host Serial Control `48` configures serial port 1 as the main
DAC output while ports 0, 2, and 3 accept the three ES5505 effect buses. During
sampling, the uploaded overlay instead consumes the mono ADC at serial input 0
and writes its filtered result to GPR `80` for the CPU. The hardware's automatic
sampling monitor remains a separate board route. In the VST adapter it is
active only while the original OS performs current GPR `80` conversions, is
sent equally to both main outputs, follows the same analog MIC/LINE frontend,
and then follows the physical master-volume ADC.
Main-board U41 is the hardware source multiplexer in front of ES5510 serial
input 0. Its A input is ES5505 `DSER0` (Bus 1), its B input is the mono ADC
`A/DATA`, and its `SAMPEN` select is MC68681 output OP2. MC68681 output pins
are active-low relative to the DUART set/reset command latch, so latch bit 2
set selects Bus 1 and bit 2 reset selects the ADC. Waveboy Audio-In effects
perform those actual writes at `$28001d/$28001f`; the emulator follows OP2 and
does not identify effect names or displayed parameters. OP7 independently
selects the original LINE/MIC analog feedback path.
The ES5510 host interface also holds execution during an instruction upload and
its verifier readback; otherwise the running DSP can legally rewrite a GPR
between two host reads and make the original OS report `EFFECT DOWNLOAD
FAILED`. Unit tests cover ES5505 channel assignment and the delayed ES5510 ALU
write pipeline. The machine regression records a sample through the original
OS and verifies the complete voice/bus/ESP/DAC path for ROM effects 10..13.

ROM effects 11 (`CMP+DIST+REV`) and 13 (`WAH+DIST+REV`) exposed two ES5510
state errors because, unlike the neighboring programs, both use conditional
instructions after saturating arithmetic and A/B-table reads. Saturation now
updates N/Z to describe the saturated result instead of the wrapped 24-bit
intermediate. The host's Halt Enable sequence is also honored before Host
Control bit 1 clears external delay RAM, so an effect no longer inherits
arbitrary table contents from the previous program. ROM 11/13 also download
514 lookup-table words through DADR. DADR is left-justified and must pass
through the current MEMSIZ mask/shift just like ABASE/BBASE accesses; treating
it as a raw 20-bit index stored the tables at `dfe00..` while the programs read
`03dfe..`. The focused unit test verifies that `3dfe00` maps to `03dfe` for
`MEMSIZ=0000ff`. The original-OS recording regression now produces distinct,
nonzero ESP returns for `10,11,12,13,11,10` without changing bus routing.

The normal live ENTER click produces the verified press/hold/release panel
packet. Actual recording is activated by the ENTER release edge, while an
already running recording stops on the next ENTER press edge, matching the
observed hardware behavior. The sampling overlay then installs its own DUART
vector and accepts Channel-B receive
interrupts only. MC68681 IRQ3 is level-sensitive: reading the last RHRB byte or
masking the source must deassert it within the current CPU slice. Keeping IRQ3
high until a fixed slice ended caused a second vector with an empty ISR, which
the unmodified overlay deliberately reported as `ERROR 145`. Immediate IRQ
refresh on RHRB, timer acknowledge, and IMR writes removes that false second
interrupt; the original recording loop reaches `PLAY ROOT KEY`.

The KPC ready byte scheduled after the ENTER release belongs to the transition
into RECORD. Once the sampling overlay reaches its recording entry point, any
still-pending copy of that `ff` is removed from the panel wire/RX queues;
otherwise the new sampling IRQ handler interprets it as an immediate stop.
During actual recording the OS sends a short blank VFD frame terminated by
`71`, not a full 22-character/`72` field. Publishing `71` as an atomic frame is
therefore required for the browser display to clear the readiness `*` exactly
when recording begins.

The physical keyboard and display processors do not need cycle-accurate CPU
emulation for the modern GUI. A protocol-compatible virtual panel is
preferable: it preserves main-CPU firmware behavior while exposing display
text and controls to a new interface. Complete and short VFD frames are now
published atomically after their UART burst, including blank recording frames,
so startup strings no longer appear one character at a time. The main fidelity
work is now live verification of the ES5510 effect/DAC path and remaining DMA
timing.

## References

- Ensoniq EPS-16 Plus Service Manual:
  https://www.vintagesynthparts.com/wp-content/uploads/2018/01/Ensoniq-EPS16-Service-Manual-with-searchable-text.pdf
- EPS/EPS-16 disk format notes:
  https://www.youngmonkey.ca/nose/audio_tech/synth/Ensoniq-DiskFormats.html
- Musashi 68000 core:
  https://github.com/kstenerud/Musashi
- MAME Ensoniq driver and panel devices (hardware/protocol reference):
  https://github.com/mamedev/mame/tree/master/src/mame/ensoniq
