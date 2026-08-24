Ensoniq EPS-16 Plus 1.0.9 external files
=========================================

The universal VST3 and Audio Unit v2 support Intel macOS 10.13 or newer and
Apple Silicon macOS 11 or newer.

Place the user-supplied files in this EPS_files folder, next to the installed
VST3 or Audio Unit plug-in. The plug-in never contains or redistributes these
copyrighted files.

The plug-in also searches both standard locations automatically, so existing
files do not need to be moved when the VST3 changes between a user and a
system-wide installation:

  ~/Library/Audio/Plug-Ins/VST3/EPS_files/
  /Library/Audio/Plug-Ins/VST3/EPS_files/
  ~/Library/Audio/Plug-Ins/Components/EPS_files/
  /Library/Audio/Plug-Ins/Components/EPS_files/

Preferred names:

  eps16plus-rom.bin   combined 128 KiB U28/U27 main ROM
  eps16plus-kpc.bin   32 KiB KPC 2.33 EPROM
  EPS130OS.img        819,200-byte logical OS disk

Instead of eps16plus-rom.bin, the two unchanged 64 KiB EPS-16 Plus 1.00F
chips may be placed here together. Their filenames do not matter: the plug-in
identifies U28 (upper/high byte) and U27 (lower/low byte) by SHA-256 and
interleaves them in memory without creating another ROM file. Unknown or
modified split ROMs are not accepted.

EPS130OS.hfe is also accepted. The original KPC filename
"Ensoniq EPS KPC2 v2.33 27c256.BIN" is recognized without renaming. If a
preferred name is absent, unique .bin/.rom files with the expected ROM sizes
and an .img with the expected disk size are detected.

Typical installed layout:

  ~/Library/Audio/Plug-Ins/VST3/
    Ensoniq EPS-16 Plus.vst3
    EPS_files/
      eps16plus-rom.bin
      eps16plus-kpc.bin
      EPS130OS.img

Ableton Live sampling
---------------------

The plug-in is an instrument. Its stereo "Sampling Input" is an auxiliary
input so Live can route audio to it through the plug-in's sidechain/input
chooser while MIDI remains on the instrument track. The EPS panel and OS own
the sampling sequence:

  SAMPLE -> TRACK 1 -> wait for * -> ENTER to record -> ENTER to stop

When "PLAY ROOT KEY" appears, send a MIDI note from the Live track or use the
expandable on-screen keyboard. Both use the original EPS keyboard-controller
path.

If the files were added after the plug-in was opened, remove and insert the
plug-in once so the authentic machine can initialize from reset.

Disk controls
-------------

The four small floppy icons at the upper right are instance-local:

  OS     reinsert the configured EPS130OS.img or EPS130OS.hfe
  NEW    insert a fresh formatted empty 800 KiB EPS data disk
  LOAD   import .efe, insert .img/HFE v1, or insert an experimental SCSI .iso
  SAVE   save the currently inserted disk as .img or HFE v1

OS, NEW and LOAD send the original hardware disk-change input to the running
EPS. NEW asks for confirmation because it ejects unsaved in-memory disk data.
SAVE includes sector changes made by the original operating system. Choose
.hfe when the exported disk will be used with HFE-compatible real hardware.
Experimental read-only SCSI ISO mounting is intended for loading instruments
from EPS CD-ROM libraries and their ISO backups. Insert the ISO with LOAD.

Experimental keyboard shortcuts
-------------------------------

With the plug-in editor focused, Control+1 through Control+0 open the matching
CMD page. Option+1 through Option+0 open the matching EDIT page. Shortcut and
mouse presses show a brief acknowledgement glow.
