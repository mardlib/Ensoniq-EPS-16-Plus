#include "EmulatorBridge.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

using namespace eps16::vst3;

struct CaptureSink final : EmulatorSink {
    struct Message { std::uint8_t value; std::uint64_t cycle; };
    struct AnalogMessage { unsigned int channel; std::uint16_t value; };
    struct KeyboardMessage {
        std::uint8_t note;
        std::uint8_t velocity;
        bool pressed;
    };
    void prepare(double rate) override { preparedRate = rate; }
    void runUntil(std::uint64_t cycle) override {
        assert(cycle >= lastRunCycle);
        lastRunCycle = cycle;
    }
    void midi(std::uint8_t status, std::uint8_t data1, std::uint8_t data2,
              std::uint64_t cycle) override {
        midiMessages.push_back({status, cycle});
        lastMidiData1 = data1;
        lastMidiData2 = data2;
    }

    void keyboard(std::uint8_t note, std::uint8_t velocity, bool pressed,
                  std::uint64_t) override {
        keyboardMessages.push_back({note, velocity, pressed});
    }

    void midiBytes(const std::uint8_t *, std::size_t, std::uint64_t) override {}

    std::size_t drainMidiOutput(std::uint8_t *, std::size_t) override {
        return 0;
    }

    void panelByte(std::uint8_t value, std::uint64_t cycle) override {
        panelMessages.push_back({value, cycle});
    }
    void analog(unsigned int channel, std::uint16_t value,
                std::uint64_t cycle) override {
        analogChannel = channel;
        analogValue = value;
        analogCycle = cycle;
        analogMessages.push_back({channel, value});
    }
    void samplingInput(float left, float right, std::uint64_t) override {
        inputSum += left + right;
        ++inputFrames;
    }
    void stereoOutput(float &left, float &right, std::uint64_t cycle) override {
        left = static_cast<float>(cycle & 1U);
        right = -left;
    }
    double preparedRate{};
    std::uint64_t lastRunCycle{};
    std::uint8_t lastMidiData1{}, lastMidiData2{};
    unsigned int analogChannel{};
    std::uint16_t analogValue{};
    std::uint64_t analogCycle{};
    double inputSum{};
    std::size_t inputFrames{};
    std::vector<Message> midiMessages;
    std::vector<Message> panelMessages;
    std::vector<AnalogMessage> analogMessages;
    std::vector<KeyboardMessage> keyboardMessages;
};

int main() {
    CaptureSink sink;
    EmulatorBridge bridge(sink);
    assert(bridge.prepare(48000.0));
    assert(sink.preparedRate == 48000.0);
    assert(bridge.enqueuePanelTransition(0x23, true));
    assert(bridge.enqueuePanelTransition(0x23, false));
    assert(bridge.enqueueAnalog(3, 128));
    assert(bridge.enqueueAnalog(3, 384));
    assert(bridge.enqueueAnalog(3, 715));
    assert(bridge.enqueueAnalog(0, 511));
    assert(bridge.enqueueAnalog(2, 0));
    assert(bridge.enqueueKeyboardTransition(36, 127, true));
    assert(bridge.enqueueKeyboardTransition(36, 1, false));
    assert(!bridge.enqueueKeyboardTransition(35, 100, true));
    assert(!bridge.enqueueKeyboardTransition(97, 100, true));
    assert(!bridge.enqueueKeyboardTransition(60, 0, true));

    constexpr int frames = 48000;
    std::array<float, frames> inputLeft{};
    std::array<float, frames> inputRight{};
    std::array<float, frames> outputLeft{};
    std::array<float, frames> outputRight{};
    inputLeft.fill(0.25f);
    inputRight.fill(-0.125f);
    const MidiEvent midi[] = {
        {0, 0x90, 60, 100}, {4800, 0xa0, 60, 50},
        {9600, 0xd0, 64, 0}, {14400, 0xb0, 1, 96},
        {19200, 0xe0, 0, 64}, {24000, 0x80, 60, 0},
        {28800, 0xf8, 0, 0}
    };
    bridge.process(inputLeft.data(), inputRight.data(), outputLeft.data(),
                   outputRight.data(), frames, midi, 7);

    assert(bridge.cpuCycles() == kCpuClockHz);
    assert(sink.lastRunCycle == kCpuClockHz);
    assert(sink.inputFrames == frames);
    assert(std::abs(sink.inputSum - 6000.0) < 0.01);
    assert(sink.panelMessages.size() == 4);
    assert(sink.panelMessages[0].value == 0xa3);
    assert(sink.panelMessages[1].value == 0x00);
    assert(sink.panelMessages[2].value == 0x23);
    assert(sink.panelMessages[3].value == 0x00);
    assert(sink.panelMessages[2].cycle >= 500000);
    assert(sink.analogChannel == 2 && sink.analogValue == 0);
    assert(sink.analogMessages.size() == 5);
    assert(sink.analogMessages[0].value == 128);
    assert(sink.analogMessages[1].value == 384);
    assert(sink.analogMessages[2].value == 715);
    assert(sink.analogMessages[3].channel == 0 &&
           sink.analogMessages[3].value == 511);
    assert(sink.analogMessages[4].channel == 2 &&
           sink.analogMessages[4].value == 0);
    assert(sink.keyboardMessages.size() == 2);
    assert(sink.keyboardMessages[0].note == 36 &&
           sink.keyboardMessages[0].velocity == 127 &&
           sink.keyboardMessages[0].pressed);
    assert(sink.keyboardMessages[1].note == 36 &&
           sink.keyboardMessages[1].velocity == 0 &&
           !sink.keyboardMessages[1].pressed);
    assert(sink.midiMessages.size() == 7);
    assert(sink.midiMessages[0].cycle == 0);
    assert(sink.midiMessages[1].value == 0xa0 &&
           sink.midiMessages[1].cycle == 1000000);
    assert(sink.midiMessages[2].value == 0xd0 &&
           sink.midiMessages[2].cycle == 2000000);
    assert(sink.midiMessages[3].value == 0xb0 &&
           sink.midiMessages[3].cycle == 3000000);
    assert(sink.midiMessages[4].value == 0xe0 &&
           sink.midiMessages[4].cycle == 4000000);
    assert(sink.midiMessages[5].value == 0x80 &&
           sink.midiMessages[5].cycle == 5000000);
    assert(sink.midiMessages[6].value == 0xf8 &&
           sink.midiMessages[6].cycle == 6000000);
    assert(sink.lastMidiData1 == 0 && sink.lastMidiData2 == 0);
    assert(bridge.droppedControls() == 0);

    DawClock clock;
    assert(clock.prepare(44100.0));
    for (int i = 0; i < 44100; ++i) clock.advanceOneSample();
    assert(clock.cycles() == kCpuClockHz);

    HostMidiClock hostClock;
    assert(hostClock.prepare(48000.0));
    std::array<MidiEvent, 64> clockEvents{};
    auto clockCount = hostClock.generate(true, true, 120.0, 0.0, 48000,
                                         clockEvents.data(),
                                         clockEvents.size());
    assert(clockCount == 49);
    assert(clockEvents[0].status == 0xfa &&
           clockEvents[0].sampleOffset == 0);
    for (std::size_t index = 1; index < clockCount; ++index) {
        assert(clockEvents[index].status == 0xf8);
        assert(clockEvents[index].sampleOffset ==
               static_cast<int>((index - 1) * 1000));
    }
    clockCount = hostClock.generate(true, false, 120.0, 2.0, 128,
                                    clockEvents.data(), clockEvents.size());
    assert(clockCount == 1 && clockEvents[0].status == 0xfc);
    clockCount = hostClock.generate(true, true, 120.0, 2.0, 1000,
                                    clockEvents.data(), clockEvents.size());
    assert(clockCount == 2 && clockEvents[0].status == 0xfb &&
           clockEvents[1].status == 0xf8);

    /* A tick that rounds to the first sample of the next audio block must be
       carried across that boundary. Recomputing the first tick independently
       for every block used to lose such ticks and made the EPS drift behind
       the DAW at ordinary non-integral tempos and buffer sizes. */
    HostMidiClock boundaryClock;
    constexpr double boundaryRate = 44100.0;
    constexpr double boundaryBpm = 123.456;
    constexpr int boundaryBlock = 128;
    constexpr std::int64_t boundarySamples =
        static_cast<std::int64_t>(boundaryRate * 60.0);
    if (!boundaryClock.prepare(boundaryRate)) return 9;
    std::int64_t clockTicks = 0;
    std::int64_t previousClockSample = -1;
    for (std::int64_t blockStart = 0; blockStart < boundarySamples;
         blockStart += boundaryBlock) {
        const int blockSamples = static_cast<int>(std::min<std::int64_t>(
            boundaryBlock, boundarySamples - blockStart));
        const double blockPpq =
            static_cast<double>(blockStart) * boundaryBpm /
            (60.0 * boundaryRate);
        const auto count = boundaryClock.generate(
            true, true, boundaryBpm, blockPpq, blockSamples,
            clockEvents.data(), clockEvents.size());
        for (std::size_t index = 0; index < count; ++index) {
            if (clockEvents[index].status != 0xf8) continue;
            const auto absoluteSample =
                blockStart + clockEvents[index].sampleOffset;
            const double idealSample =
                static_cast<double>(clockTicks) * boundaryRate * 60.0 /
                (boundaryBpm * 24.0);
            if (absoluteSample <= previousClockSample) return 10;
            if (std::abs(static_cast<double>(absoluteSample) -
                         idealSample) > 0.500001)
                return 11;
            previousClockSample = absoluteSample;
            ++clockTicks;
        }
    }
    std::int64_t expectedClockTicks = 0;
    while (std::floor(
               static_cast<double>(expectedClockTicks) * boundaryRate * 60.0 /
                   (boundaryBpm * 24.0) +
               0.5) < static_cast<double>(boundarySamples))
        ++expectedClockTicks;
    if (clockTicks != expectedClockTicks) {
        std::fprintf(stderr, "clock ticks: actual=%lld expected=%lld\n",
                     static_cast<long long>(clockTicks),
                     static_cast<long long>(expectedClockTicks));
        return 12;
    }

    CaptureSink partitionedSink;
    EmulatorBridge partitionedBridge(partitionedSink);
    assert(partitionedBridge.prepare(48000.0));
    for (int block = 0; block < 48; ++block) {
        partitionedBridge.process(inputLeft.data(), inputRight.data(),
                                  outputLeft.data(), outputRight.data(), 1000,
                                  nullptr, 0);
    }
    assert(partitionedBridge.cpuCycles() == bridge.cpuCycles());
    assert(partitionedSink.lastRunCycle == sink.lastRunCycle);
    assert(partitionedBridge.resetTimeline());
    /* A real resetTimeline() is paired with restoring/rebasing the machine
       sink. Mirror that new absolute-cycle origin in the capture sink. */
    partitionedSink.lastRunCycle = 0;
    partitionedBridge.process(inputLeft.data(), inputRight.data(),
                              outputLeft.data(), outputRight.data(), 1,
                              nullptr, 0);
    assert(partitionedBridge.cpuCycles() == 208);
    return 0;
}
