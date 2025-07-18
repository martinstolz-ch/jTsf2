
/**
 * (c) martin stolz @ noizplay
 */

#pragma once

#include <JuceHeader.h>

// TinySoundFont 2 - header only implementation
#define TSF_IMPLEMENTATION
#include "tsf.h" // du musst tsf.h von https://github.com/schellingb/TinySoundFont downloaden

namespace aa {
    class SF2Player final {
    public:
        SF2Player() = default;

        ~SF2Player() {
            if (soundFont) {
                tsf_close(soundFont);
            }
        }

        bool loadSF2File(const File& file) {
            if (soundFont) {
                tsf_close(soundFont);
                soundFont = nullptr;
            }

            if (!file.existsAsFile()) {
                return false;
            }

            // load file into memory
            FileInputStream stream{file};
            if (!stream.openedOk()) {
                return false;
            }

            auto fileSize = static_cast<size_t>(file.getSize());
            fileData.resize(fileSize);

            if (stream.read(fileData.data(), static_cast<int>(fileSize)) != static_cast<int>(fileSize)) {
                return false;
            }

            // create soundfont from memory
            soundFont = tsf_load_memory(fileData.data(), static_cast<int>(fileSize));

            if (!soundFont) {
                return false;
            }

            // set output mode (stereo, 44.1kHz default)
            tsf_set_output(soundFont, TSF_STEREO_INTERLEAVED, 44100, 0.0f);

            // wichtig: preset muss explizit geladen werden!
            if (tsf_get_presetcount(soundFont) > 0) {
                // lade erstes preset auf channel 0 (= MIDI channel 1)
                tsf_channel_set_presetindex(soundFont, 0, 0);
            }

            currentFile = file;
            return true;
        }

        void setSampleRate(double sampleRate) {
            if (soundFont) {
                tsf_set_output(soundFont, TSF_STEREO_INTERLEAVED, static_cast<int>(sampleRate), 0.0f);
            }
        }

        void noteOn(int midiChannel, int noteNumber, int velocity) {
            if (soundFont) {
                tsf_channel_note_on(soundFont, midiChannel, noteNumber, velocity / 127.0f);
            }
        }

        void noteOff(int midiChannel, int noteNumber) {
            if (soundFont) {
                tsf_channel_note_off(soundFont, midiChannel, noteNumber);
            }
        }

        void renderAudio(float* outputBuffer, int numSamples) {
            if (soundFont) {
                tsf_render_float(soundFont, outputBuffer, numSamples, 0);
            } else {
                // silence wenn keine soundfont geladen
                FloatVectorOperations::clear(outputBuffer, numSamples * 2); // stereo
            }
        }

        bool isLoaded() const {
            return soundFont != nullptr;
        }

        String getCurrentFileName() const {
            return currentFile.getFileName();
        }

        int getPresetCount() const {
            return soundFont ? tsf_get_presetcount(soundFont) : 0;
        }

        void selectPreset(int midiChannel, int presetIndex) {
            if (soundFont && presetIndex >= 0 && presetIndex < getPresetCount()) {
                // verwende presetindex statt presetnumber für direkten zugriff
                tsf_channel_set_presetindex(soundFont, midiChannel, presetIndex);

                DBG("SF2Player: Loaded preset " + String{presetIndex} + " on channel " + String{midiChannel});
            }
        }

        String getPresetName(int presetIndex) const {
            if (soundFont && presetIndex >= 0 && presetIndex < getPresetCount()) {
                auto* preset = tsf_get_presetname(soundFont, presetIndex);
                return preset ? String{preset} : String{"Preset " + String{presetIndex}};
            }
            return {};
        }

    private:
        tsf* soundFont{nullptr};
        std::vector<unsigned char> fileData;
        File currentFile;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SF2Player)
    };
}