#include "Params.h"

namespace cortex::params
{

juce::String id::macro (int index)
{
    return "macro" + juce::String (index + 1);
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using Float  = juce::AudioParameterFloat;
    using Choice = juce::AudioParameterChoice;
    using Int    = juce::AudioParameterInt;
    using Bool   = juce::AudioParameterBool;
    using Range  = juce::NormalisableRange<float>;

    const auto pid = [] (const juce::String& name) { return juce::ParameterID { name, 1 }; };

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<Choice> (pid (id::sourceType), "Source Type",
                                          juce::StringArray { "Full Mix", "Kick", "Snare", "Hats", "Bass", "Vocal", "Synth" }, 0));

    layout.add (std::make_unique<Float> (pid (id::kickSensitivity),  "Kick Sensitivity",  Range (0.0f, 1.0f), 0.5f));
    layout.add (std::make_unique<Float> (pid (id::snareSensitivity), "Snare Sensitivity", Range (0.0f, 1.0f), 0.5f));
    layout.add (std::make_unique<Float> (pid (id::hatSensitivity),   "Hat Sensitivity",   Range (0.0f, 1.0f), 0.5f));

    const Range releaseRange (0.02f, 1.0f, 0.0f, 0.4f);   // seconds, skewed toward short times
    const auto seconds = juce::AudioParameterFloatAttributes().withLabel ("s");
    layout.add (std::make_unique<Float> (pid (id::kickRelease),  "Kick Release",  releaseRange, 0.18f, seconds));
    layout.add (std::make_unique<Float> (pid (id::snareRelease), "Snare Release", releaseRange, 0.15f, seconds));
    layout.add (std::make_unique<Float> (pid (id::hatRelease),   "Hat Release",   releaseRange, 0.08f, seconds));

    layout.add (std::make_unique<Float> (pid (id::intensity), "Intensity", Range (0.0f, 1.0f), 1.0f));

    for (int i = 0; i < id::numMacros; ++i)
        layout.add (std::make_unique<Float> (pid (id::macro (i)), "Macro " + juce::String (i + 1), Range (0.0f, 1.0f), 0.0f));

    layout.add (std::make_unique<Int> (pid (id::sceneSelect),  "Scene",  1, 64, 1));
    layout.add (std::make_unique<Int> (pid (id::moduleSelect), "Module", 1, 64, 1));
    layout.add (std::make_unique<Int> (pid (id::lookSelect),   "Look",   0, 64, 0));

    // Standard by default; the limiter is never switched off silently (section 9).
    layout.add (std::make_unique<Choice> (pid (id::flashLimiter), "Flash Limiter",
                                          juce::StringArray { "Off", "Standard", "Strict" }, 1));
    layout.add (std::make_unique<Bool> (pid (id::freeze), "Freeze", false));

    return layout;
}

} // namespace cortex::params
