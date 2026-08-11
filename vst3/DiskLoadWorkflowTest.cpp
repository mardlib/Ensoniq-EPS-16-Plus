#include "EmulatorBridge.h"
#include "ProbeMachineSink.h"

#include <array>
#include <iostream>
#include <string>

namespace {
constexpr int blockSize = 512;
constexpr double sampleRate = 48000.0;

void runFor(eps16::vst3::EmulatorBridge &bridge, double seconds) {
    std::array<float, blockSize> input{};
    std::array<float, blockSize> left{};
    std::array<float, blockSize> right{};
    const auto blocks = static_cast<int>(seconds * sampleRate / blockSize) + 1;
    for (int block = 0; block < blocks; ++block)
        bridge.process(input.data(), input.data(), left.data(), right.data(),
                       blockSize, nullptr, 0);
}

bool click(eps16::vst3::EmulatorBridge &bridge, std::uint8_t code,
           double settleSeconds = 2.0) {
    if (!bridge.enqueuePanelTransition(code, true) ||
        !bridge.enqueuePanelTransition(code, false))
        return false;
    runFor(bridge, settleSeconds);
    return true;
}

void printDisplay(const char *label, const eps16::vst3::ProbeMachineSink &machine) {
    std::cout << label << "=|" << machine.display() << "| cursor="
              << machine.cursorStart() << ".." << machine.cursorEnd()
              << " mask=" << std::hex << machine.cursorSegmentMask()
              << std::dec << '\n';
}
} // namespace

int main(int argc, char **argv) {
    if (argc != 5 && argc != 6) {
        std::cerr << "usage: " << argv[0]
                  << " ROM KPC OS INSTRUMENT_DISK [SECOND_DISK]\n";
        return 2;
    }
    eps16::vst3::ProbeMachineSink machine;
    machine.configure(argv[1], {}, {}, argv[2], argv[3]);
    eps16::vst3::EmulatorBridge bridge(machine);
    if (!bridge.prepare(sampleRate) || !machine.isReady()) return 1;
    runFor(bridge, 22.0);
    if (!machine.insertDisk(argv[4], "Test disk")) return 1;
    runFor(bridge, 2.0);

    /* A normal GUI click can enqueue both edges before the next audio block.
       The bridge must retain a physical key-down interval for the KPC. */
    if (!click(bridge, 0x0f)) return 1;
    const auto display = machine.display();
    std::cout << "display=|" << display << "| status=" << machine.status()
              << '\n';
    if (!display.starts_with("FILE ") || machine.illegalInstructions()) return 1;

    /* Load the selected file into two slots, then exercise the LOAD/Instrument
       volume page exactly as the GUI does. This page uses an original-OS
       incremental field update rather than redrawing the complete frame. */
    if (!click(bridge, 0x23) || !click(bridge, 0x02, 8.0)) return 1;
    printDisplay("loaded_track_1", machine);
    if (!click(bridge, 0x1a) || !click(bridge, 0x0f) ||
        !click(bridge, 0x23) || !click(bridge, 0x08, 8.0))
        return 1;
    printDisplay("loaded_track_2", machine);
    if (!click(bridge, 0x1a) || !click(bridge, 0x02)) return 1;
    printDisplay("load_volume_before", machine);
    const auto before = machine.display();
    const auto beforeMask = machine.cursorSegmentMask();
    if (before.find("VOLUME=") == std::string::npos) return 1;
    if (!bridge.enqueueAnalog(3, 100)) return 1;
    runFor(bridge, 2.0);
    printDisplay("load_volume_after", machine);
    const auto after = machine.display();
    if (after == before || beforeMask || machine.cursorSegmentMask()) return 1;
    if (!click(bridge, 0x08) || !click(bridge, 0x02)) return 1;
    printDisplay("load_volume_after_reselect", machine);
    const auto afterReselect = machine.display();
    if (afterReselect != after || machine.cursorSegmentMask()) return 1;

    if (!click(bridge, 0x05) || !click(bridge, 0x0f)) return 1;
    for (unsigned int page = 0;
         page < 100 && machine.display().find("NAME=") == std::string::npos;
         ++page)
        if (!click(bridge, 0x11, 0.2)) return 1;
    printDisplay("instrument_name_before", machine);
    if (machine.display().find("NAME=") == std::string::npos) return 1;
    const auto nameBefore = machine.display();
    if (!bridge.enqueueAnalog(3, 600)) return 1;
    runFor(bridge, 2.0);
    printDisplay("instrument_name_first", machine);
    const auto nameFirst = machine.display();
    if (nameFirst == nameBefore) return 1;
    if (!click(bridge, 0x11)) return 1;
    const auto secondMask = machine.cursorSegmentMask();
    printDisplay("instrument_name_second_selected", machine);
    if (!bridge.enqueueAnalog(3, 100)) return 1;
    runFor(bridge, 2.0);
    printDisplay("instrument_name_second", machine);
    const auto nameSecond = machine.display();
    if (nameSecond == nameFirst || !secondMask ||
        machine.cursorSegmentMask() != secondMask)
        return 1;
    if (!click(bridge, 0x1a)) return 1;
    if (argc == 6) {
        if (!machine.insertDisk(argv[5], "Second test disk")) return 1;
        runFor(bridge, 2.0);
        if (!bridge.enqueuePanelTransition(0x0f, true) ||
            !bridge.enqueuePanelTransition(0x0f, false))
            return 1;
        runFor(bridge, 2.0);
        const auto secondDisplay = machine.display();
        std::cout << "second_display=|" << secondDisplay << "| status="
                  << machine.status() << '\n';
        if (!secondDisplay.starts_with("FILE ") || machine.illegalInstructions())
            return 1;
    }
    if (!machine.createBlankDisk()) return 1;
    runFor(bridge, 2.0);
    if (!bridge.enqueuePanelTransition(0x0f, true) ||
        !bridge.enqueuePanelTransition(0x0f, false))
        return 1;
    runFor(bridge, 2.0);
    const auto blankDisplay = machine.display();
    std::cout << "blank_display=|" << blankDisplay << "| status="
              << machine.status() << '\n';
    return blankDisplay.starts_with("NO INSTRUMENTS") &&
                   !machine.illegalInstructions() ? 0 : 1;
}
