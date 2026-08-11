# macOS VST3 and Audio Unit prototype

Status: milestone 1 on branch `vst3-prototype`, based on tag
`vst3-prototype-base` and commit `8371356`.

## Architecture finding

The verified emulator is not yet an instantiable library. CPU bus state,
peripheral state, boot, scheduling, CLI diagnostics, browser transport,
CoreMIDI and AudioQueue are all owned by the single translation unit
`native/rom_probe.c` and its `main` loop. Directly compiling that loop into a
plug-in callback would retain wall-clock and singleton assumptions, would not
be safe for multiple plug-in instances, and would violate the DAW-clock
boundary.

The smallest safe executable milestone is therefore a real VST3 bundle plus a
strict, tested emulator-sink boundary. It establishes the host contract before
the verified machine is mechanically extracted from `rom_probe.c`.

## Implemented in milestone 1

- Apple Silicon/macOS VST3 and Audio Unit instrument bundles with stereo sampling input,
  stereo output and MIDI input buses. It is advertised to the DAW as an
  instrument, not an audio effect. Release builds target macOS 11.0 or newer.
- No HTTP server, Web Audio, AudioQueue or CoreMIDI link in the plug-in.
- Fixed-capacity message-thread-to-audio-thread queue for physical panel and
  analog transitions; the audio callback performs no queue allocation.
- Sample-offset MIDI delivery and deterministic conversion of DAW sample time
  to the EPS 10 MHz CPU clock using an integer remainder accumulator.
- Conventional channel-1 Pitch Bend and Mod Wheel (CC1) drive the original EPS
  analog wheel channels. Polyphonic Key Pressure (`A0`) enters the original OS
  through the emulated MC68681 MIDI receiver; it is never converted into a
  second local KPC key-down, so it cannot delay the established Note Off path.
  The EPS has no MPE expression path, so
  member-channel pitch and Channel Pressure (`D0`) are ignored instead of being
  converted into artificial keyboard events. The original OS remains
  responsible for modulation routing and voice response.
- Stereo DAW input delivery to the emulator sampling boundary for every audio
  sample.
- Component-derived sampling frontend from analog schematic sheet 3. The
  original OS `INPUT LEVEL` variable selects the TL072/CD4053 LINE/MIC feedback
  gains (`2.2874x` LINE and `57.5868x` MIC, with MIC `25.1758x` relative to
  LINE), followed by both loaded
  third-order fixed R/C sections. The original ES5510 sampling program then
  applies the OS-selected digital cutoff table. No display-text inference,
  invented noise or generic host EQ is used.
- Stereo output delivery exclusively through the plug-in process callback.
- Hardware-timestamped ES5505/ES5510 output at the rate selected by the
  ES5505 active-voice register. A deterministic 48-tap polyphase resampler
  converts that native stream to the DAW rate without sample-and-hold images.
  Its fixed 12,288-cycle latency is reported to the host.
- Native original-layout panel surface. Buttons send raw KPC transitions only;
  they do not implement modes, menus or display state. RECORD,
  STOP/CONTINUE and PLAY use the service-manual/KPC-verified raw matrix codes.
  Shift-clicking PLAY emits overlapping RECORD-down, PLAY-down, PLAY-up and
  RECORD-up transitions, matching the physical two-button gesture.
- Ableton/VST3 tempo and transport are converted into sample-positioned MIDI
  realtime bytes (`f8/fa/fb/fc`). They enter the original OS through the
  emulated MC68681 channel-A receiver and its IRQ; the EPS CLOCK SOURCE setting
  remains authoritative. Clock phase is carried across audio-block boundaries,
  so a tick rounded onto the following block cannot be dropped. Existing note,
  Pitch Wheel and Mod Wheel paths are unchanged.
- While the plug-in editor has keyboard focus, the macOS cursor keys send the
  same raw press/release transitions as the four physical EPS arrow buttons.
  Losing focus releases every held arrow; other computer keys remain with the
  DAW.
- The VFD is blank until a real KPC/OS sink publishes it. No placeholder OS
  message is inserted into the display.
- Complete and short KPC/VFD frames are published atomically to the plug-in
  editor. In particular, the original OS `71` recording frame clears the
  sampling-ready `*`; the editor no longer reads the decoder's partially
  updated character workspace.
- ROM, KPC ROM and the default OS disk are discovered automatically from the
  external `EPS_files` folder. Four compact floppy controls provide media
  operations without restarting the machine: `OS` reinserts the configured
  system disk, `NEW` creates a fresh formatted 800 KiB EPS data disk, `LOAD`
  chooses an EPS `.IMG` or HFE v1 image, and `SAVE` exports the currently
  inserted disk as `.IMG` or standards-compatible HFE v1. `NEW` confirms in
  English before ejecting the current in-memory disk.
  Insertion generates the physical one-shot disk-change input for the original
  OS. No copyrighted image is in the source or bundle.
- Automatic discovery in the `EPS_files` folder beside the installed `.vst3`
  bundle. The package contains only an empty folder and README; user-supplied
  ROM and disk images remain external.
- WD1772 sector writes now follow the MC68450 memory-to-device direction into
  the instance-local logical disk. Save uses an atomic replacement file; HFE
  export MFM-encodes all 80 tracks, two sides and ten 512-byte sectors. An
  automated full-disk `IMG -> HFE -> IMG` round trip verifies every byte.
  The blank-disk regression verifies the original geometry, empty directory,
  15 reserved blocks, 1,585 free blocks, and all `DR`/`FB` signatures; the
  original OS then reads the new disk as `NO INSTRUMENTS`.
  Reading the generated HFE on physical replacement-drive hardware remains a
  release acceptance check rather than an emulator-side assumption.
- VOLUME maps to analog channel 5. DATA ENTRY uses the documented GUI
  `0..1023` to raw ADC `0..715` mapping on channel 3.
- VST state contains a checksummed full-machine snapshot: CPU and controller
  state, low/OS/sample RAM, ES5505, ES5510, analog sampling-filter history,
  panel, DMA, DUART and mounted-disk state. A project can therefore reopen with its instruments and samples
  already resident. ROM and KPC firmware bytes are deliberately excluded and
  still come from the user's external files.
- The rack VFD includes the three permanently printed annunciator rows above
  the 22-character line. All printed lamps are driven from the decoded
  `77..7c` KPC command banks, never from display text or browser menu state.
  The cyan-on-dark glass colour follows the supplied hardware close-up.
- The two physical LEDs above every Instrument/Track key mirror the `74..76`
  bank: upper means Loaded, lower means Selected, and the OS-provided flash
  state represents a stacked instrument.
- The changing 22-character line uses compact monospaced cells anchored at the
  left edge instead of distributing the characters across the full window;
  its renderer models the schematic-identified Futaba `FIP 22AM5R` topology:
  fourteen segments plus decimal point in each of 22 cells, rather than a
  desktop font. Decimal points and cursor segments remain independent
  attributes. Plain `62 ... 72` field transport does not draw a cursor;
  `62 60 03` selects the lower segment in every following OS-padded field
  cell, including populated characters and a bipolar sign. Text, decimal mask
  and the 22-bit segment mask are published atomically, while the OS still
  owns their exact values.
- The Level-Detect trigger-threshold star follows its observed two-byte
  one-based cell/`2a` pair and replaces its previous position atomically.
  Low KPC/VFD bytes are not treated as general character positions.
  Trigger threshold remains distinct from the supplementary Pre-Trigger
  function. Sampling Level Detect renders the original OS/KPC `00..0e`
  meter length as vertical VFD bars. Trigger Sensitivity remains the
  separately addressed star marker; neither value is inferred from host
  input amplitude.
- The mono Sampling Input is automatically monitored on both main outputs
  while the original OS is actively polling its sampling ADC, matching the
  hardware Level-Detect and recording path. This board-level monitor is not an
  always-on DAW dry mix; the physical VOLUME fader controls it.
- The editor follows the low-profile rack-panel proportions: Volume and mode
  controls at the left, page matrix and Data Entry in the centre, a full-width
  22-cell VFD above the eight track keys, and sampling/sequencer controls at
  the right. Resizing preserves the photographed rack aspect ratio.

Each plug-in processor owns a directly addressed machine context, including
CPU, RAM/sample RAM, KPC, ES5505, ES5510, panel, disk and audio-queue state.
No full-machine image is copied at DAW block boundaries. The VST-specific
Musashi build keeps only its transient execution registers in thread-local
storage; the durable CPU context follows its EPS instance when a DAW moves a
plug-in between worker threads.

There is no process-wide emulator mutex and no lock in the normal plug-in
audio path. Separate EPS instances can execute simultaneously on separate DAW
threads. State capture remains serialized with the same instance's callback
through JUCE's existing callback lock. Existing version-1/version-2 machine
snapshots and the VST state container remain byte-compatible.

## Deterministic callback contract

For every DAW block, `EmulatorBridge`:

1. dispatches queued physical controls in order, retaining a 50 ms emulated
   key interval when a fast GUI click queues press and release before the same
   audio block;
2. timestamps DAW MIDI at its sample offset;
3. submits the stereo DAW sample to the sampling input boundary;
4. advances the machine to the exact accumulated 10 MHz cycle target;
5. asks the machine for one stereo output sample.

After exactly one second at 44.1 or 48 kHz, the target is exactly 10,000,000
CPU cycles. Block partitioning does not alter the total.

## Build and test

JUCE is an external build dependency, like Musashi. Put a local JUCE 8 checkout
at `work/deps/JUCE` or set `EPS16_JUCE_DIR`.

```sh
cmake -S . -B work/vst3-build \
  -DEPS16_BUILD_VST3=ON \
  -DEPS16_JUCE_DIR="$PWD/work/deps/JUCE" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build work/vst3-build --target Eps16Plus_VST3 Eps16Plus_AU -j 6
cmake --build work/vst3-build --target eps16_plugin_packages
ctest --test-dir work/vst3-build --output-on-failure
```

The resource-backed regression executables additionally cover two independent
low-level machines, two simultaneous plug-in processors, the complete
sampling path, and VST-state round trips. ROM, KPC ROM and OS disk paths are
passed to those tests at run time and are never embedded in the binaries.

The raw bundles are written below
`work/vst3-build/vst3/Eps16Plus_artefacts/Release/`. The package targets copy
them without Finder/resource-fork metadata, ad-hoc sign and strictly verify
them, then write separate VST3 and AU archives in `vst3-package/` and
`au-package/`. Each archive contains the plug-in plus an `EPS_files` sibling
folder with a README, but no ROM, KPC ROM or OS disk.

## Remaining authentic-engine work

The instance extraction is complete for the VST path. Further engine changes
must continue to preserve deterministic boot, original-OS display, sampling,
audio and version-1/version-2 state regressions. The legacy KPC path retains
the opt-in rules from `docs/kpc-migration.md`; no Enhanced parameter or
direct-RAM feature belongs in the authentic prototype.
