#ifndef EPS16_VST3_PROBE_MACHINE_SINK_H
#define EPS16_VST3_PROBE_MACHINE_SINK_H

#include "BandlimitedResampler.h"
#include "EmulatorBridge.h"
#include "ProbeMachine.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace eps16::vst3 {

class ProbeMachineSink final : public EmulatorSink {
public:
    ProbeMachineSink();
    ~ProbeMachineSink() override;
    bool beginBlock() override;
    void endBlock() override;
    void configure(std::string romPath, std::string upperRomPath,
                   std::string lowerRomPath, std::string kpcPath,
                   std::string osDiskPath);
    bool insertDisk(const std::string &path, const std::string &label);
    bool insertScsiCd(const std::string &path);
    bool createBlankDisk();
    bool saveDisk(const std::string &path, bool hfeFormat);

    void prepare(double dawSampleRate) override;
    void runUntil(std::uint64_t absoluteCpuCycle) override;
    void midi(std::uint8_t status, std::uint8_t data1,
              std::uint8_t data2, std::uint64_t cycle) override;
    void keyboard(std::uint8_t note, std::uint8_t velocity, bool pressed,
                  std::uint64_t cycle) override;
    void midiBytes(const std::uint8_t *bytes, std::size_t size,
                   std::uint64_t cycle) override;
    std::size_t drainMidiOutput(std::uint8_t *bytes,
                                std::size_t capacity) override;
    void panelByte(std::uint8_t value, std::uint64_t cycle) override;
    void analog(unsigned int channel, std::uint16_t value,
                std::uint64_t cycle) override;
    void samplingInput(float left, float right, std::uint64_t cycle) override;
    void stereoOutput(float &left, float &right,
                      std::uint64_t cycle) override;

    [[nodiscard]] bool isReady() const {
        return ready.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::string status() const;
    [[nodiscard]] std::string display() const;
    [[nodiscard]] int cursorStart() const {
        return displayCursorStart.load(std::memory_order_relaxed);
    }
    [[nodiscard]] int cursorEnd() const {
        return displayCursorEnd.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint32_t cursorSegmentMask() const {
        return displayCursorSegmentMask.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint32_t decimalMask() const {
        return displayDecimalMask.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint16_t indicatorOn(unsigned int bank) const {
        return bank < displayIndicatorOn.size()
            ? displayIndicatorOn[bank].load(std::memory_order_relaxed) : 0;
    }
    [[nodiscard]] std::uint16_t indicatorFlash(unsigned int bank) const {
        return bank < displayIndicatorFlash.size()
            ? displayIndicatorFlash[bank].load(std::memory_order_relaxed) : 0;
    }
    [[nodiscard]] std::size_t illegalInstructions() const;
    [[nodiscard]] std::vector<std::uint8_t> captureState() const;
    [[nodiscard]] std::vector<std::uint8_t>
    captureRam(std::uint32_t address, std::size_t size) const;
    bool debugWriteRam(std::uint32_t address, const void *data,
                       std::size_t size);
    bool restoreState(const void *data, std::size_t size);

private:
    void publishDisplay();
    void discardQueuedAudio();

    std::string rom;
    std::string upperRom;
    std::string lowerRom;
    std::string kpc;
    std::string disk;
    mutable std::mutex statusMutex;
    std::string statusText{"waiting for ROM, KPC ROM and OS disk"};
    std::array<std::atomic<char>, 23> displayCharacters{};
    std::atomic<int> displayCursorStart{-1};
    std::atomic<int> displayCursorEnd{-1};
    std::atomic<std::uint32_t> displayCursorSegmentMask{};
    std::atomic<std::uint32_t> displayDecimalMask{};
    std::atomic<std::size_t> displayIllegalInstructions{};
    std::array<std::atomic<std::uint16_t>, 3> displayIndicatorOn{};
    std::array<std::atomic<std::uint16_t>, 3> displayIndicatorFlash{};
    float samplingMonitorSample{};
    std::atomic<bool> ready{};
    Eps16ProbeMachine *machine{};
    bool blockActive{};
    std::uint64_t cycleBase{};
    BandlimitedResampler resampler;
};

} // namespace eps16::vst3

#endif
