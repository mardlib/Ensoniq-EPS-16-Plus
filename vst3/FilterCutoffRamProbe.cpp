#include "EmulatorBridge.h"
#include "ProbeMachineSink.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {
constexpr int blockSize = 512;
constexpr double sampleRate = 48000.0;

struct RamRegion {
    std::uint32_t base{};
    const char *name{};
    std::vector<std::uint8_t> bytes;
};

struct RamImage {
    std::array<RamRegion, 3> regions;
};

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
           double settleSeconds = 1.0) {
    if (!bridge.enqueuePanelTransition(code, true) ||
        !bridge.enqueuePanelTransition(code, false))
        return false;
    runFor(bridge, settleSeconds);
    return true;
}

RamImage capture(const eps16::vst3::ProbeMachineSink &machine) {
    RamImage image;
    image.regions[0] =
        {0x000000, "low", machine.captureRam(0x000000, 0x8000)};
    image.regions[1] =
        {0x580000, "sample", machine.captureRam(0x580000, 0x280000)};
    image.regions[2] =
        {0xff0000, "os", machine.captureRam(0xff0000, 0x10000)};
    return image;
}

bool openCutoffPage(eps16::vst3::EmulatorBridge &bridge,
                    const eps16::vst3::ProbeMachineSink &machine) {
    for (unsigned int attempt = 0;
         attempt < 3 && machine.display().find("LYR=") == std::string::npos;
         ++attempt)
        if (!click(bridge, 0x05)) return false;
    std::cout << "edit_selection=|" << machine.display() << "|\n";
    if (machine.display().find("LYR=") == std::string::npos) return false;
    const auto allLow = machine.captureRam(0x000000, 0x8000);
    if (machine.display().find("WS=ALL") != std::string::npos) {
        if (!bridge.enqueueKeyboardTransition(60, 100, true)) return false;
        runFor(bridge, 0.3);
        if (!bridge.enqueueKeyboardTransition(60, 0, false)) return false;
        runFor(bridge, 0.7);
        std::cout << "wavesample_selected=|" << machine.display() << "|\n";
    }
    if (machine.display().find("WS=ALL") != std::string::npos) {
        if (!click(bridge, 0x11, 0.5)) return false;
        std::cout << "wavesample_field=|" << machine.display() << "|\n";
        if (!click(bridge, 0x0a, 0.7)) return false;
        std::cout << "wavesample_number=|" << machine.display() << "|\n";
    }
    if (machine.display().find("WS=ALL") != std::string::npos) return false;
    const auto selectedLow = machine.captureRam(0x000000, 0x8000);
    if (allLow.size() == selectedLow.size()) {
        for (std::size_t offset = 0; offset + 3 < allLow.size(); offset += 2) {
            const auto value = [](const std::vector<std::uint8_t> &bytes,
                                  std::size_t at) {
                return (static_cast<std::uint32_t>(bytes[at]) << 24) |
                       (static_cast<std::uint32_t>(bytes[at + 1]) << 16) |
                       (static_cast<std::uint32_t>(bytes[at + 2]) << 8) |
                       bytes[at + 3];
            };
            const auto before = value(allLow, offset) & 0xffffff;
            const auto after = value(selectedLow, offset) & 0xffffff;
            if (before != after &&
                ((before >= 0x580000 && before < 0x800000) ||
                 (after >= 0x580000 && after < 0x800000)))
                std::cout << "selection_pointer_change low $" << std::hex
                          << std::setw(4) << std::setfill('0') << offset
                          << " $" << std::setw(6) << before << " -> $"
                          << std::setw(6) << after << std::dec << '\n';
        }
    }
    if (!click(bridge, 0x19)) return false;
    for (unsigned int page = 0; page < 12; ++page) {
        const auto text = machine.display();
        std::cout << "filter_page=|" << text << "|\n";
        if (text.find("CUTOFF") != std::string::npos) return true;
        if (!click(bridge, 0x11, 0.4)) return false;
    }
    return false;
}

RamImage captureCutoff(const eps16::vst3::ProbeMachineSink &machine,
                       const char *label) {
    std::cout << label << "=|" << machine.display() << "|\n";
    return capture(machine);
}

bool step(eps16::vst3::EmulatorBridge &bridge, std::uint8_t direction,
          unsigned int count = 1) {
    for (unsigned int index = 0; index < count; ++index)
        if (!click(bridge, direction, 0.7)) return false;
    return true;
}

std::uint16_t read16(const std::vector<std::uint8_t> &bytes,
                     std::size_t offset) {
    return static_cast<std::uint16_t>(
        (static_cast<unsigned int>(bytes[offset]) << 8) | bytes[offset + 1]);
}

std::uint32_t read32(const std::vector<std::uint8_t> &bytes,
                     std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
           bytes[offset + 3];
}

void reportPointerNeighborhood(const RamImage &image, std::uint32_t candidate) {
    std::cout << "context $" << std::hex << std::setw(6) << std::setfill('0')
              << candidate << ":";
    for (const auto &region : image.regions) {
        if (candidate < region.base ||
            candidate >= region.base + region.bytes.size())
            continue;
        const auto center = static_cast<std::size_t>(candidate - region.base);
        const auto begin = center > 24 ? center - 24 : 0;
        const auto end = std::min(region.bytes.size(), center + 25);
        for (auto offset = begin; offset < end; ++offset)
            std::cout << ' ' << std::setw(2)
                      << static_cast<unsigned int>(region.bytes[offset]);
    }
    std::cout << std::dec << '\n';

    std::size_t references = 0;
    const auto windowBegin = candidate > 0x400 ? candidate - 0x400 : 0;
    for (const auto &region : image.regions) {
        for (std::size_t offset = 0; offset + 3 < region.bytes.size();
             offset += 2) {
            const auto target = read32(region.bytes, offset) & 0xffffff;
            if (target >= windowBegin && target <= candidate &&
                references++ < 64)
                std::cout << "pointer " << region.name << " $" << std::hex
                          << std::setw(6) << std::setfill('0')
                          << region.base + offset << " -> $" << std::setw(6)
                          << target << " candidate_offset=+$"
                          << candidate - target << std::dec << '\n';
        }
    }
    std::cout << "pointer_count_near_candidate=" << references << '\n';
}

std::uint32_t unpackOffset(const std::vector<std::uint8_t> &bytes,
                           std::size_t at) {
    const auto byte0 = bytes[at];
    const auto byte2 = bytes[at + 2];
    const auto byte3 = bytes[at + 3];
    return ((static_cast<std::uint32_t>(byte3) >> 4) << 20) |
           (static_cast<std::uint32_t>(byte0) << 12) |
           (static_cast<std::uint32_t>(byte2) << 4);
}

void reportInstrumentObject(const RamImage &image, const std::string &name) {
    constexpr std::size_t objectHeaderBytes = 10;
    constexpr std::size_t wavesampleTableWord = 61;
    constexpr unsigned int wavesampleNumber = 1;
    const auto entryBytes = objectHeaderBytes +
        (wavesampleTableWord + wavesampleNumber * 2) * 2;
    for (const auto &memory : image.regions) {
      std::size_t rawNameMatches = 0;
      for (std::size_t nameOffset = objectHeaderBytes;
         nameOffset + name.size() * 2 <= memory.bytes.size(); nameOffset += 2) {
        bool matches = true;
        for (std::size_t character = 0; character < name.size(); ++character)
            if (memory.bytes[nameOffset + character * 2] !=
                static_cast<std::uint8_t>(name[character])) {
                matches = false;
                break;
            }
        if (!matches) continue;
        if (rawNameMatches++ < 16)
            std::cout << "instrument_name_pattern " << memory.name << " $"
                      << std::hex << std::setw(6) << std::setfill('0')
                      << memory.base + nameOffset << std::dec << '\n';
        const auto objectOffset = nameOffset - objectHeaderBytes;
        if ((objectOffset & 0x0f) || objectOffset + entryBytes + 4 > memory.bytes.size())
            continue;
        const auto objectAddress = memory.base + objectOffset;
        const auto wavesampleOffset =
            unpackOffset(memory.bytes, objectOffset + entryBytes);
        const auto wavesampleAddress = objectAddress + wavesampleOffset;
        std::cout << "instrument_candidate address=$" << std::hex
                  << std::setw(6) << std::setfill('0') << objectAddress
                  << " packed_ws1_offset=$" << std::setw(6)
                  << wavesampleOffset
                  << std::dec << '\n';
        if (wavesampleAddress < 0x580000 || wavesampleAddress >= 0x800000)
            continue;
        std::cout << "instrument_object name=\"" << name << "\" address=$"
                  << std::hex << std::setw(6) << std::setfill('0')
                  << objectAddress << " ws1=$" << std::setw(6)
                  << wavesampleAddress << " f1_cutoff=$" << std::setw(6)
                  << wavesampleAddress + 0xbc << std::dec << '\n';
        for (const auto &region : image.regions) {
            for (std::size_t offset = 0; offset + 3 < region.bytes.size();
                 offset += 2) {
                const auto raw = read32(region.bytes, offset) & 0xffffff;
                const auto packed = unpackOffset(region.bytes, offset);
                if (raw == objectAddress || packed == objectAddress)
                    std::cout << "instrument_reference " << region.name
                              << " $" << std::hex << std::setw(6)
                              << region.base + offset << " encoding="
                  << (raw == objectAddress ? "raw" : "packed")
                              << std::dec << '\n';
            }
        }
      }
    }
}

std::vector<std::uint32_t>
reportCandidates(const char *track, const std::array<RamImage, 4> &images) {
    std::size_t byteCount = 0;
    std::size_t wordCount = 0;
    std::vector<std::uint32_t> byteCandidates;
    std::cout << "candidate_set=" << track << '\n';
    const auto &low = images[0].regions[0].bytes;
    if (low.size() >= 0x1860) {
        std::cout << "selected_pointer_low_182c=$" << std::hex
                  << std::setw(6) << std::setfill('0')
                  << (read32(low, 0x182c) & 0xffffff) << std::dec << '\n';
        std::cout << "low_1800:" << std::hex;
        for (std::size_t offset = 0x1800; offset < 0x1860; ++offset)
            std::cout << ' ' << std::setw(2) << std::setfill('0')
                      << static_cast<unsigned int>(low[offset]);
        std::cout << std::dec << '\n';
    }
    const auto &os = images[0].regions[2].bytes;
    if (os.size() >= 0xdcc0) {
        std::cout << "os_dc80_longs:" << std::hex;
        for (std::size_t offset = 0xdc80; offset < 0xdcc0; offset += 4)
            std::cout << " [$ff" << std::setw(4) << std::setfill('0')
                      << offset << "]=$" << std::setw(6)
                      << (read32(os, offset) & 0xffffff);
        std::cout << std::dec << '\n';
    }
    for (std::size_t regionIndex = 0; regionIndex < images[0].regions.size();
         ++regionIndex) {
        const auto &first = images[0].regions[regionIndex];
        const auto &second = images[1].regions[regionIndex];
        const auto &third = images[2].regions[regionIndex];
        const auto &restored = images[3].regions[regionIndex];
        if (first.bytes.empty() || first.bytes.size() != second.bytes.size() ||
            first.bytes.size() != third.bytes.size() ||
            first.bytes.size() != restored.bytes.size())
            continue;
        for (std::size_t offset = 0; offset < first.bytes.size(); ++offset) {
            const auto a = first.bytes[offset];
            const auto b = second.bytes[offset];
            const auto c = third.bytes[offset];
            const auto a2 = restored.bytes[offset];
            if (a == a2 && a != b && b != c && c != a2) {
                if (byteCount < 128)
                    std::cout << "byte " << first.name << " $" << std::hex
                              << std::setw(6) << std::setfill('0')
                              << first.base + offset << " "
                              << std::setw(2) << static_cast<unsigned int>(a)
                              << "->" << std::setw(2)
                              << static_cast<unsigned int>(b) << "->"
                              << std::setw(2) << static_cast<unsigned int>(c)
                              << "->" << std::setw(2)
                              << static_cast<unsigned int>(a2) << std::dec
                              << '\n';
                ++byteCount;
                byteCandidates.push_back(first.base + offset);
            }
        }
        for (std::size_t offset = 0; offset + 1 < first.bytes.size();
             offset += 2) {
            const auto a = read16(first.bytes, offset);
            const auto b = read16(second.bytes, offset);
            const auto c = read16(third.bytes, offset);
            const auto a2 = read16(restored.bytes, offset);
            if (a == a2 && a != b && b != c && c != a2) {
                if (wordCount < 128)
                    std::cout << "word " << first.name << " $" << std::hex
                              << std::setw(6) << std::setfill('0')
                              << first.base + offset << " " << std::setw(4)
                              << a << "->" << std::setw(4) << b << "->"
                              << std::setw(4) << c << "->" << std::setw(4)
                              << a2 << std::dec << '\n';
                ++wordCount;
            }
        }
    }
    std::cout << "candidate_counts " << track << " bytes=" << byteCount
              << " words=" << wordCount << '\n';
    for (const auto candidate : byteCandidates)
        reportPointerNeighborhood(images[0], candidate);
    return byteCandidates;
}

bool writeCutoffWord(eps16::vst3::ProbeMachineSink &machine,
                     std::uint32_t address, std::uint8_t cutoff) {
    const std::array<std::uint8_t, 2> word{{cutoff, 0x00}};
    return machine.debugWriteRam(address, word.data(), word.size());
}

std::uint16_t readRamWord(const eps16::vst3::ProbeMachineSink &machine,
                          std::uint32_t address) {
    const auto bytes = machine.captureRam(address, 2);
    return bytes.size() == 2
        ? static_cast<std::uint16_t>((bytes[0] << 8) | bytes[1]) : 0xffff;
}
} // namespace

int main(int argc, char **argv) {
    if (argc != 5) {
        std::cerr << "usage: " << argv[0]
                  << " ROM KPC OS INSTRUMENT_DISK\n";
        return 2;
    }
    eps16::vst3::ProbeMachineSink machine;
    machine.configure(argv[1], {}, {}, argv[2], argv[3]);
    eps16::vst3::EmulatorBridge bridge(machine);
    if (!bridge.prepare(sampleRate) || !machine.isReady()) return 1;
    runFor(bridge, 22.0);
    if (!machine.insertDisk(argv[4], "Cutoff probe disk")) return 1;
    runFor(bridge, 2.0);

    /* ED-001 is an original bank. Loading it once populates several instrument
       slots, allowing the same relative parameter to be measured in unrelated
       instruments without assuming a RAM structure or file-format layout. */
    if (!click(bridge, 0x0f) || !click(bridge, 0x23) ||
        !click(bridge, 0x02, 8.0) || !click(bridge, 0x1a) ||
        !click(bridge, 0x02))
        return 1;
    std::cout << "track1_selected=|" << machine.display() << "|\n";

    if (!openCutoffPage(bridge, machine)) {
        std::cerr << "F1 cutoff page not reached: |" << machine.display()
                  << "|\n";
        return 1;
    }
    std::array<RamImage, 4> track1;
    track1[0] = captureCutoff(machine, "track1_a");
    if (!step(bridge, 0x0a)) return 1;
    track1[1] = captureCutoff(machine, "track1_b");
    if (!step(bridge, 0x0a)) return 1;
    track1[2] = captureCutoff(machine, "track1_c");
    if (!step(bridge, 0x0b, 2)) return 1;
    track1[3] = captureCutoff(machine, "track1_a2");
    const auto track1Candidates = reportCandidates("track1", track1);
    reportInstrumentObject(track1[0], "FLUTE 1");

    if (!click(bridge, 0x08) || !openCutoffPage(bridge, machine)) return 1;
    std::array<RamImage, 4> track2;
    track2[0] = captureCutoff(machine, "track2_a");
    if (!step(bridge, 0x0a)) return 1;
    track2[1] = captureCutoff(machine, "track2_b");
    if (!step(bridge, 0x0a)) return 1;
    track2[2] = captureCutoff(machine, "track2_c");
    if (!step(bridge, 0x0b, 2)) return 1;
    track2[3] = captureCutoff(machine, "track2_a2");
    const auto track2Candidates = reportCandidates("track2", track2);
    reportInstrumentObject(track2[0], "PIANO 241");

    if (track1Candidates.size() != 1 || track2Candidates.size() != 1 ||
        !writeCutoffWord(machine, track1Candidates[0], 100) ||
        !writeCutoffWord(machine, track2Candidates[0], 90))
        return 1;
    runFor(bridge, 2.0);
    const auto slot1Word = readRamWord(machine, track1Candidates[0]);
    const auto slot2Word = readRamWord(machine, track2Candidates[0]);
    std::cout << "direct_cutoff slot1_word=$" << std::hex << std::setw(4)
              << std::setfill('0') << slot1Word << " slot2_word=$"
              << std::setw(4) << slot2Word << std::dec << '\n';
    if (slot1Word != 0x6400 || slot2Word != 0x5a00) return 1;

    if (!click(bridge, 0x02) || !openCutoffPage(bridge, machine)) return 1;
    std::cout << "direct_slot1_display=|" << machine.display() << "|\n";
    if (machine.display().find("F1=100") == std::string::npos) return 1;
    if (!click(bridge, 0x08) || !openCutoffPage(bridge, machine)) return 1;
    std::cout << "direct_slot2_display=|" << machine.display() << "|\n";
    if (machine.display().find("F1=90") == std::string::npos) return 1;
    return machine.illegalInstructions() ? 1 : 0;
}
