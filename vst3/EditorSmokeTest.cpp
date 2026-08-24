#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <iostream>
#include <memory>

int main(int argc, char **argv) {
    const juce::ScopedJuceInitialiser_GUI gui;
    Eps16PlusProcessor processor;
    if (argc == 6 && std::string(argv[1]) == "--verify-split-resources") {
        processor.refreshResourcePaths();
        const auto matches = [&processor](const juce::Identifier &key,
                                          const char *expected) {
            return juce::File(processor.getResourcePath(key)) ==
                   juce::File(expected);
        };
        if (!processor.getResourcePath(Eps16PlusProcessor::romPathKey).isEmpty() ||
            !matches(Eps16PlusProcessor::upperRomPathKey, argv[2]) ||
            !matches(Eps16PlusProcessor::lowerRomPathKey, argv[3]) ||
            !matches(Eps16PlusProcessor::kpcPathKey, argv[4]) ||
            !matches(Eps16PlusProcessor::osDiskPathKey, argv[5])) {
            std::cerr << "combined="
                      << processor.getResourcePath(Eps16PlusProcessor::romPathKey)
                      << "\nupper="
                      << processor.getResourcePath(Eps16PlusProcessor::upperRomPathKey)
                      << "\nlower="
                      << processor.getResourcePath(Eps16PlusProcessor::lowerRomPathKey)
                      << "\nKPC="
                      << processor.getResourcePath(Eps16PlusProcessor::kpcPathKey)
                      << "\nOS="
                      << processor.getResourcePath(Eps16PlusProcessor::osDiskPathKey)
                      << '\n';
            return 1;
        }
        processor.prepareToPlay(48000.0, 512);
        if (!processor.machineReady()) {
            std::cerr << processor.machineStatus() << '\n';
            return 1;
        }
        return 0;
    }
    if (argc == 5 && std::string(argv[1]) == "--verify-resources") {
        processor.refreshResourcePaths();
        const auto matches = [&processor](const juce::Identifier &key,
                                          const char *expected) {
            return juce::File(processor.getResourcePath(key)) ==
                   juce::File(expected);
        };
        if (!matches(Eps16PlusProcessor::romPathKey, argv[2]) ||
            !matches(Eps16PlusProcessor::kpcPathKey, argv[3]) ||
            !matches(Eps16PlusProcessor::osDiskPathKey, argv[4])) {
            std::cerr << "ROM="
                      << processor.getResourcePath(Eps16PlusProcessor::romPathKey)
                      << "\nKPC="
                      << processor.getResourcePath(Eps16PlusProcessor::kpcPathKey)
                      << "\nOS="
                      << processor.getResourcePath(Eps16PlusProcessor::osDiskPathKey)
                      << '\n';
            return 1;
        }
        return 0;
    }
    juce::TemporaryFile testRom(".bin");
    const std::uint8_t testByte = 0;
    if (!testRom.getFile().replaceWithData(&testByte, sizeof(testByte))) return 1;
    const auto testRomPath = testRom.getFile().getFullPathName();
    processor.setResourcePath(Eps16PlusProcessor::romPathKey, testRomPath);
    processor.setResourcePath(Eps16PlusProcessor::mountedDiskPathKey,
                              "/tmp/TEST-DISK.hfe");
    juce::MemoryBlock state;
    processor.getStateInformation(state);
    if (state.getSize() < 24) return 1;
    Eps16PlusProcessor restoredProcessor;
    restoredProcessor.setStateInformation(state.getData(),
                                           static_cast<int>(state.getSize()));
    if (restoredProcessor.getResourcePath(Eps16PlusProcessor::romPathKey) !=
        testRomPath)
        return 1;
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    if (!editor || editor->getWidth() != 1350 || editor->getHeight() != 285)
        return 1;
    auto *vfd = dynamic_cast<juce::Label *>(
        editor->findChildWithID("vfd-display"));
    if (!vfd) return 1;
    vfd->setText("MODE F1=3/LP F2=1/LP", juce::dontSendNotification);
    if (!editor->findChildWithID("os-disk-button") ||
        !editor->findChildWithID("new-disk-button") ||
        !editor->findChildWithID("load-disk-button") ||
        !editor->findChildWithID("save-disk-button"))
        return 1;
    auto *keyboardToggle = dynamic_cast<juce::TextButton *>(
        editor->findChildWithID("keyboard-toggle-button"));
    auto *pianoKeyboard = editor->findChildWithID("eps-piano-keyboard");
    auto *pitchWheel = dynamic_cast<juce::Slider *>(
        pianoKeyboard != nullptr
            ? pianoKeyboard->findChildWithID("eps-pitch-wheel")
            : nullptr);
    auto *modWheel = dynamic_cast<juce::Slider *>(
        pianoKeyboard != nullptr
            ? pianoKeyboard->findChildWithID("eps-mod-wheel")
            : nullptr);
    if (!keyboardToggle || !pianoKeyboard || !pitchWheel || !modWheel ||
        pianoKeyboard->isVisible())
        return 21;
    keyboardToggle->onClick();
    if (editor->getWidth() != 1350 || editor->getHeight() != 500 ||
        !pianoKeyboard->isVisible() || pianoKeyboard->getBounds().isEmpty() ||
        pitchWheel->getBounds().isEmpty() || modWheel->getBounds().isEmpty() ||
        pitchWheel->getValue() != 8192.0 || modWheel->getValue() != 0.0)
        return 22;
    pitchWheel->setValue(16383.0, juce::sendNotificationSync);
    modWheel->setValue(16383.0, juce::sendNotificationSync);
    pitchWheel->onDragEnd();
    if (pitchWheel->getValue() != 8192.0 || modWheel->getValue() != 16383.0)
        return 24;
    keyboardToggle->onClick();
    if (editor->getWidth() != 1350 || editor->getHeight() != 285 ||
        pianoKeyboard->isVisible() || pitchWheel->getValue() != 8192.0 ||
        modWheel->getValue() != 16383.0)
        return 23;
    auto *diskName = dynamic_cast<juce::Label *>(
        editor->findChildWithID("mounted-disk-name"));
    if (!diskName || diskName->getText() != "DISK: TEST-DISK.hfe" ||
        diskName->getBounds().isEmpty())
        return 1;
    auto *dataEntry = dynamic_cast<juce::Slider *>(
        editor->findChildWithID("data-entry-slider"));
    auto *volume = dynamic_cast<juce::Slider *>(
        editor->findChildWithID("volume-slider"));
    if (!dataEntry || !volume || !dataEntry->isEnabled() ||
        dataEntry->getMouseClickGrabsKeyboardFocus())
        return 1;
    bool recordEnabled = false;
    bool stopEnabled = false;
    bool playEnabled = false;
    bool playChordDocumented = false;
    int numberedShortcutTooltips = 0;
    juce::TextButton *firstPageButton = nullptr;
    juce::TextButton *commandModeButton = nullptr;
    for (int index = 0; index < editor->getNumChildComponents(); ++index) {
        auto *button = dynamic_cast<juce::TextButton *>(
            editor->getChildComponent(index));
        if (!button) continue;
        if (button->getTooltip().contains("Control+") &&
            button->getTooltip().contains("Option+"))
            ++numberedShortcutTooltips;
        if (button->getName() == "1 / ENV 1") firstPageButton = button;
        if (button->getName() == "CMD") commandModeButton = button;
        if (button->getName() == "RECORD") recordEnabled = button->isEnabled();
        if (button->getName() == "STOP / CONT") stopEnabled = button->isEnabled();
        if (button->getName() == "PLAY") {
            playEnabled = button->isEnabled();
            playChordDocumented = button->getTooltip().contains("Shift-click");
        }
    }
    if (!recordEnabled || !stopEnabled || !playEnabled ||
        !playChordDocumented || numberedShortcutTooltips != 10 ||
        !firstPageButton || !commandModeButton)
        return 1;
    dataEntry->setValue(1023, juce::sendNotificationSync);
    if (dataEntry->getValue() != 1023) return 1;
    if (!editor->keyPressed(juce::KeyPress(juce::KeyPress::upKey)) ||
        !editor->keyPressed(juce::KeyPress(juce::KeyPress::downKey)) ||
        !editor->keyPressed(juce::KeyPress(juce::KeyPress::leftKey)) ||
        !editor->keyPressed(juce::KeyPress(juce::KeyPress::rightKey)) ||
        editor->keyPressed(juce::KeyPress('A')))
        return 1;
    const juce::ModifierKeys command(juce::ModifierKeys::commandModifier);
    const juce::ModifierKeys control(juce::ModifierKeys::ctrlModifier);
    const juce::ModifierKeys option(juce::ModifierKeys::altModifier);
    const auto beforeGlow = firstPageButton->createComponentSnapshot(
        firstPageButton->getLocalBounds());
    const auto beforeCommandGlow = commandModeButton->createComponentSnapshot(
        commandModeButton->getLocalBounds());
    if (!editor->keyPressed(juce::KeyPress('1', control, '1')))
        return 25;
    const auto afterGlow = firstPageButton->createComponentSnapshot(
        firstPageButton->getLocalBounds());
    const auto afterCommandGlow = commandModeButton->createComponentSnapshot(
        commandModeButton->getLocalBounds());
    auto imageChecksum = [](const juce::Image &image) {
        std::uint64_t checksum = 0;
        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x)
                checksum = checksum * 33U + image.getPixelAt(x, y).getARGB();
        return checksum;
    };
    if (!beforeGlow.isValid() || !afterGlow.isValid() ||
        !beforeCommandGlow.isValid() || !afterCommandGlow.isValid() ||
        imageChecksum(beforeGlow) == imageChecksum(afterGlow) ||
        imageChecksum(beforeCommandGlow) == imageChecksum(afterCommandGlow))
        return 26;
    for (const auto digit : std::string("1234567890")) {
        if (!editor->keyPressed(juce::KeyPress(digit, control, digit)) ||
            !editor->keyPressed(juce::KeyPress(digit, option, digit)))
            return 25;
    }
    const juce::ModifierKeys commandShift(
        juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
    if (editor->keyPressed(juce::KeyPress('1')) ||
        editor->keyPressed(juce::KeyPress('1', command, '1')) ||
        editor->keyPressed(juce::KeyPress('A', command, 'A')) ||
        editor->keyPressed(juce::KeyPress('1', commandShift, '1')))
        return 25;
    editor->focusLost(juce::Component::focusChangedDirectly);
    dataEntry->setValue(512, juce::dontSendNotification);

    if (argc == 2) keyboardToggle->onClick();
    const auto snapshot = editor->createComponentSnapshot(editor->getLocalBounds());
    if (!snapshot.isValid() || snapshot.getWidth() != editor->getWidth() ||
        snapshot.getHeight() != editor->getHeight())
        return 1;
    if (argc == 2) {
        const juce::File outputFile(argv[1]);
        outputFile.deleteFile();
        juce::FileOutputStream output(outputFile);
        juce::PNGImageFormat png;
        if (!output.openedOk() || !png.writeImageToStream(snapshot, output))
            return 1;
    }
    return 0;
}
