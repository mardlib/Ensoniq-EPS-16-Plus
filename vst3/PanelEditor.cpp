#include "PanelEditor.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>

namespace {
constexpr int rackWidth = 1350;
constexpr int rackHeight = 285;
constexpr int keyboardHeight = 215;
constexpr int expandedRackHeight = rackHeight + keyboardHeight;
const juce::Colour panelColour{0xff3b3d3c};
const juce::Colour buttonColour{0xff252625};
const juce::Colour displayColour{0xff55eaff};
const juce::Colour displayDimColour{0xff17353a};
const juce::Colour rackLabelColour{0xffe8e5df};
const juce::Colour accentColour{0xff9d4f82};

/* FIP 22AM5R alphanumeric cell, as labelled on the EPS-16 Plus keypad/display
   schematic: fourteen directly-driven segments SA..SN plus decimal point. */
enum VfdSegment : std::uint16_t {
    segA  = UINT16_C(1) << 0,  segB  = UINT16_C(1) << 1,
    segC  = UINT16_C(1) << 2,  segD  = UINT16_C(1) << 3,
    segE  = UINT16_C(1) << 4,  segF  = UINT16_C(1) << 5,
    segG1 = UINT16_C(1) << 6,  segG2 = UINT16_C(1) << 7,
    segH  = UINT16_C(1) << 8,  segI  = UINT16_C(1) << 9,
    segJ  = UINT16_C(1) << 10, segK  = UINT16_C(1) << 11,
    segL  = UINT16_C(1) << 12, segM  = UINT16_C(1) << 13
};

constexpr std::uint16_t segG = segG1 | segG2;

std::uint16_t vfdGlyph(juce::juce_wchar character) {
    const auto c = (juce::juce_wchar)juce::CharacterFunctions::toUpperCase(
        character);
    switch (c) {
        case '0': return segA | segB | segC | segD | segE | segF;
        case '1': return segB | segC;
        case '2': return segA | segB | segD | segE | segG;
        case '3': return segA | segB | segC | segD | segG;
        case '4': return segB | segC | segF | segG;
        case '5': return segA | segC | segD | segF | segG;
        case '6': return segA | segC | segD | segE | segF | segG;
        case '7': return segA | segB | segC;
        case '8': return segA | segB | segC | segD | segE | segF | segG;
        case '9': return segA | segB | segC | segD | segF | segG;
        case 'A': return segA | segB | segC | segE | segF | segG;
        case 'B': return segA | segB | segC | segD | segG | segI | segL;
        case 'C': return segA | segD | segE | segF;
        case 'D': return segA | segB | segC | segD | segI | segL;
        case 'E': return segA | segD | segE | segF | segG;
        case 'F': return segA | segE | segF | segG;
        case 'G': return segA | segC | segD | segE | segF | segG2;
        case 'H': return segB | segC | segE | segF | segG;
        case 'I': return segA | segD | segI | segL;
        case 'J': return segB | segC | segD | segE;
        case 'K': return segE | segF | segG1 | segJ | segK;
        case 'L': return segD | segE | segF;
        case 'M': return segB | segC | segE | segF | segH | segJ;
        case 'N': return segB | segC | segE | segF | segH | segK;
        case 'O': return segA | segB | segC | segD | segE | segF;
        case 'P': return segA | segB | segE | segF | segG;
        case 'Q': return segA | segB | segC | segD | segE | segF | segK;
        case 'R': return segA | segB | segE | segF | segG | segK;
        case 'S': return segA | segC | segD | segF | segG;
        case 'T': return segA | segI | segL;
        case 'U': return segB | segC | segD | segE | segF;
        case 'V': return segE | segF | segJ | segM;
        case 'W': return segB | segC | segE | segF | segK | segM;
        case 'X': return segH | segJ | segK | segM;
        case 'Y': return segH | segJ | segL;
        case 'Z': return segA | segD | segJ | segM;
        case '-': return segG;
        case '_': return segD;
        case '=': return segG | segD;
        case '+': return segG | segI | segL;
        case '/': return segJ | segM;
        case '\\': return segH | segK;
        case '|': return segI | segL;
        case '*': return segG | segH | segI | segJ | segK | segL | segM;
        case '[': return segA | segD | segE | segF;
        case ']': return segA | segB | segC | segD;
        case '(': return segJ | segK;
        case ')': return segH | segM;
        case '<': return segJ | segK;
        case '>': return segH | segM;
        case '\'': return segJ;
        case '"': return segF | segB;
        case '?': return segA | segB | segG2 | segL;
        default: return 0;
    }
}

juce::Path vfdSegmentPath(juce::Line<float> line, float width) {
    const auto vector = line.getEnd() - line.getStart();
    const auto length = vector.getDistanceFromOrigin();
    if (length <= 0.0f) return {};
    const auto along = vector / length;
    const juce::Point<float> across{-along.y, along.x};
    const auto halfWidth = width * 0.5f;
    const auto bevel = juce::jmin(width * 0.55f, length * 0.16f);
    const auto start = line.getStart();
    const auto end = line.getEnd();
    juce::Path path;
    path.startNewSubPath(start - across * halfWidth);
    path.lineTo(start - along * bevel);
    path.lineTo(start + across * halfWidth);
    path.lineTo(end + across * halfWidth);
    path.lineTo(end + along * bevel);
    path.lineTo(end - across * halfWidth);
    path.closeSubPath();
    return path;
}

void drawVfdElectrode(juce::Graphics &graphics, juce::Line<float> line,
                      float width, bool lit, juce::Colour colour) {
    const auto segment = vfdSegmentPath(line, width);
    if (lit) {
        graphics.setColour(colour.withAlpha(0.14f));
        graphics.strokePath(segment,
                            juce::PathStrokeType(width * 1.8f));
        graphics.setColour(colour);
    } else {
        graphics.setColour(displayDimColour.withAlpha(0.22f));
    }
    graphics.fillPath(segment);
}

void drawVfdCell(juce::Graphics &graphics, juce::Rectangle<float> cell,
                 std::uint16_t active, bool decimalPoint, bool cursor,
                 juce::Colour colour) {
    /* The FIP 22AM5R glass has a separate cursor electrode below every
       alphanumeric cell. The hardware close-up shows a real gap between it
       and the character's D segment; it is not the D segment itself. */
    cell = cell.reduced(0.45f, 0.35f);
    const float cursorWidth = juce::jmax(1.15f, cell.getWidth() * 0.105f);
    const float cursorGap = juce::jmax(1.35f, cell.getHeight() * 0.075f);
    const float cursorY = cell.getBottom() - cursorWidth * 0.5f;
    const auto glyph = cell.withBottom(cursorY - cursorGap);
    const float left = glyph.getX() + glyph.getWidth() * 0.12f;
    const float centre = glyph.getX() + glyph.getWidth() * 0.43f;
    const float right = glyph.getX() + glyph.getWidth() * 0.73f;
    const float top = glyph.getY() + 0.8f;
    const float middle = glyph.getCentreY();
    const float bottom = glyph.getBottom() - 0.6f;
    const float gap = juce::jmax(0.48f, glyph.getWidth() * 0.045f);
    const float tip = juce::jmax(0.65f, glyph.getWidth() * 0.075f);
    const std::array<juce::Line<float>, 14> lines{{
        {{left + tip, top}, {right - tip, top}},
        {{right, top + tip}, {right, middle - tip}},
        {{right, middle + tip}, {right, bottom - tip}},
        {{left + tip, bottom}, {right - tip, bottom}},
        {{left, middle + tip}, {left, bottom - tip}},
        {{left, top + tip}, {left, middle - tip}},
        {{left + tip, middle}, {centre - gap, middle}},
        {{centre + gap, middle}, {right - tip, middle}},
        {{left + tip, top + tip}, {centre - gap, middle - gap}},
        {{centre, top + tip}, {centre, middle - gap}},
        {{right - tip, top + tip}, {centre + gap, middle - gap}},
        {{centre + gap, middle + gap}, {right - tip, bottom - tip}},
        {{centre, middle + gap}, {centre, bottom - tip}},
        {{centre - gap, middle + gap}, {left + tip, bottom - tip}}
    }};
    const float coreWidth = juce::jmax(0.9f, cell.getWidth() * 0.082f);
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const auto bit = (std::uint16_t)(UINT16_C(1) << index);
        drawVfdElectrode(graphics, lines[index], coreWidth,
                         (active & bit) != 0, colour);
    }
    const float dotSize = juce::jmax(1.35f, cell.getWidth() * 0.115f);
    graphics.setColour(decimalPoint ? colour
                                    : displayDimColour.withAlpha(0.22f));
    graphics.fillEllipse(glyph.getX() + glyph.getWidth() * 0.82f,
                         bottom - dotSize * 0.4f, dotSize, dotSize);

    const juce::Line<float> cursorLine{
        {left + tip, cursorY}, {right - tip, cursorY}};
    drawVfdElectrode(graphics, cursorLine, cursorWidth, cursor, colour);
}
}

void Eps16PanelEditor::EpsFaderLookAndFeel::drawLinearSlider(
    juce::Graphics &graphics, int x, int y, int width, int height,
    float sliderPosition, float minimumSliderPosition,
    float maximumSliderPosition, juce::Slider::SliderStyle style,
    juce::Slider &slider) {
    if (style != juce::Slider::LinearVertical) {
        juce::LookAndFeel_V4::drawLinearSlider(
            graphics, x, y, width, height, sliderPosition,
            minimumSliderPosition, maximumSliderPosition, style, slider);
        return;
    }

    const float scale = juce::jmax(0.55f, (float)height / 198.0f);
    const auto originalOuter =
        juce::Rectangle<float>((float)x, (float)y, (float)width,
                               (float)height)
            .reduced(13.0f * scale, 2.0f * scale);
    auto outer = originalOuter;
    /* JUCE shortens the painting area to leave room for the thumb.  On the
       EPS the recessed fader track itself reaches the display's upper edge.
       Extend only that recess upward; its component and lower edge stay put. */
    outer.setTop(2.0f * scale);
    const float outerRadius = 3.0f * scale;

    graphics.setColour(juce::Colour{0xff444744});
    graphics.fillRoundedRectangle(outer, outerRadius);
    graphics.setColour(juce::Colour{0xff171817});
    graphics.drawRoundedRectangle(outer, outerRadius, 1.0f * scale);

    auto cavity = outer.reduced(3.0f * scale);
    graphics.setGradientFill(juce::ColourGradient{
        juce::Colour{0xff151615}, cavity.getTopLeft(),
        juce::Colour{0xff343634}, cavity.getBottomRight(), false});
    graphics.fillRoundedRectangle(cavity, 1.5f * scale);
    graphics.setColour(juce::Colour{0xff0b0c0b});
    graphics.drawRoundedRectangle(cavity, 1.5f * scale, 1.0f * scale);

    const float slotWidth = juce::jmax(5.0f * scale, cavity.getWidth() * 0.17f);
    auto slot = juce::Rectangle<float>(
        cavity.getCentreX() - slotWidth * 0.5f,
        cavity.getY() + 18.0f * scale, slotWidth,
        cavity.getHeight() - 36.0f * scale);
    graphics.setGradientFill(juce::ColourGradient{
        juce::Colour{0xff080908}, slot.getX(), slot.getCentreY(),
        juce::Colour{0xff272927}, slot.getRight(), slot.getCentreY(), false});
    graphics.fillRect(slot);
    graphics.setColour(juce::Colour{0xff050605});
    graphics.drawRect(slot, 0.9f * scale);

    const float originalCavityHeight =
        originalOuter.reduced(3.0f * scale).getHeight();
    const float thumbHeight =
        juce::jmax(33.0f * scale, originalCavityHeight * 0.205f);
    const float thumbWidth = cavity.getWidth() - 7.0f * scale;
    const float valueProportion =
        (float)slider.valueToProportionOfLength(slider.getValue());
    const float extendedSliderPosition = juce::jmap(
        valueProportion, cavity.getBottom() - thumbHeight * 0.5f,
        cavity.getY() + thumbHeight * 0.5f);
    const float thumbY = juce::jlimit(
        cavity.getY(), cavity.getBottom() - thumbHeight,
        extendedSliderPosition - thumbHeight * 0.5f);
    auto thumb = juce::Rectangle<float>(
        cavity.getCentreX() - thumbWidth * 0.5f, thumbY,
        thumbWidth, thumbHeight);

    graphics.setColour(juce::Colour{0x52000000});
    graphics.fillRoundedRectangle(
        thumb.translated(1.7f * scale, 2.2f * scale), 1.8f * scale);
    graphics.setGradientFill(juce::ColourGradient{
        juce::Colour{0xff555855}, thumb.getX(), thumb.getY(),
        juce::Colour{0xff181a18}, thumb.getRight(), thumb.getBottom(), false});
    graphics.fillRoundedRectangle(thumb, 1.6f * scale);
    graphics.setColour(juce::Colour{0xff0c0d0c});
    graphics.drawRoundedRectangle(thumb, 1.6f * scale, 1.0f * scale);

    const float smoothHeight = thumbHeight * 0.42f;
    graphics.setGradientFill(juce::ColourGradient{
        juce::Colour{0xff656865}, thumb.getX(), thumb.getY(),
        juce::Colour{0xff303230}, thumb.getX(),
        thumb.getY() + smoothHeight, false});
    graphics.fillRect(thumb.getX() + 1.2f * scale,
                      thumb.getY() + 1.2f * scale,
                      thumb.getWidth() - 2.4f * scale,
                      smoothHeight - 1.2f * scale);

    const float gripTop = thumb.getY() + smoothHeight;
    const float gripBottom = thumb.getBottom() - 2.0f * scale;
    constexpr int ridgeCount = 6;
    const float ridgeStep = (gripBottom - gripTop) / ridgeCount;
    for (int ridge = 0; ridge < ridgeCount; ++ridge) {
        const float ridgeY = gripTop + ridgeStep * (float)ridge;
        const float darkHeight = juce::jmax(1.2f * scale, ridgeStep * 0.44f);
        graphics.setColour(juce::Colour{0xff111211});
        graphics.fillRect(thumb.getX() + 1.5f * scale, ridgeY,
                          thumb.getWidth() - 3.0f * scale, darkHeight);
        graphics.setColour(juce::Colour{0xff747774});
        graphics.fillRect(thumb.getX() + 2.2f * scale, ridgeY + darkHeight,
                          thumb.getWidth() - 4.4f * scale,
                          juce::jmax(0.65f * scale, ridgeStep * 0.18f));
    }
}

void Eps16PanelEditor::VfdLabel::setCursorSegmentMask(std::uint32_t mask) {
    mask &= 0x3fffffU;
    if (cursorSegmentMask == mask) return;
    cursorSegmentMask = mask;
    repaint();
}

void Eps16PanelEditor::VfdLabel::setDecimalMask(std::uint32_t mask) {
    mask &= 0x3fffffU;
    if (decimalMask == mask) return;
    decimalMask = mask;
    repaint();
}

void Eps16PanelEditor::VfdLabel::setIndicators(
    const std::array<std::uint16_t, 3> &on,
    const std::array<std::uint16_t, 3> &flash, bool flashPhase) {
    if (indicatorOn == on && indicatorFlash == flash &&
        indicatorFlashPhase == flashPhase) return;
    indicatorOn = on;
    indicatorFlash = flash;
    indicatorFlashPhase = flashPhase;
    repaint();
}

void Eps16PanelEditor::VfdLabel::paint(juce::Graphics &graphics) {
    graphics.fillAll(findColour(juce::Label::backgroundColourId));
    const auto bounds = getLocalBounds().reduced(8, 5).toFloat();
    const auto font = getFont();
    const float indicatorHeight = bounds.getHeight() * 0.54f;
    const float rowHeight = indicatorHeight / 3.0f;

    struct Legend {
        const char *text;
        float x;
        float y;
        int bank;
        int bit;
    };
    /* The legends and positions are part of the physical VFD glass. Indices
       follow the serial hardware mapping recovered by an Ensoniq display
       sniffer and are driven only by original OS/KPC traffic. */
    static const Legend legends[] = {
        {"LOAD", 0.00f, 0.00f, 1, 15}, {"INST", 0.13f, 0.00f, 1, 14},
        {"MIDI", 0.25f, 0.00f, 1, 3}, {"SYSTEM", 0.36f, 0.00f, 1, 12},
        {"LAYER", 0.49f, 0.00f, 1, 11},
        {"ENV", 0.64f, 0.00f, 2, 15}, {"ODUB", 0.72f, 0.00f, 2, 14},
        {"REC", 0.81f, 0.00f, 2, 3}, {"PLAY", 0.88f, 0.00f, 2, 12},
        {"STOP", 0.95f, 0.00f, 2, 11},
        {"CMD", 0.00f, 1.00f, 1, 13}, {"SEQ", 0.13f, 1.00f, 1, 2},
        {"SONG", 0.25f, 1.00f, 1, 4}, {"PITCH", 0.36f, 1.00f, 1, 10},
        {"FILTER", 0.49f, 1.00f, 1, 6},
        {"AMP", 0.64f, 1.00f, 2, 13}, {"SONG", 0.72f, 1.00f, 2, 2},
        {"SEQ", 0.81f, 1.00f, 2, 4}, {"STEP", 0.88f, 1.00f, 2, 10},
        {"REP", 0.96f, 1.00f, 2, 6},
        {"EDIT", 0.00f, 2.00f, 1, 5}, {"MACRO", 0.13f, 2.00f, 2, 8},
        {"BANK", 0.27f, 2.00f, 1, 7}, {"LFO", 0.38f, 2.00f, 1, 9},
        {"WAVE", 0.47f, 2.00f, 1, 8},
        {"TRACK", 0.64f, 2.00f, 2, 5}, {"BAR", 0.76f, 2.00f, 2, 1},
        {"BEAT", 0.84f, 2.00f, 2, 7}, {"CLOCK", 0.92f, 2.00f, 2, 9}
    };
    graphics.setFont(juce::Font(juce::FontOptions("Helvetica Neue", 8.0f,
                                                  juce::Font::bold)));
    for (const auto &legend : legends) {
        const bool on = legend.bank >= 0 &&
            (indicatorOn[(std::size_t)legend.bank] &
             (UINT16_C(1) << legend.bit));
        const bool flashing = on &&
            (indicatorFlash[(std::size_t)legend.bank] &
             (UINT16_C(1) << legend.bit));
        const bool lit = on && (!flashing || indicatorFlashPhase);
        const float width = juce::jmax(
            28.0f, (float)std::strlen(legend.text) * 5.1f + 4.0f);
        const auto area = juce::Rectangle<float>(
            bounds.getX() + legend.x * (bounds.getWidth() - 28.0f),
            bounds.getY() + legend.y * rowHeight, width, rowHeight);
        if (lit) {
            graphics.setColour(displayColour.withAlpha(0.18f));
            for (int offset = 3; offset >= 1; --offset)
                graphics.drawText(legend.text, area.expanded((float)offset),
                                  juce::Justification::centredLeft, false);
            graphics.setColour(displayColour);
        } else {
            graphics.setColour(displayDimColour);
        }
        graphics.drawText(legend.text, area, juce::Justification::centredLeft,
                          false);
    }

    const auto textArea = juce::Rectangle<float>(
        bounds.getX(), bounds.getY() + indicatorHeight + 3.0f,
        bounds.getWidth(), bounds.getHeight() - indicatorHeight - 3.0f);
    const float cellHeight = juce::jmin(textArea.getHeight() - 1.0f,
                                        font.getHeight() * 1.32f);
    const float cellWidth = font.getHeight() * 0.82f;
    const float cellTop = textArea.getCentreY() - cellHeight * 0.5f;
    const auto text = getText().paddedRight(' ', 22).substring(0, 22);
    for (int index = 0; index < 22; ++index) {
        const auto segments = vfdGlyph(text[index]);
        const bool cursor =
            (cursorSegmentMask & (UINT32_C(1) << index)) != 0;
        const auto cell = juce::Rectangle<float>(
            textArea.getX() + cellWidth * (float)index, cellTop,
            cellWidth, cellHeight);
        drawVfdCell(graphics, cell, segments,
                    (decimalMask & (UINT32_C(1) << index)) != 0,
                    cursor,
                    findColour(juce::Label::textColourId));
    }
}

Eps16PanelEditor::PanelButton::PanelButton(Eps16PlusProcessor &processorToUse,
                                           juce::String label,
                                           std::uint8_t rawCode,
                                           bool mappingKnown)
    : processor(processorToUse), code(rawCode) {
    setName(label);
    setTooltip(label);
    setColour(buttonColourId, buttonColour);
    setColour(buttonOnColourId, buttonColour.brighter(0.18f));
    setColour(textColourOffId, rackLabelColour);
    setWantsKeyboardFocus(false);
    setEnabled(mappingKnown);
    if (!mappingKnown) setTooltip("Wire mapping is not verified yet");
}

void Eps16PanelEditor::PanelButton::setShiftChordCode(std::uint8_t rawCode) {
    shiftChordCode = rawCode;
}

void Eps16PanelEditor::PanelButton::startActivationGlow() {
    activationGlowStartedMs = juce::Time::getMillisecondCounterHiRes();
    repaint();
}

void Eps16PanelEditor::PanelButton::updateActivationGlow(double nowMs) {
    static constexpr double durationMs = 260.0;
    if (activationGlowStartedMs < 0.0) return;
    if (nowMs - activationGlowStartedMs >= durationMs)
        activationGlowStartedMs = -1.0;
    repaint();
}

void Eps16PanelEditor::PanelButton::paintButton(juce::Graphics &graphics,
                                                bool highlighted,
                                                bool down) {
    TextButton::paintButton(graphics, highlighted, down);
    if (activationGlowStartedMs < 0.0) return;

    static constexpr double durationMs = 260.0;
    const auto elapsed = juce::Time::getMillisecondCounterHiRes() -
                         activationGlowStartedMs;
    const auto amount = static_cast<float>(juce::jlimit(
        0.0, 1.0, 1.0 - elapsed / durationMs));
    const auto bounds = getLocalBounds().toFloat();
    graphics.setColour(displayColour.withAlpha(0.12f * amount));
    graphics.drawRoundedRectangle(bounds.reduced(1.0f), 7.0f, 5.0f);
    graphics.setColour(displayColour.withAlpha(0.28f * amount));
    graphics.drawRoundedRectangle(bounds.reduced(2.0f), 6.0f, 2.8f);
    graphics.setColour(displayColour.withAlpha(0.72f * amount));
    graphics.drawRoundedRectangle(bounds.reduced(3.0f), 5.0f, 1.2f);
}

void Eps16PanelEditor::PanelButton::triggerShortcut() {
    if (!isEnabled()) return;
    if (processor.enqueuePanelTransition(code, true)) {
        startActivationGlow();
        processor.enqueuePanelTransition(code, false);
    }
}

void Eps16PanelEditor::PanelButton::showActivationGlow() {
    startActivationGlow();
}

void Eps16PanelEditor::PanelButton::mouseDown(const juce::MouseEvent &event) {
    if (auto *parent = getParentComponent()) parent->grabKeyboardFocus();
    if (isEnabled() && !pressed) {
        if (shiftChordCode != 0xff && event.mods.isShiftDown())
            shiftChordPressed =
                processor.enqueuePanelTransition(shiftChordCode, true);
        pressed = processor.enqueuePanelTransition(code, true);
        if (pressed) startActivationGlow();
        if (!pressed && shiftChordPressed) {
            processor.enqueuePanelTransition(shiftChordCode, false);
            shiftChordPressed = false;
        }
    }
    TextButton::mouseDown(event);
}

void Eps16PanelEditor::PanelButton::releaseIfNeeded() {
    if (pressed) {
        processor.enqueuePanelTransition(code, false);
        pressed = false;
    }
    if (shiftChordPressed) {
        processor.enqueuePanelTransition(shiftChordCode, false);
        shiftChordPressed = false;
    }
}

void Eps16PanelEditor::PanelButton::mouseUp(const juce::MouseEvent &event) {
    releaseIfNeeded();
    TextButton::mouseUp(event);
}

void Eps16PanelEditor::PanelButton::mouseExit(const juce::MouseEvent &event) {
    if (!event.mods.isAnyMouseButtonDown()) releaseIfNeeded();
    TextButton::mouseExit(event);
}

Eps16PanelEditor::DiskButton::DiskButton(juce::String name,
                                         juce::String diskLabel)
    : Button(std::move(name)), label(std::move(diskLabel)) {
    setWantsKeyboardFocus(false);
}

void Eps16PanelEditor::DiskButton::paintButton(juce::Graphics &graphics,
                                               bool highlighted, bool down) {
    auto area = getLocalBounds().toFloat().reduced(1.0f);
    auto body = buttonColour;
    if (highlighted) body = body.brighter(0.10f);
    if (down) body = body.brighter(0.18f);
    if (!isEnabled()) body = body.withMultipliedAlpha(0.42f);
    graphics.setColour(body);
    graphics.fillRoundedRectangle(area, 2.5f);
    graphics.setColour(rackLabelColour.withMultipliedAlpha(
        isEnabled() ? 0.90f : 0.35f));
    graphics.drawRoundedRectangle(area, 2.5f, 1.0f);

    const auto shutter = juce::Rectangle<float>(
        area.getX() + area.getWidth() * 0.22f,
        area.getY() + area.getHeight() * 0.08f,
        area.getWidth() * 0.56f, area.getHeight() * 0.27f);
    graphics.setColour(juce::Colour(0xff777b7a).withMultipliedAlpha(
        isEnabled() ? 1.0f : 0.4f));
    graphics.fillRect(shutter);
    graphics.setColour(juce::Colour(0xff1b1c1b));
    graphics.fillRect(shutter.getRight() - shutter.getWidth() * 0.22f,
                      shutter.getY(), shutter.getWidth() * 0.12f,
                      shutter.getHeight());

    const auto labelArea = juce::Rectangle<float>(
        area.getX() + area.getWidth() * 0.11f,
        area.getY() + area.getHeight() * 0.50f,
        area.getWidth() * 0.78f, area.getHeight() * 0.34f);
    graphics.setColour(juce::Colour(0xffdedbd1).withMultipliedAlpha(
        isEnabled() ? 1.0f : 0.4f));
    graphics.fillRoundedRectangle(labelArea, 1.0f);
    graphics.setColour(juce::Colour(0xff252625).withMultipliedAlpha(
        isEnabled() ? 1.0f : 0.45f));
    graphics.setFont(juce::Font(juce::FontOptions(
        "Helvetica Neue", juce::jmax(5.5f, labelArea.getHeight() * 0.54f),
        juce::Font::bold)));
    graphics.drawText(label, labelArea, juce::Justification::centred, false);
}

Eps16PanelEditor::PianoKeyboard::PianoKeyboard(
    Eps16PlusProcessor &processorToUse)
    : processor(processorToUse) {
    setComponentID("eps-piano-keyboard");
    setMouseClickGrabsKeyboardFocus(false);

    auto configureWheel = [this](juce::Slider &wheel,
                                 const juce::String &componentID) {
        wheel.setComponentID(componentID);
        wheel.setSliderStyle(juce::Slider::LinearVertical);
        wheel.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        wheel.setRange(0.0, 16383.0, 1.0);
        wheel.setMouseDragSensitivity(180);
        wheel.setScrollWheelEnabled(false);
        wheel.setMouseClickGrabsKeyboardFocus(false);
        addAndMakeVisible(wheel);
    };
    configureWheel(pitchWheel, "eps-pitch-wheel");
    configureWheel(modWheel, "eps-mod-wheel");
    pitchWheel.setTooltip("Pitch wheel - returns to centre when released");
    modWheel.setTooltip("Modulation wheel");
    pitchWheel.setValue(8192.0, juce::dontSendNotification);
    modWheel.setValue(0.0, juce::dontSendNotification);
    pitchWheel.onValueChange = [this] {
        processor.enqueueAnalog(0, wheelToAnalog(pitchWheel.getValue()));
    };
    pitchWheel.onDragEnd = [this] {
        pitchWheel.setValue(8192.0, juce::sendNotificationSync);
    };
    modWheel.onValueChange = [this] {
        processor.enqueueAnalog(2, wheelToAnalog(modWheel.getValue()));
    };
}

void Eps16PanelEditor::PianoKeyboard::PerformanceWheel::paint(
    juce::Graphics &graphics) {
    auto housing = getLocalBounds().toFloat().reduced(2.0f);
    housing = housing.withSizeKeepingCentre(housing.getWidth(), 142.0f);
    graphics.setColour(juce::Colour{0xff101110});
    graphics.fillRoundedRectangle(housing, 2.0f);
    graphics.setColour(juce::Colour{0xff555956});
    graphics.drawRoundedRectangle(housing, 2.0f, 1.0f);

    const auto range = getMaximum() - getMinimum();
    const auto position = range > 0.0
        ? static_cast<float>((getValue() - getMinimum()) / range)
        : 0.0f;
    const auto wheel = housing.reduced(5.0f, 7.0f);
    juce::ColourGradient wheelShade(
        juce::Colour{0xff292b29}, wheel.getX(), wheel.getY(),
        juce::Colour{0xff292b29}, wheel.getX(), wheel.getBottom(), false);
    wheelShade.addColour(0.5, juce::Colour{0xff555955});
    graphics.setGradientFill(wheelShade);
    graphics.fillRoundedRectangle(wheel, wheel.getWidth() * 0.45f);
    graphics.setColour(juce::Colour{0xff777c78});
    graphics.drawRoundedRectangle(wheel, wheel.getWidth() * 0.45f, 1.0f);

    constexpr float gripHalfHeight = 10.0f;
    const float visibleTravel =
        wheel.getHeight() - gripHalfHeight * 2.0f - 12.0f;
    const float gripY = wheel.getCentreY() -
                        (position - 0.5f) * visibleTravel;
    graphics.saveState();
    graphics.reduceClipRegion(wheel.toNearestInt());
    graphics.setColour(juce::Colour{0xff252725});
    for (float grooveY = wheel.getY() + 5.0f;
         grooveY < wheel.getBottom() - 4.0f; grooveY += 3.0f) {
        if (std::abs(grooveY - gripY) > gripHalfHeight)
            graphics.drawHorizontalLine(juce::roundToInt(grooveY),
                                        wheel.getX() + 1.5f,
                                        wheel.getRight() - 1.5f);
    }
    auto grip = juce::Rectangle<float>(
        wheel.getX(), gripY - gripHalfHeight,
        wheel.getWidth(), gripHalfHeight * 2.0f);
    juce::ColourGradient gripShade(
        juce::Colour{0xff303230}, grip.getX(), grip.getY(),
        juce::Colour{0xff171817}, grip.getX(), grip.getBottom(), false);
    graphics.setGradientFill(gripShade);
    graphics.fillRect(grip);
    graphics.setColour(juce::Colour{0xff696d69}.withAlpha(0.55f));
    graphics.drawHorizontalLine(juce::roundToInt(grip.getY()),
                                grip.getX() + 1.0f,
                                grip.getRight() - 1.0f);
    graphics.restoreState();
}

Eps16PanelEditor::PianoKeyboard::~PianoKeyboard() {
    releaseAllNotes();
    releasePerformanceControls();
}

bool Eps16PanelEditor::PianoKeyboard::isBlackKey(int note) {
    const int pitch = note % 12;
    return pitch == 1 || pitch == 3 || pitch == 6 || pitch == 8 || pitch == 10;
}

std::uint16_t Eps16PanelEditor::PianoKeyboard::wheelToAnalog(double value) {
    const auto conventional = juce::jlimit(0.0, 16383.0, value);
    return static_cast<std::uint16_t>(juce::roundToInt(
        1023.0 - conventional * 1023.0 / 16383.0));
}

juce::Rectangle<float> Eps16PanelEditor::PianoKeyboard::keyboardArea() const {
    auto area = getLocalBounds().toFloat().reduced(8.0f, 7.0f);
    area.removeFromLeft(82.0f);
    return area;
}

juce::Rectangle<float> Eps16PanelEditor::PianoKeyboard::keyBounds(
    int note) const {
    const auto area = keyboardArea();
    constexpr int firstNote = 36;
    constexpr int whiteKeyCount = 36;
    const float whiteWidth = area.getWidth() / (float)whiteKeyCount;
    int whiteBefore = 0;
    for (int candidate = firstNote; candidate < note; ++candidate)
        if (!isBlackKey(candidate)) ++whiteBefore;
    if (!isBlackKey(note))
        return {area.getX() + static_cast<float>(whiteBefore) * whiteWidth,
                area.getY(),
                whiteWidth, area.getHeight()};
    const float blackWidth = whiteWidth * 0.61f;
    return {area.getX() + static_cast<float>(whiteBefore) * whiteWidth -
                            blackWidth * 0.5f,
            area.getY(), blackWidth, area.getHeight() * 0.62f};
}

int Eps16PanelEditor::PianoKeyboard::noteAt(
    juce::Point<float> position) const {
    for (int note = 36; note <= 96; ++note)
        if (isBlackKey(note) && keyBounds(note).contains(position)) return note;
    for (int note = 36; note <= 96; ++note)
        if (!isBlackKey(note) && keyBounds(note).contains(position)) return note;
    return -1;
}

std::uint8_t Eps16PanelEditor::PianoKeyboard::velocityAt(int note,
                                                         float y) const {
    const auto key = keyBounds(note);
    const float position = juce::jlimit(0.0f, 1.0f,
                                        (y - key.getY()) / key.getHeight());
    return static_cast<std::uint8_t>(juce::jlimit(
        1, 127, 127 - juce::roundToInt(position * 126.0f)));
}

void Eps16PanelEditor::PianoKeyboard::pressAt(
    juce::Point<float> position) {
    const int note = noteAt(position);
    if (note == activeNote) return;
    releaseActiveNote();
    if (note < 0) return;
    const auto velocity = velocityAt(note, position.y);
    if (processor.enqueueKeyboardTransition(
            static_cast<std::uint8_t>(note), velocity, true)) {
        activeNote = note;
        repaint();
    }
}

void Eps16PanelEditor::PianoKeyboard::releaseActiveNote() {
    if (activeNote < 0) return;
    processor.enqueueKeyboardTransition(static_cast<std::uint8_t>(activeNote),
                                        1, false);
    activeNote = -1;
    repaint();
}

void Eps16PanelEditor::PianoKeyboard::releaseAllNotes() {
    releaseActiveNote();
}

void Eps16PanelEditor::PianoKeyboard::releasePerformanceControls() {
    if (pitchWheel.getValue() != 8192.0)
        pitchWheel.setValue(8192.0, juce::sendNotificationSync);
}

void Eps16PanelEditor::PianoKeyboard::resized() {
    auto area = getLocalBounds().reduced(8, 7);
    auto wheelArea = area.removeFromLeft(74);
    wheelArea.removeFromTop(13);
    wheelArea.removeFromBottom(22);
    pitchWheel.setBounds(wheelArea.removeFromLeft(32).reduced(3, 0));
    wheelArea.removeFromLeft(6);
    modWheel.setBounds(wheelArea.removeFromLeft(32).reduced(3, 0));
}

void Eps16PanelEditor::PianoKeyboard::mouseDown(
    const juce::MouseEvent &event) {
    pressAt(event.position);
}

void Eps16PanelEditor::PianoKeyboard::mouseDrag(
    const juce::MouseEvent &event) {
    if (event.mods.isLeftButtonDown()) pressAt(event.position);
}

void Eps16PanelEditor::PianoKeyboard::mouseUp(const juce::MouseEvent &) {
    releaseActiveNote();
}

void Eps16PanelEditor::PianoKeyboard::mouseExit(const juce::MouseEvent &) {
    releaseActiveNote();
}

void Eps16PanelEditor::PianoKeyboard::paint(juce::Graphics &graphics) {
    graphics.fillAll(juce::Colour{0xff242625});
    graphics.setColour(juce::Colour{0xff171817});
    graphics.drawRect(getLocalBounds(), 1);
    graphics.setColour(rackLabelColour.withAlpha(0.78f));
    graphics.setFont(juce::FontOptions(9.0f));
    graphics.drawText("PITCH", 8, getHeight() - 27, 32, 14,
                      juce::Justification::centred);
    graphics.drawText("MOD", 46, getHeight() - 27, 28, 14,
                      juce::Justification::centred);
    graphics.setColour(rackLabelColour.withAlpha(0.32f));
    graphics.drawHorizontalLine(getHeight() / 2, 11.0f, 37.0f);
    for (int note = 36; note <= 96; ++note) {
        if (isBlackKey(note)) continue;
        const auto key = keyBounds(note);
        graphics.setColour(note == activeNote ? juce::Colour{0xff78dce8}
                                              : juce::Colour{0xffd9d7cf});
        graphics.fillRect(key);
        graphics.setColour(juce::Colour{0xff2b2c2b});
        graphics.drawRect(key, 1.0f);
    }
    for (int note = 36; note <= 96; ++note) {
        if (!isBlackKey(note)) continue;
        const auto key = keyBounds(note);
        graphics.setColour(note == activeNote ? juce::Colour{0xff2f8992}
                                              : juce::Colour{0xff171817});
        graphics.fillRect(key);
        graphics.setColour(juce::Colour{0xff050606});
        graphics.drawRect(key, 1.0f);
    }
    graphics.setColour(rackLabelColour.withAlpha(0.62f));
    graphics.setFont(10.0f);
    auto labelArea = keyboardArea().toNearestInt();
    labelArea.removeFromTop(labelArea.getHeight() - 18);
    graphics.drawText("VELOCITY  127 AT TOP  •  1 AT BOTTOM",
                      labelArea.reduced(10, 0),
                      juce::Justification::centredRight);
}

Eps16PanelEditor::Eps16PanelEditor(Eps16PlusProcessor &processorToUse)
    : AudioProcessorEditor(processorToUse), owner(processorToUse),
      pianoKeyboard(processorToUse) {
    setSize(rackWidth, rackHeight);
    setResizable(true, true);
    setResizeLimits(1080, 228, 1620, 342);
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(true);
    if (auto *constrainer = getConstrainer())
        constrainer->setFixedAspectRatio((double)rackWidth / rackHeight);

    vfd.setText(juce::String::repeatedString(" ", 22), juce::dontSendNotification);
    vfd.setJustificationType(juce::Justification::centredLeft);
    vfd.setComponentID("vfd-display");
    vfd.setFont(juce::Font(juce::FontOptions("Menlo", 20.0f,
                                             juce::Font::plain)));
    vfd.setColour(juce::Label::backgroundColourId, juce::Colours::black);
    vfd.setColour(juce::Label::textColourId, displayColour);
    addAndMakeVisible(vfd);

    status.setText("Plug-in adapter active - waiting for authentic emulator boot",
                   juce::dontSendNotification);
    status.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addChildComponent(status);

    osDiskButton.setComponentID("os-disk-button");
    osDiskButton.setEnabled(false);
    osDiskButton.onClick = [this] { owner.insertOsDisk(); };
    addAndMakeVisible(osDiskButton);

    newDiskButton.setComponentID("new-disk-button");
    newDiskButton.setEnabled(false);
    newDiskButton.onClick = [this] {
        auto safeEditor = juce::Component::SafePointer<Eps16PanelEditor>(this);
        juce::NativeMessageBox::showOkCancelBox(
            juce::MessageBoxIconType::QuestionIcon,
            "New blank EPS disk",
            "Eject the current disk and insert a new blank EPS disk?\n\n"
            "Unsaved disk changes will be lost.",
            this,
            juce::ModalCallbackFunction::create([safeEditor](int result) {
                if (result != 1) return;
                if (auto *editor = safeEditor.getComponent())
                    editor->owner.createBlankDisk();
            }));
    };
    addAndMakeVisible(newDiskButton);

    loadDiskButton.setComponentID("load-disk-button");
    loadDiskButton.setEnabled(false);
    loadDiskButton.onClick = [this] {
        juce::File initialFile(owner.getResourcePath(
            Eps16PlusProcessor::mountedDiskPathKey));
        if (!initialFile.existsAsFile())
            initialFile = juce::File(owner.getResourcePath(
                Eps16PlusProcessor::osDiskPathKey));
        const auto initialDirectory = initialFile.existsAsFile()
            ? initialFile.getParentDirectory()
            : Eps16PlusProcessor::defaultResourceDirectory();
        diskChooser = std::make_unique<juce::FileChooser>(
            "Load an EPS file (.EFE, .IMG, .HFE or .ISO)", initialDirectory, "*");
        auto safeEditor = juce::Component::SafePointer<Eps16PanelEditor>(this);
        diskChooser->launchAsync(
            juce::FileBrowserComponent::openMode |
                juce::FileBrowserComponent::canSelectFiles,
            [safeEditor](const juce::FileChooser &chooser) {
                if (auto *editor = safeEditor.getComponent()) {
                    const auto file = chooser.getResult();
                    if (!file.existsAsFile()) return;
                    const auto extension = file.getFileExtension().toLowerCase();
                    if (extension == ".efe" || extension == ".img" ||
                        extension == ".hfe" || extension == ".iso") {
                        editor->owner.insertDisk(file);
                    } else {
                        juce::NativeMessageBox::showMessageBoxAsync(
                            juce::MessageBoxIconType::WarningIcon,
                            "Unsupported EPS file",
                            "Please choose an EPS .EFE, .IMG, .HFE or .ISO file.",
                            editor);
                    }
                }
            });
    };
    addAndMakeVisible(loadDiskButton);

    saveDiskButton.setComponentID("save-disk-button");
    saveDiskButton.setEnabled(false);
    saveDiskButton.onClick = [this] {
        auto safeEditor = juce::Component::SafePointer<Eps16PanelEditor>(this);
        juce::PopupMenu formats;
        formats.addItem(1, "Save as IMG...");
        formats.addItem(2, "Save as HFE...");
        formats.showMenuAsync(
            juce::PopupMenu::Options().withTargetComponent(&saveDiskButton),
            [safeEditor](int choice) {
                if (auto *editor = safeEditor.getComponent()) {
                    if (choice == 1) editor->openSaveDiskDialog(false);
                    if (choice == 2) editor->openSaveDiskDialog(true);
                }
            });
    };
    addAndMakeVisible(saveDiskButton);

    diskName.setComponentID("mounted-disk-name");
    diskName.setJustificationType(juce::Justification::centredLeft);
    diskName.setMinimumHorizontalScale(0.65f);
    diskName.setColour(juce::Label::textColourId,
                       rackLabelColour.withAlpha(0.86f));
    diskName.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(diskName);
    updateDiskName();

    keyboardToggle.setComponentID("keyboard-toggle-button");
    keyboardToggle.setButtonText("KEYBOARD  +");
    keyboardToggle.setMouseClickGrabsKeyboardFocus(false);
    keyboardToggle.setColour(juce::TextButton::buttonColourId, buttonColour);
    keyboardToggle.setColour(juce::TextButton::buttonOnColourId,
                             buttonColour.brighter(0.12f));
    keyboardToggle.setColour(juce::TextButton::textColourOffId,
                             rackLabelColour);
    keyboardToggle.setTooltip("Show or hide the 61-key EPS keyboard");
    keyboardToggle.onClick = [this] {
        setKeyboardExpanded(!keyboardExpanded);
    };
    addAndMakeVisible(keyboardToggle);
    pianoKeyboard.setVisible(false);
    addChildComponent(pianoKeyboard);

    const std::array<std::pair<const char *, std::uint8_t>, 12> pages{{
        {"1 / ENV 1", 0x0d}, {"2 / ENV 2", 0x12}, {"3 / ENV 3", 0x13},
        {"4 / PITCH", 0x18}, {"5 / FILTER", 0x19}, {"6 / AMP", 0x1e},
        {"7 / LFO", 0x1f}, {"8 / WAVE", 0x24}, {"9 / LAYER", 0x25},
        {"SAMPLE", 0x20}, {"0 / TRACK", 0x0c}, {"EFFECT SELECT / BYPASS", 0x07}
    }};
    for (std::size_t index = 0; index < pages.size(); ++index)
        pageButtons[index] = &addPanelButton(pages[index].first,
                                             pages[index].second);
    for (std::size_t index = 0; index < 10; ++index) {
        const auto pageIndex = index == 9 ? 10U : index;
        const auto digit = juce::String(index == 9 ? 0 : (int)index + 1);
        pageButtons[pageIndex]->setTooltip(
            pageButtons[pageIndex]->getName() + " (Control+" + digit +
            " opens CMD; Option+" + digit + " opens EDIT)");
    }

    const std::array<std::pair<const char *, std::uint8_t>, 7> modes{{
        {"LOAD", 0x1a}, {"CMD", 0x06}, {"EDIT", 0x05},
        {"INST", 0x0f}, {"SEQ SONG", 0x15}, {"SYSTEM MIDI", 0x1b},
        {"EFFECTS", 0x09}
    }};
    for (std::size_t index = 0; index < modes.size(); ++index)
        modeButtons[index] = &addPanelButton(modes[index].first,
                                             modes[index].second);

    const std::array<std::uint8_t, 8> tracks{
        0x02, 0x08, 0x0e, 0x14, 0x04, 0x22, 0x1c, 0x16
    };
    for (std::size_t index = 0; index < tracks.size(); ++index)
        trackButtons[index] = &addPanelButton(
            "INSTRUMENT / TRACK " + juce::String((int)index + 1), tracks[index]);

    upButton = &addPanelButton("UP", 0x0a);
    downButton = &addPanelButton("DOWN", 0x0b);
    leftButton = &addPanelButton("LEFT", 0x10);
    rightButton = &addPanelButton("RIGHT", 0x11);
    cancelButton = &addPanelButton("NO / CANCEL", 0x21);
    enterButton = &addPanelButton("YES / ENTER", 0x23);
    sequencerButtons[0] = &addPanelButton("RECORD", 0x03);
    sequencerButtons[1] = &addPanelButton("STOP / CONT", 0x17);
    sequencerButtons[2] = &addPanelButton("PLAY", 0x1d);
    sequencerButtons[2]->setShiftChordCode(0x03);
    sequencerButtons[2]->setTooltip("PLAY (Shift-click: RECORD + PLAY)");

    auto configureFader = [this](juce::Slider &slider, const juce::String &name) {
        slider.setName(name);
        slider.setComponentID(name == "DATA ENTRY" ? "data-entry-slider"
                                                    : "volume-slider");
        slider.setSliderStyle(juce::Slider::LinearVertical);
        slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        /* Keep ordinary Slider mouse drag/wheel handling. A click must not
           steal the editor focus because the physical arrow keys belong to
           the panel rather than to the JUCE slider. */
        slider.setMouseClickGrabsKeyboardFocus(false);
        slider.setLookAndFeel(&faderLookAndFeel);
        slider.setRange(0, 1023, 1);
        addAndMakeVisible(slider);
    };
    configureFader(masterVolume, "VOLUME");
    configureFader(dataEntry, "DATA ENTRY");
    masterVolume.setValue(1023, juce::dontSendNotification);
    dataEntry.setValue(512, juce::dontSendNotification);
    masterVolume.onValueChange = [this] {
        this->owner.enqueueAnalog(5,
            static_cast<std::uint16_t>(masterVolume.getValue()));
    };
    dataEntry.onValueChange = [this] {
        const auto gui = static_cast<unsigned int>(dataEntry.getValue());
        this->owner.enqueueAnalog(3,
            static_cast<std::uint16_t>((gui * 715U) / 1023U));
    };

    /* setSize() runs resized() near the start of this constructor, before the
       dynamically-created panel buttons exist. Lay out once more after every
       child has been added so hosts that keep the initial size do not leave
       the entire button matrix at its default zero bounds. */
    resized();
    startTimerHz(30);
}

Eps16PanelEditor::~Eps16PanelEditor() {
    releaseArrowKeys();
    pianoKeyboard.releaseAllNotes();
    masterVolume.setLookAndFeel(nullptr);
    dataEntry.setLookAndFeel(nullptr);
}

void Eps16PanelEditor::openSaveDiskDialog(bool hfeFormat) {
    juce::File mounted(owner.getResourcePath(
        Eps16PlusProcessor::mountedDiskPathKey));
    const bool isBlankDisk = owner.blankDiskMounted();
    if (!isBlankDisk && !mounted.existsAsFile())
        mounted = juce::File(owner.getResourcePath(
            Eps16PlusProcessor::osDiskPathKey));
    const juce::String extension = hfeFormat ? ".hfe" : ".img";
    auto suggested = !isBlankDisk && mounted.existsAsFile()
        ? mounted.getSiblingFile(mounted.getFileNameWithoutExtension() +
                                 "-saved" + extension)
        : Eps16PlusProcessor::defaultResourceDirectory()
              .getChildFile(isBlankDisk ? "NEWDISK" + extension
                                        : "EPS-disk-saved" + extension);
    diskChooser = std::make_unique<juce::FileChooser>(
        hfeFormat ? "Save EPS disk as HFE" : "Save EPS disk as IMG",
        suggested, "*");
    auto safeEditor = juce::Component::SafePointer<Eps16PanelEditor>(this);
    diskChooser->launchAsync(
        juce::FileBrowserComponent::saveMode |
            juce::FileBrowserComponent::canSelectFiles |
            juce::FileBrowserComponent::warnAboutOverwriting,
        [safeEditor, hfeFormat](const juce::FileChooser &chooser) {
            if (auto *editor = safeEditor.getComponent()) {
                auto file = chooser.getResult();
                if (file.getFullPathName().isEmpty()) return;
                file = file.withFileExtension(hfeFormat ? ".hfe" : ".img");
                editor->owner.saveDisk(file);
            }
        });
}

Eps16PanelEditor::PanelButton &Eps16PanelEditor::addPanelButton(
    const juce::String &label, std::uint8_t code, bool known) {
    auto button = std::make_unique<PanelButton>(owner, label, code, known);
    auto &reference = *button;
    addAndMakeVisible(reference);
    buttons.push_back(std::move(button));
    return reference;
}

bool Eps16PanelEditor::updateArrowKey(int keyCode, bool isDown) {
    static const std::array<int, 4> keyCodes{
        juce::KeyPress::upKey, juce::KeyPress::downKey,
        juce::KeyPress::leftKey, juce::KeyPress::rightKey
    };
    static constexpr std::array<std::uint8_t, 4> panelCodes{
        0x0a, 0x0b, 0x10, 0x11
    };
    const std::array<PanelButton *, 4> arrowButtons{
        upButton, downButton, leftButton, rightButton
    };
    for (std::size_t index = 0; index < keyCodes.size(); ++index) {
        if (keyCode != keyCodes[index]) continue;
        if (arrowKeysDown[index] == isDown) return true;
        if (isDown) {
            arrowKeysDown[index] =
                owner.enqueuePanelTransition(panelCodes[index], true);
            if (arrowKeysDown[index])
                arrowButtons[index]->showActivationGlow();
        } else {
            owner.enqueuePanelTransition(panelCodes[index], false);
            arrowKeysDown[index] = false;
        }
        return true;
    }
    return false;
}

bool Eps16PanelEditor::keyPressed(const juce::KeyPress &key) {
    const auto modifiers = key.getModifiers();
    const bool noAdditionalModifiers = !modifiers.isShiftDown();
    const bool controlShortcut = modifiers.isCtrlDown() &&
        !modifiers.isCommandDown() && !modifiers.isAltDown() &&
        noAdditionalModifiers;
    const bool editShortcut = modifiers.isAltDown() &&
        !modifiers.isCommandDown() && !modifiers.isCtrlDown() &&
        noAdditionalModifiers;
    const auto keyCode = key.getKeyCode();
    if ((controlShortcut || editShortcut) &&
        keyCode >= '0' && keyCode <= '9') {
        const auto pageIndex = keyCode == '0'
            ? std::size_t{10} : static_cast<std::size_t>(keyCode - '1');
        if (controlShortcut) modeButtons[1]->triggerShortcut();
        if (editShortcut) modeButtons[2]->triggerShortcut();
        pageButtons[pageIndex]->triggerShortcut();
        return true;
    }
    return updateArrowKey(key.getKeyCode(), true);
}

bool Eps16PanelEditor::keyStateChanged(bool) {
    const bool hadArrowDown = std::any_of(arrowKeysDown.begin(),
                                          arrowKeysDown.end(),
                                          [](bool down) { return down; });
    static const std::array<int, 4> keyCodes{
        juce::KeyPress::upKey, juce::KeyPress::downKey,
        juce::KeyPress::leftKey, juce::KeyPress::rightKey
    };
    for (const auto keyCode : keyCodes)
        updateArrowKey(keyCode, juce::KeyPress::isKeyCurrentlyDown(keyCode));
    const bool hasArrowDown = std::any_of(arrowKeysDown.begin(),
                                          arrowKeysDown.end(),
                                          [](bool down) { return down; });
    return hadArrowDown || hasArrowDown;
}

void Eps16PanelEditor::releaseArrowKeys() {
    static const std::array<int, 4> keyCodes{
        juce::KeyPress::upKey, juce::KeyPress::downKey,
        juce::KeyPress::leftKey, juce::KeyPress::rightKey
    };
    for (const auto keyCode : keyCodes) updateArrowKey(keyCode, false);
}

void Eps16PanelEditor::focusLost(FocusChangeType cause) {
    releaseArrowKeys();
    pianoKeyboard.releaseAllNotes();
    pianoKeyboard.releasePerformanceControls();
    AudioProcessorEditor::focusLost(cause);
}

void Eps16PanelEditor::updateDiskName() {
    juce::String name;
    juce::String tooltip;
    if (owner.blankDiskMounted()) {
        name = "NEWDISK (UNSAVED)";
        tooltip = "New blank EPS disk (not saved to a host file)";
    } else {
        const auto mountedPath = owner.getResourcePath(
            Eps16PlusProcessor::mountedDiskPathKey);
        if (mountedPath.isNotEmpty()) {
            const juce::File mounted(mountedPath);
            name = mounted.getFileName();
            tooltip = mounted.getFullPathName();
        } else {
            const juce::File osDisk(owner.getResourcePath(
                Eps16PlusProcessor::osDiskPathKey));
            if (owner.machineReady() && osDisk.existsAsFile()) {
                name = osDisk.getFileName();
                tooltip = osDisk.getFullPathName();
            }
        }
    }
    diskName.setText("DISK: " + (name.isNotEmpty() ? name : "NONE"),
                     juce::dontSendNotification);
    diskName.setTooltip(tooltip);
}

void Eps16PanelEditor::setKeyboardExpanded(bool expanded) {
    if (keyboardExpanded == expanded) return;
    pianoKeyboard.releaseAllNotes();
    pianoKeyboard.releasePerformanceControls();
    keyboardExpanded = expanded;
    pianoKeyboard.setVisible(expanded);
    keyboardToggle.setButtonText(expanded ? "KEYBOARD  -" : "KEYBOARD  +");
    const int designHeight = expanded ? expandedRackHeight : rackHeight;
    const int targetWidth = getWidth();
    const int targetHeight = juce::roundToInt(
        (double)targetWidth * designHeight / rackWidth);
    if (auto *constrainer = getConstrainer())
        constrainer->setFixedAspectRatio(0.0);
    setResizeLimits(1080, juce::roundToInt(1080.0 * designHeight / rackWidth),
                    1620, juce::roundToInt(1620.0 * designHeight / rackWidth));
    setSize(targetWidth, targetHeight);
    if (auto *constrainer = getConstrainer())
        constrainer->setFixedAspectRatio((double)rackWidth / designHeight);
    setSize(targetWidth, targetHeight);
    resized();
    repaint();
}

void Eps16PanelEditor::timerCallback() {
    /* VFD motion needs display-rate polling, especially for the original
       Level-Detect meter. Resource discovery hashes split ROMs, so retain
       the previous low-rate cadence for file/status work. */
    const bool slowUpdate = (timerTicks++ % 8U) == 0;
    const auto nowMs = juce::Time::getMillisecondCounterHiRes();
    for (auto &button : buttons) button->updateActivationGlow(nowMs);
    if (slowUpdate) {
        owner.refreshResourcePaths();
        const juce::File osDisk(owner.getResourcePath(
            Eps16PlusProcessor::osDiskPathKey));
        osDiskButton.setEnabled(owner.machineReady() && osDisk.existsAsFile());
        osDiskButton.setTooltip(osDisk.existsAsFile()
            ? "Insert OS disk: " + osDisk.getFileName()
            : "OS disk not found in EPS_files");
        newDiskButton.setEnabled(owner.machineReady());
        newDiskButton.setTooltip("Insert a new blank formatted EPS disk");
        loadDiskButton.setEnabled(owner.machineReady());
        loadDiskButton.setTooltip(
            "Load an EPS .EFE file or insert an .IMG/.HFE/.ISO image");
        saveDiskButton.setEnabled(owner.machineReady());
        saveDiskButton.setTooltip("Save the inserted disk as .IMG or .HFE");
        updateDiskName();
    }
    vfd.setText(owner.machineDisplay(), juce::dontSendNotification);
    vfd.setCursorSegmentMask(owner.machineCursorSegmentMask());
    vfd.setDecimalMask(owner.machineDecimalMask());
    std::array<std::uint16_t, 3> indicatorOn{};
    std::array<std::uint16_t, 3> indicatorFlash{};
    for (unsigned int bank = 0; bank < indicatorOn.size(); ++bank) {
        indicatorOn[bank] = owner.machineIndicatorOn(bank);
        indicatorFlash[bank] = owner.machineIndicatorFlash(bank);
    }
    vfd.setIndicators(indicatorOn, indicatorFlash,
                      ((owner.cpuCycles() / 2500000U) & 1U) != 0);
    const auto nextTrackOn = owner.machineIndicatorOn(0);
    const auto nextTrackFlash = owner.machineIndicatorFlash(0);
    const bool nextTrackPhase = ((owner.cpuCycles() / 2500000U) & 1U) != 0;
    if (trackLedOn != nextTrackOn || trackLedFlash != nextTrackFlash ||
        trackLedFlashPhase != nextTrackPhase) {
        trackLedOn = nextTrackOn;
        trackLedFlash = nextTrackFlash;
        trackLedFlashPhase = nextTrackPhase;
        repaint();
    }
    if (slowUpdate)
        status.setText(
            "DAW-driven CPU cycles: " + juce::String(owner.cpuCycles()) +
                " | " + owner.machineStatus() +
                " | illegal instructions: " +
                juce::String(owner.illegalInstructions()),
            juce::dontSendNotification);
}

void Eps16PanelEditor::paint(juce::Graphics &graphics) {
    graphics.fillAll(panelColour);
    if (pageButtons.front() == nullptr) return;
    const int designHeight = keyboardExpanded ? expandedRackHeight : rackHeight;
    const float scale = juce::jmin(
        (float)getWidth() / rackWidth,
        (float)getHeight() / static_cast<float>(designHeight));
    graphics.setColour(rackLabelColour);

    auto above = [&graphics, scale](const PanelButton *button,
                                    const juce::String &text, int height = 13) {
        const auto bounds = button->getBounds();
        const int margin = juce::roundToInt(5.0f * scale);
        const int scaledHeight = juce::roundToInt((float)height * scale);
        graphics.drawText(text, bounds.getX() - margin,
                          bounds.getY() - scaledHeight,
                          bounds.getWidth() + margin * 2, scaledHeight,
                          juce::Justification::centred);
    };
    auto below = [&graphics, scale](const PanelButton *button,
                                    const juce::String &text, int height = 13) {
        const auto bounds = button->getBounds();
        const int margin = juce::roundToInt(12.0f * scale);
        const int scaledHeight = juce::roundToInt((float)height * scale);
        graphics.drawText(text, bounds.getX() - margin, bounds.getBottom(),
                          bounds.getWidth() + margin * 2, scaledHeight,
                          juce::Justification::centred);
    };

    graphics.setFont(9.5f * scale);
    static const char *pageTop[12] = {
        "1", "2", "3", "4", "5", "6", "7", "8", "9", "", "0", ""
    };
    static const char *pageBottom[12] = {
        "ENV 1", "ENV 2", "ENV 3", "PITCH", "FILTER", "AMP",
        "LFO", "WAVE", "LAYER", "SAMPLE", "TRACK", ""
    };
    for (std::size_t index = 0; index < pageButtons.size(); ++index) {
        if (*pageTop[index]) above(pageButtons[index], pageTop[index]);
        if (*pageBottom[index]) below(pageButtons[index], pageBottom[index]);
    }
    graphics.setFont(7.5f * scale);
    const auto effectBounds = pageButtons[11]->getBounds();
    graphics.drawText("EFFECT", effectBounds.getX() - 8,
                      effectBounds.getBottom(), effectBounds.getWidth() + 16,
                      juce::roundToInt(9 * scale), juce::Justification::centred);
    graphics.drawText("SELECT", effectBounds.getX() - 8,
                      effectBounds.getBottom() + juce::roundToInt(8 * scale),
                      effectBounds.getWidth() + 16, juce::roundToInt(9 * scale),
                      juce::Justification::centred);
    graphics.drawText("BYPASS", effectBounds.getX() - 8,
                      effectBounds.getBottom() + juce::roundToInt(16 * scale),
                      effectBounds.getWidth() + 16, juce::roundToInt(9 * scale),
                      juce::Justification::centred);

    graphics.setFont(8.5f * scale);
    static const char *modeLabels[7] = {
        "LOAD", "CMD", "EDIT", "INST", "SEQ SONG", "SYSTEM MIDI", "EFFECTS"
    };
    for (std::size_t index = 0; index < modeButtons.size(); ++index)
        below(modeButtons[index], modeLabels[index]);

    graphics.setFont(9.5f * scale);
    const int trackHeaderX = trackButtons.front()->getX();
    const int trackHeaderRight = trackButtons.back()->getRight();
    graphics.drawText(
        juce::CharPointer_UTF8("INSTRUMENTS  \xe2\x80\xa2  TRACKS"),
        trackHeaderX,
                      trackButtons.front()->getY() - juce::roundToInt(38 * scale),
                      trackHeaderRight - trackHeaderX,
                      juce::roundToInt(13 * scale),
                      juce::Justification::centred);

    const auto ledPhase = [this](unsigned int bit) {
        const auto mask = (std::uint16_t)(UINT16_C(1) << bit);
        return (trackLedOn & mask) &&
               (!(trackLedFlash & mask) || trackLedFlashPhase);
    };
    const auto loadedColour = juce::Colour(0xffdc8732);
    const auto selectedColour = juce::Colour(0xffffd84b);
    const auto unlitColour = juce::Colour(0xff252725);
    for (unsigned int index = 0; index < trackButtons.size(); ++index) {
        const auto button = trackButtons[index]->getBounds();
        const int ledWidth = juce::roundToInt(17.0f * scale);
        const int ledHeight = juce::jmax(2, juce::roundToInt(4.0f * scale));
        const int ledX = button.getCentreX() - ledWidth / 2;
        const int loadedY = button.getY() - juce::roundToInt(20.0f * scale);
        const int selectedY = button.getY() - juce::roundToInt(11.0f * scale);
        auto drawLed = [&graphics, unlitColour](juce::Rectangle<int> area,
                                                juce::Colour colour,
                                                bool lit) {
            graphics.setColour(lit ? colour.withAlpha(0.20f) : unlitColour);
            if (lit) graphics.fillRoundedRectangle(area.expanded(3).toFloat(),
                                                    2.0f);
            graphics.setColour(lit ? colour : unlitColour);
            graphics.fillRoundedRectangle(area.toFloat(), 1.0f);
        };
        drawLed({ledX, loadedY, ledWidth, ledHeight}, loadedColour,
                ledPhase(index));
        drawLed({ledX, selectedY, ledWidth, ledHeight}, selectedColour,
                ledPhase(index + 8));
    }
    graphics.setFont(5.8f * scale);
    graphics.setColour(rackLabelColour.withAlpha(0.72f));
    graphics.drawText("LOADED", trackHeaderX - juce::roundToInt(40 * scale),
                      trackButtons.front()->getY() - juce::roundToInt(22 * scale),
                      juce::roundToInt(38 * scale), juce::roundToInt(8 * scale),
                      juce::Justification::centredRight);
    graphics.drawText("SELECTED", trackHeaderRight + juce::roundToInt(2 * scale),
                      trackButtons.front()->getY() - juce::roundToInt(13 * scale),
                      juce::roundToInt(45 * scale), juce::roundToInt(8 * scale),
                      juce::Justification::centredLeft);

    graphics.setFont(7.5f * scale);
    static const char *sequenceLabels[3] = {"RECORD", "STOP / CONT", "PLAY"};
    for (std::size_t index = 0; index < sequencerButtons.size(); ++index)
        above(sequencerButtons[index], sequenceLabels[index], 12);

    auto drawNavigationTriangle =
        [&graphics, scale](juce::Point<float> centre, float rotation) {
            const float radius = 8.0f * scale;
            juce::Path triangle;
            for (int vertex = 0; vertex < 3; ++vertex) {
                const float angle =
                    rotation +
                    juce::MathConstants<float>::twoPi * (float)vertex / 3.0f;
                const auto point =
                    centre + juce::Point<float>(std::cos(angle),
                                                std::sin(angle)) *
                                 radius;
                if (vertex == 0)
                    triangle.startNewSubPath(point);
                else
                    triangle.lineTo(point);
            }
            triangle.closeSubPath();
            graphics.strokePath(
                triangle,
                juce::PathStrokeType(1.1f * scale,
                                     juce::PathStrokeType::curved,
                                     juce::PathStrokeType::rounded));
        };
    drawNavigationTriangle(
        {(float)upButton->getBounds().getCentreX(),
         (float)upButton->getY() - 10.0f * scale},
        -juce::MathConstants<float>::halfPi);
    drawNavigationTriangle(
        {(float)downButton->getBounds().getCentreX(),
         (float)downButton->getBottom() + 10.0f * scale},
        juce::MathConstants<float>::halfPi);
    drawNavigationTriangle(
        {(float)leftButton->getX() - 13.0f * scale,
         (float)leftButton->getBounds().getCentreY()},
        juce::MathConstants<float>::pi);
    drawNavigationTriangle(
        {(float)rightButton->getRight() + 13.0f * scale,
         (float)rightButton->getBounds().getCentreY()},
        0.0f);

    graphics.setFont(8.0f * scale);
    above(cancelButton, "NO", 13);
    below(cancelButton, "CANCEL", 12);
    above(enterButton, "YES", 13);
    below(enterButton, "ENTER", 12);

    auto baseRect = [this, scale, designHeight](int x, int y, int width,
                                                int height) {
        const int offsetX =
            (getWidth() - juce::roundToInt(rackWidth * scale)) / 2;
        const int offsetY =
            (getHeight() - juce::roundToInt(
                               static_cast<float>(designHeight) * scale)) / 2;
        return juce::Rectangle<int>(
            offsetX + juce::roundToInt((float)x * scale),
            offsetY + juce::roundToInt((float)y * scale),
            juce::roundToInt((float)width * scale),
            juce::roundToInt((float)height * scale));
    };
    auto drawLegendLines = [&graphics, &baseRect](int x, int width) {
        if (width <= 0) return;
        graphics.fillRect(baseRect(x, 248, width, 2));
        graphics.fillRect(baseRect(x, 252, width, 2));
    };

    graphics.setColour(accentColour);
    drawLegendLines(0, 41);
    drawLegendLines(89, 52);
    drawLegendLines(181, 157);
    drawLegendLines(376, 149);
    drawLegendLines(595, 121);

    int trackLineX = 716;
    for (int index = 0; index < 8; ++index) {
        const int numberCentre = 722 + index * 55;
        drawLegendLines(trackLineX, numberCentre - 6 - trackLineX);
        trackLineX = numberCentre + 6;
    }
    drawLegendLines(trackLineX, 1216 - trackLineX);
    drawLegendLines(1286, 64);

    graphics.setColour(rackLabelColour);
    graphics.setFont(9.0f * scale);
    graphics.drawText("VOLUME", baseRect(38, 240, 54, 20),
                      juce::Justification::centred);
    graphics.drawText("MODE", baseRect(133, 240, 56, 20),
                      juce::Justification::centred);
    graphics.drawText("PAGE", baseRect(331, 240, 51, 20),
                      juce::Justification::centred);
    graphics.drawText("DATA ENTRY", baseRect(513, 240, 93, 20),
                      juce::Justification::centred);
    for (int index = 0; index < 8; ++index)
        graphics.drawText(juce::String(index + 1),
                          baseRect(712 + index * 55, 240, 20, 20),
                          juce::Justification::centred);
    graphics.drawText("SEQUENCER", baseRect(1207, 240, 88, 20),
                      juce::Justification::centred);
}

void Eps16PanelEditor::resized() {
    if (pageButtons.front() == nullptr) return;
    const int designHeight = keyboardExpanded ? expandedRackHeight : rackHeight;
    const float scale = juce::jmin(
        (float)getWidth() / rackWidth,
        (float)getHeight() / static_cast<float>(designHeight));
    const int offsetX = (getWidth() - juce::roundToInt(rackWidth * scale)) / 2;
    const int offsetY =
        (getHeight() - juce::roundToInt(
                           static_cast<float>(designHeight) * scale)) / 2;
    auto rackRect = [scale, offsetX, offsetY](int x, int y,
                                              int width, int height) {
        return juce::Rectangle<int>(
            offsetX + juce::roundToInt((float)x * scale),
            offsetY + juce::roundToInt((float)y * scale),
            juce::roundToInt((float)width * scale),
            juce::roundToInt((float)height * scale));
    };

    vfd.setBounds(rackRect(696, 27, 444, 101));
    vfd.setFont(juce::Font(juce::FontOptions("Menlo", 17.0f * scale,
                                             juce::Font::plain)));
    status.setBounds({});
    masterVolume.setBounds(rackRect(36, 25, 76, 198));
    dataEntry.setBounds(rackRect(452, 26, 76, 199));

    const int pageX[3] = {294, 345, 396};
    const int pageY[3] = {51, 105, 159};
    for (std::size_t index = 0; index < 9; ++index)
        pageButtons[index]->setBounds(
            rackRect(pageX[index % 3], pageY[index / 3], 34, 18));
    pageButtons[9]->setBounds(rackRect(1270, 89, 38, 18));
    pageButtons[10]->setBounds(rackRect(345, 203, 34, 18));
    pageButtons[11]->setBounds(rackRect(1183, 89, 38, 18));

    modeButtons[0]->setBounds(rackRect(142, 69, 38, 18));
    modeButtons[1]->setBounds(rackRect(142, 119, 38, 18));
    modeButtons[2]->setBounds(rackRect(142, 169, 38, 18));
    modeButtons[3]->setBounds(rackRect(225, 47, 38, 18));
    modeButtons[4]->setBounds(rackRect(225, 99, 38, 18));
    modeButtons[5]->setBounds(rackRect(225, 151, 38, 18));
    modeButtons[6]->setBounds(rackRect(225, 203, 38, 18));

    for (std::size_t index = 0; index < trackButtons.size(); ++index)
        trackButtons[index]->setBounds(
            rackRect(698 + (int)index * 55, 181, 48, 34));

    for (std::size_t index = 0; index < sequencerButtons.size(); ++index)
        sequencerButtons[index]->setBounds(
            rackRect(1172 + (int)index * 52, 196, 39, 18));
    upButton->setBounds(rackRect(589, 50, 39, 18));
    leftButton->setBounds(rackRect(551, 84, 39, 18));
    rightButton->setBounds(rackRect(627, 84, 39, 18));
    downButton->setBounds(rackRect(589, 119, 39, 18));
    cancelButton->setBounds(rackRect(552, 183, 42, 22));
    enterButton->setBounds(rackRect(624, 183, 42, 22));
    osDiskButton.setBounds(rackRect(1160, 27, 39, 44));
    newDiskButton.setBounds(rackRect(1203, 27, 39, 44));
    loadDiskButton.setBounds(rackRect(1246, 27, 39, 44));
    saveDiskButton.setBounds(rackRect(1289, 27, 39, 44));
    diskName.setBounds(rackRect(1160, 6, 168, 17));
    diskName.setFont(juce::Font(juce::FontOptions(
        "Helvetica Neue", 10.0f * scale, juce::Font::plain)));
    keyboardToggle.setBounds(rackRect(618, 263, 114, 18));
    pianoKeyboard.setBounds(rackRect(0, rackHeight, rackWidth,
                                     keyboardHeight));

}
