#include "ProbeMachineSink.h"
#include "ProbeMachine.h"

#include <algorithm>
#include <utility>

namespace eps16::vst3 {
namespace {
class MachineAccess {
public:
    explicit MachineAccess(Eps16ProbeMachine *target)
        : machine(target), active(eps16_probe_machine_begin(machine) != 0) {}
    ~MachineAccess() {
        if (active) eps16_probe_machine_end(machine);
    }
    explicit operator bool() const { return active; }

private:
    Eps16ProbeMachine *machine{};
    bool active{};
};
} // namespace

ProbeMachineSink::ProbeMachineSink() {
    machine = eps16_probe_machine_create();
    for (std::size_t index = 0; index < 22; ++index)
        displayCharacters[index].store(' ', std::memory_order_relaxed);
    displayCharacters[22].store('\0', std::memory_order_relaxed);
}

ProbeMachineSink::~ProbeMachineSink() {
    eps16_probe_machine_destroy(machine);
}

bool ProbeMachineSink::beginBlock() {
    blockActive = isReady() && machine && eps16_probe_machine_begin(machine);
    return blockActive;
}

void ProbeMachineSink::endBlock() {
    if (!blockActive) return;
    publishDisplay();
    eps16_probe_machine_end(machine);
    blockActive = false;
}

void ProbeMachineSink::configure(std::string romPath,
                                 std::string upperRomPath,
                                 std::string lowerRomPath,
                                 std::string kpcPath,
                                 std::string osDiskPath) {
    rom = std::move(romPath);
    upperRom = std::move(upperRomPath);
    lowerRom = std::move(lowerRomPath);
    kpc = std::move(kpcPath);
    disk = std::move(osDiskPath);
}

bool ProbeMachineSink::insertDisk(const std::string &path,
                                  const std::string &label) {
    if (!isReady() || path.empty()) return false;
    MachineAccess access(machine);
    if (!access) return false;
    char error[256]{};
    if (!eps16_probe_machine_insert_disk(path.c_str(), error, sizeof(error))) {
        const std::lock_guard<std::mutex> lock(statusMutex);
        statusText = error[0] ? error : "disk insertion failed";
        return false;
    }
    {
        const std::lock_guard<std::mutex> lock(statusMutex);
        statusText = label + " inserted";
    }
    return true;
}

bool ProbeMachineSink::insertScsiCd(const std::string &path) {
    if (!isReady() || path.empty()) return false;
    MachineAccess access(machine);
    if (!access) return false;
    char error[256]{};
    if (!eps16_probe_machine_insert_scsi_cd(path.c_str(), error,
                                             sizeof(error))) {
        const std::lock_guard<std::mutex> lock(statusMutex);
        statusText = error[0] ? error : "SCSI CD image insertion failed";
        return false;
    }
    const std::lock_guard<std::mutex> lock(statusMutex);
    statusText = "SCSI CD image mounted at SCSI 0";
    return true;
}

bool ProbeMachineSink::createBlankDisk() {
    if (!isReady()) return false;
    MachineAccess access(machine);
    if (!access) return false;
    char error[256]{};
    if (!eps16_probe_machine_create_blank_disk(error, sizeof(error))) {
        const std::lock_guard<std::mutex> lock(statusMutex);
        statusText = error[0] ? error : "blank disk creation failed";
        return false;
    }
    const std::lock_guard<std::mutex> lock(statusMutex);
    statusText = "New blank EPS disk inserted";
    return true;
}

bool ProbeMachineSink::saveDisk(const std::string &path, bool hfeFormat) {
    if (!isReady() || path.empty()) return false;
    MachineAccess access(machine);
    if (!access) return false;
    char error[256]{};
    if (!eps16_probe_machine_save_disk(path.c_str(), hfeFormat ? 1 : 0,
                                       error, sizeof(error))) {
        const std::lock_guard<std::mutex> lock(statusMutex);
        statusText = error[0] ? error : "disk save failed";
        return false;
    }
    {
        const std::lock_guard<std::mutex> lock(statusMutex);
        statusText = hfeFormat ? "disk saved as HFE" : "disk saved as IMG";
    }
    return true;
}

void ProbeMachineSink::prepare(double dawSampleRate) {
    if (!resampler.prepare(dawSampleRate)) {
        const std::lock_guard<std::mutex> lock(statusMutex);
        statusText = "unsupported DAW sample rate";
        ready.store(false, std::memory_order_release);
        return;
    }
    MachineAccess access(machine);
    if (!access) {
        const std::lock_guard<std::mutex> lock(statusMutex);
        statusText = "machine instance allocation failed";
        ready.store(false, std::memory_order_release);
        return;
    }
    char error[256]{};
    const bool initialized = !rom.empty()
        ? eps16_probe_machine_initialize(rom.c_str(), kpc.c_str(), disk.c_str(),
                                         error, sizeof(error)) != 0
        : eps16_probe_machine_initialize_split_rom(
              upperRom.c_str(), lowerRom.c_str(), kpc.c_str(), disk.c_str(),
              error, sizeof(error)) != 0;
    if (initialized) {
        eps16_probe_machine_sampling_input_rate(dawSampleRate);
        cycleBase = eps16_probe_machine_cycles();
        discardQueuedAudio();
        {
            const std::lock_guard<std::mutex> lock(statusMutex);
            statusText = "authentic machine running";
        }
        ready.store(true, std::memory_order_release);
        publishDisplay();
    } else {
        {
            const std::lock_guard<std::mutex> lock(statusMutex);
            statusText = error[0] ? error : "machine initialization failed";
        }
        ready.store(false, std::memory_order_release);
    }
}

void ProbeMachineSink::discardQueuedAudio() {
    Eps16ProbeAudioFrame staleFrames[32];
    while (eps16_probe_machine_drain_audio(staleFrames, 32) == 32) {}
}

void ProbeMachineSink::runUntil(std::uint64_t absoluteCpuCycle) {
    if (!isReady()) return;
    eps16_probe_machine_run_until(cycleBase + absoluteCpuCycle);
    Eps16ProbeAudioFrame frames[32];
    for (;;) {
        const auto count = eps16_probe_machine_drain_audio(frames, 32);
        for (std::size_t index = 0; index < count; ++index) {
            const auto &frame = frames[index];
            resampler.push(frame.cpu_cycle - cycleBase, frame.clock_divider,
                           frame.left, frame.right);
        }
        if (count < 32) break;
    }
}

void ProbeMachineSink::midi(std::uint8_t status, std::uint8_t data1,
                            std::uint8_t data2, std::uint64_t) {
    if (isReady()) eps16_probe_machine_midi(status, data1, data2);
}

void ProbeMachineSink::keyboard(std::uint8_t note, std::uint8_t velocity,
                                bool pressed, std::uint64_t) {
    if (isReady())
        eps16_probe_machine_keyboard(note, velocity, pressed ? 1 : 0);
}

void ProbeMachineSink::midiBytes(const std::uint8_t *bytes, std::size_t size,
                                 std::uint64_t) {
    if (isReady()) eps16_probe_machine_midi_bytes(bytes, size);
}

std::size_t ProbeMachineSink::drainMidiOutput(std::uint8_t *bytes,
                                              std::size_t capacity) {
    return isReady()
        ? eps16_probe_machine_drain_midi_output(bytes, capacity) : 0;
}

void ProbeMachineSink::panelByte(std::uint8_t value, std::uint64_t) {
    if (isReady()) eps16_probe_machine_panel_byte(value);
}

void ProbeMachineSink::analog(unsigned int channel, std::uint16_t value,
                              std::uint64_t) {
    if (isReady()) eps16_probe_machine_analog(channel, value);
}

void ProbeMachineSink::samplingInput(float left, float right, std::uint64_t) {
    const float mono = std::clamp(0.5f * (left + right), -1.0f, 1.0f);
    samplingMonitorSample = isReady()
        ? eps16_probe_machine_sampling_input(mono, mono) : mono;
}

void ProbeMachineSink::stereoOutput(float &left, float &right,
                                    std::uint64_t cycle) {
    if (!isReady()) {
        left = 0.0f;
        right = 0.0f;
        return;
    }
    resampler.output(cycle, left, right);
    /* The sampling board's mono input monitor is present only while the
       original OS has selected the Level-Detect VFD mode with its trigger
       marker. This is the EPS board route, not the disabled VST Main Input
       bus and not an always-on host dry mix. */
    if (eps16_probe_machine_sampling_monitor_active()) {
        const float gain =
            (float)eps16_probe_machine_master_volume() / 1023.0f;
        const float monitor = samplingMonitorSample * gain;
        left = std::clamp(left + monitor, -1.0f, 1.0f);
        right = std::clamp(right + monitor, -1.0f, 1.0f);
    }
}

void ProbeMachineSink::publishDisplay() {
    char text[23];
    int cursorStart = -1;
    int cursorEnd = -1;
    eps16_probe_machine_display(text);
    const auto decimalMask = eps16_probe_machine_decimal_mask();
    const auto cursorSegmentMask =
        eps16_probe_machine_cursor_segment_mask();
    eps16_probe_machine_cursor(&cursorStart, &cursorEnd);
    for (std::size_t index = 0; index < 23; ++index)
        displayCharacters[index].store(text[index], std::memory_order_relaxed);
    displayCursorStart.store(cursorStart, std::memory_order_relaxed);
    displayCursorEnd.store(cursorEnd, std::memory_order_relaxed);
    displayCursorSegmentMask.store(cursorSegmentMask,
                                   std::memory_order_relaxed);
    displayDecimalMask.store(decimalMask, std::memory_order_relaxed);
    for (unsigned int bank = 0; bank < displayIndicatorOn.size(); ++bank) {
        displayIndicatorOn[bank].store(
            eps16_probe_machine_indicator_on(bank), std::memory_order_relaxed);
        displayIndicatorFlash[bank].store(
            eps16_probe_machine_indicator_flash(bank),
            std::memory_order_relaxed);
    }
    displayIllegalInstructions.store(
        eps16_probe_machine_illegal_instructions(), std::memory_order_relaxed);
}

std::string ProbeMachineSink::display() const {
    std::string result(22, ' ');
    for (std::size_t index = 0; index < 22; ++index)
        result[index] = displayCharacters[index].load(std::memory_order_relaxed);
    return result;
}

std::string ProbeMachineSink::status() const {
    const std::lock_guard<std::mutex> lock(statusMutex);
    return statusText;
}

std::size_t ProbeMachineSink::illegalInstructions() const {
    return isReady()
        ? displayIllegalInstructions.load(std::memory_order_relaxed) : 0;
}

std::vector<std::uint8_t> ProbeMachineSink::captureState() const {
    MachineAccess access(machine);
    const auto size = isReady() && access
        ? eps16_probe_machine_state_size() : 0;
    std::vector<std::uint8_t> result(size);
    if (size && !eps16_probe_machine_save_state(result.data(), result.size()))
        result.clear();
    return result;
}

std::vector<std::uint8_t>
ProbeMachineSink::captureRam(std::uint32_t address, std::size_t size) const {
    MachineAccess access(machine);
    std::vector<std::uint8_t> result(isReady() && access ? size : 0);
    if (!result.empty() && eps16_probe_machine_debug_read_ram(
            address, result.data(), result.size()) != result.size())
        result.clear();
    return result;
}

bool ProbeMachineSink::debugWriteRam(std::uint32_t address, const void *data,
                                     std::size_t size) {
    if (!isReady() || !data || !size) return false;
    MachineAccess access(machine);
    return access && eps16_probe_machine_debug_write_ram(address, data, size)
        == size;
}

bool ProbeMachineSink::restoreState(const void *data, std::size_t size) {
    if (!isReady() || !data || !size) return false;
    MachineAccess access(machine);
    if (!access) return false;
    char error[256]{};
    if (!eps16_probe_machine_load_state(data, size, error, sizeof(error))) {
        const std::lock_guard<std::mutex> lock(statusMutex);
        statusText = error[0] ? error : "machine snapshot restore failed";
        return false;
    }
    cycleBase = eps16_probe_machine_cycles();
    discardQueuedAudio();
    resampler.reset();
    publishDisplay();
    {
        const std::lock_guard<std::mutex> lock(statusMutex);
        statusText = "authentic machine restored from VST state";
    }
    return true;
}

} // namespace eps16::vst3
