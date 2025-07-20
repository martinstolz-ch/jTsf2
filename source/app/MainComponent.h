/**
 * (c) martin stolz @ noizplay
 */

#pragma once

#include <JuceHeader.h>

#include "../common/cmakeVar.h"
#include "../common/appConfig.h"
#include "SF2Player.h"

namespace aa {

class MainComponent final
    : public AudioAppComponent,
      private Button::Listener,
      private MidiKeyboardState::Listener,
      private ComboBox::Listener,
      private Slider::Listener {

public:
    MainComponent() {

        // load sf2 button
        loadButton.setButtonText("Load SF2 File");
        loadButton.addListener(this);
        addAndMakeVisible(loadButton);

        // preset selector
        presetCombo.setTextWhenNothingSelected("No presets available");
        presetCombo.addListener(this);
        presetCombo.setEnabled(false);
        addAndMakeVisible(presetCombo);

        // tuning slider
        tuningSlider.setSliderStyle(Slider::LinearHorizontal);
        tuningSlider.setTextBoxStyle(Slider::TextBoxRight, false, 80, 20);
        tuningSlider.setRange(436.0, 444.0, 0.1);
        tuningSlider.setValue(440.0);
        tuningSlider.setTextValueSuffix(" Hz");
        tuningSlider.addListener(this);
        addAndMakeVisible(tuningSlider);

        tuningLabel.setText("Tuning:", dontSendNotification);
        tuningLabel.attachToComponent(&tuningSlider, true);
        addAndMakeVisible(tuningLabel);

        // status label
        statusLabel.setText("No SF2 file loaded", dontSendNotification);
        statusLabel.setJustificationType(Justification::centred);
        addAndMakeVisible(statusLabel);

        // midi keyboard
        keyboardState.reset();
        keyboardState.addListener(this);
        keyboard.setKeyWidth(40.0f);
        keyboard.setLowestVisibleKey(36); // C2
        addAndMakeVisible(keyboard);

        setSize(800, 400);

        // audio permissions
        if (RuntimePermissions::isRequired(RuntimePermissions::recordAudio)
            &&
            !RuntimePermissions::isGranted(RuntimePermissions::recordAudio)) {

            RuntimePermissions::request(RuntimePermissions::recordAudio,
                                       [&](bool granted) {
                                           setAudioChannels(granted ? 2 : 0, 2);
                                       });
        } else {
            setAudioChannels(2, 2);
        }
    }

    ~MainComponent() override {
        tuningSlider.removeListener(this);
        presetCombo.removeListener(this);
        keyboardState.removeListener(this);
        shutdownAudio();
    }

    void paint(Graphics& g) override {
        g.fillAll(getLookAndFeel().findColour(ResizableWindow::backgroundColourId));

        // company url unten
        g.setColour(app_config::MAIN_COLOUR);
        g.setFont(FontOptions().withStyle("light"));
        g.drawFittedText(
            cmakeVar::companyURL,
            getLocalBounds().removeFromBottom(30),
            Justification::centredBottom,
            1);
    }

    void resized() override {
        auto bounds = getLocalBounds();
        bounds.reduce(20, 20);

        // load button oben
        loadButton.setBounds(bounds.removeFromTop(40));
        bounds.removeFromTop(10);

        // preset combo
        presetCombo.setBounds(bounds.removeFromTop(30));
        bounds.removeFromTop(10);

        // tuning slider
        auto tuningBounds = bounds.removeFromTop(30);
        tuningBounds.removeFromLeft(60); // platz für label
        tuningSlider.setBounds(tuningBounds);
        bounds.removeFromTop(10);

        // status label
        statusLabel.setBounds(bounds.removeFromTop(30));
        bounds.removeFromTop(20);

        // keyboard unten (reserviere platz für company url)
        bounds.removeFromBottom(40);
        keyboard.setBounds(bounds.removeFromBottom(120));
    }

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override {
        ignoreUnused(samplesPerBlockExpected);

        DBG("prepareToPlay called - Sample rate: " + String{sampleRate} + " Hz");

        // update sample rate falls geändert
        if (std::abs(currentSampleRate - sampleRate) > 0.1) {
            currentSampleRate = sampleRate;
            sf2Player.setSampleRate(sampleRate);

            DBG("Sample rate changed to: " + String{sampleRate} + " Hz");
        }

        // clear pending midi messages
        const ScopedLock lock{midiMessageLock};
        pendingMidiMessages.clear();
    }

    void getNextAudioBlock(const AudioSourceChannelInfo& bufferToFill) override {
        bufferToFill.clearActiveBufferRegion();

        if (!sf2Player.isLoaded()) {
            return;
        }

        // process pending midi messages
        {
            const ScopedLock lock{midiMessageLock};

            for (const auto metadata : pendingMidiMessages) {
                auto message = metadata.getMessage();

                if (message.isNoteOn()) {
                    sf2Player.noteOn(0, message.getNoteNumber(), message.getVelocity());
                }
                else if (message.isNoteOff()) {
                    sf2Player.noteOff(0, message.getNoteNumber());
                }
            }

            pendingMidiMessages.clear();
        }

        // render audio from sf2
        auto numSamples = bufferToFill.numSamples;
        auto numChannels = bufferToFill.buffer->getNumChannels();

        if (numChannels >= 2) {
            // stereo output
            tempBuffer.resize(numSamples * 2);
            sf2Player.renderAudio(tempBuffer.data(), numSamples);

            // copy to juce buffer (interleaved -> separate channels)
            auto* leftChannel = bufferToFill.buffer->getWritePointer(0, bufferToFill.startSample);
            auto* rightChannel = bufferToFill.buffer->getWritePointer(1, bufferToFill.startSample);

            for (int i = 0; i < numSamples; ++i) {
                leftChannel[i] = tempBuffer[i * 2];
                rightChannel[i] = tempBuffer[i * 2 + 1];
            }
        }
        else if (numChannels == 1) {
            // mono output (mix L+R)
            tempBuffer.resize(numSamples * 2);
            sf2Player.renderAudio(tempBuffer.data(), numSamples);

            auto* monoChannel = bufferToFill.buffer->getWritePointer(0, bufferToFill.startSample);

            for (int i = 0; i < numSamples; ++i) {
                monoChannel[i] = (tempBuffer[i * 2] + tempBuffer[i * 2 + 1]) * 0.5f;
            }
        }
    }

    void releaseResources() override {
        // sf2 player cleanup happens in destructor
    }

    // MidiKeyboardState::Listener
    void handleNoteOn(MidiKeyboardState* /*source*/, int /*midiChannel*/, int midiNoteNumber, float velocity) override {
        const ScopedLock lock{midiMessageLock};
        pendingMidiMessages.addEvent(MidiMessage::noteOn(1, midiNoteNumber, velocity), 0);
    }

    void handleNoteOff(MidiKeyboardState* /*source*/, int /*midiChannel*/, int midiNoteNumber, float /*velocity*/) override {
        const ScopedLock lock{midiMessageLock};
        pendingMidiMessages.addEvent(MidiMessage::noteOff(1, midiNoteNumber), 0);
    }

private:
    void buttonClicked(Button* button) override {
        if (button == &loadButton) {
            showFileChooser();
        }
    }

    void comboBoxChanged(ComboBox* comboBox) override {
        if (comboBox == &presetCombo) {
            auto selectedPresetIndex = presetCombo.getSelectedItemIndex();
            if (selectedPresetIndex >= 0 && sf2Player.isLoaded()) {
                sf2Player.selectPreset(0, selectedPresetIndex); // channel 0 = MIDI channel 1

                DBG("Selected preset " + String{selectedPresetIndex} + ": " +
                    sf2Player.getPresetName(selectedPresetIndex));
            }
        }
    }

    void sliderValueChanged(Slider* slider) override {
        if (slider == &tuningSlider) {
            auto tuningHz = static_cast<float>(tuningSlider.getValue());
            sf2Player.setTuning(tuningHz);

            DBG("Tuning changed to: " + String{tuningHz} + " Hz");
        }
    }

    void showFileChooser() {
        chooser = std::make_unique<FileChooser>("Select SF2 file...",
                                               File{},
                                               "*.sf2");

        auto chooserFlags = FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles;

        chooser->launchAsync(chooserFlags, [this](const FileChooser& fc) {
            auto file = fc.getResult();
            if (file != File{}) {
                loadSF2File(file);
            }
        });
    }

    void loadSF2File(const File& file) {
        if (!file.existsAsFile()) {
            statusLabel.setText("File not found!", dontSendNotification);
            return;
        }

        bool success = sf2Player.loadSF2File(file);

        if (success) {
            // sample rate update falls bereits audio läuft
            if (currentSampleRate > 0) {
                sf2Player.setSampleRate(currentSampleRate);
            }

            // tuning auf aktuellen wert setzen
            sf2Player.setTuning(static_cast<float>(tuningSlider.getValue()));

            // populate preset combo box
            updatePresetCombo();

            statusLabel.setText("Loaded: " + file.getFileName() +
                              " (" + String{sf2Player.getPresetCount()} + " presets)",
                              dontSendNotification);

            DBG("SF2 file loaded successfully: " + file.getFullPathName());
            DBG("Presets available: " + String{sf2Player.getPresetCount()});

            if (sf2Player.getPresetCount() > 0) {
                DBG("First preset: " + sf2Player.getPresetName(0));
            }
        }
        else {
            statusLabel.setText("Failed to load: " + file.getFileName(), dontSendNotification);
            presetCombo.clear();
            presetCombo.setEnabled(false);
            DBG("Failed to load SF2 file: " + file.getFullPathName());
        }
    }

    void updatePresetCombo() {
        presetCombo.clear();

        if (!sf2Player.isLoaded()) {
            presetCombo.setEnabled(false);
            return;
        }

        auto presetCount = sf2Player.getPresetCount();

        for (int i = 0; i < presetCount; ++i) {
            auto presetName = sf2Player.getPresetName(i);
            presetCombo.addItem(String{i + 1} + ": " + presetName, i + 1);
        }

        if (presetCount > 0) {
            presetCombo.setSelectedItemIndex(0); // select first preset
            presetCombo.setEnabled(true);
            sf2Player.selectPreset(0, 0); // select first preset on channel 0
        }
    }

    // components
    TextButton loadButton;
    ComboBox presetCombo;
    Slider tuningSlider;
    Label tuningLabel;
    Label statusLabel;
    MidiKeyboardState keyboardState;
    MidiKeyboardComponent keyboard{keyboardState, MidiKeyboardComponent::horizontalKeyboard};

    // sf2 engine
    SF2Player sf2Player;

    // audio
    double currentSampleRate{0.0};
    std::vector<float> tempBuffer;
    MidiBuffer pendingMidiMessages;
    CriticalSection midiMessageLock;

    // file chooser
    std::unique_ptr<FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

}