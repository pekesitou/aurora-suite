#include "ModuleCatalog.h"

#include <algorithm>
#include <cmath>

namespace aurora
{
namespace
{
ParamSpec makeFloat(const juce::String& prefix, const juce::String& rel, const juce::String& name,
                    float minV, float maxV, float defV, const juce::String& unit = {},
                    float step = 0.0f, float skew = 0.0f, int smoothingMs = 0)
{
    ParamSpec p;
    p.id = prefix + "." + rel;
    p.name = name;
    p.kind = ParamKind::continuous;
    p.minValue = minV;
    p.maxValue = maxV;
    p.defaultValue = defV;
    p.unit = unit;
    p.step = step;
    p.skewCentre = skew;
    p.smoothingMs = smoothingMs;
    return p;
}

ParamSpec makeBool(const juce::String& prefix, const juce::String& rel, const juce::String& name, bool def)
{
    ParamSpec p;
    p.id = prefix + "." + rel;
    p.name = name;
    p.kind = ParamKind::boolean;
    p.minValue = 0.0f;
    p.maxValue = 1.0f;
    p.defaultValue = def ? 1.0f : 0.0f;
    return p;
}

ParamSpec makeChoice(const juce::String& prefix, const juce::String& rel, const juce::String& name,
                     std::initializer_list<const char*> options, int defIndex)
{
    ParamSpec p;
    p.id = prefix + "." + rel;
    p.name = name;
    p.kind = ParamKind::choice;
    for (const auto* s : options)
        p.choices.add(s);
    p.minValue = 0.0f;
    p.maxValue = static_cast<float>(p.choices.size() - 1);
    p.defaultValue = static_cast<float>(defIndex);
    return p;
}

ModuleSpec makeModule(ModuleId id, ModuleType type, const juce::String& name,
                      const juce::String& label, bool activeByDefault)
{
    ModuleSpec m;
    m.id = id;
    m.type = type;
    m.name = name;
    m.prefix = instancePrefix(id);
    m.shortLabel = label;
    m.defaultActive = activeByDefault;
    m.params.push_back(makeBool(m.prefix, "active", "Active", activeByDefault));
    return m;
}

void addEqBand(ModuleSpec& m, int band, float freq)
{
    const auto b = "b" + juce::String(band);
    m.params.push_back(makeBool(m.prefix, b + ".on", "Band " + juce::String(band), true));
    m.params.push_back(makeChoice(m.prefix, b + ".type", "Band " + juce::String(band) + " Type",
                                  { "Low Pass", "High Pass", "Band Pass", "Notch", "Bell", "Low Shelf", "High Shelf" }, 4));
    m.params.push_back(makeFloat(m.prefix, b + ".freq", "Band " + juce::String(band) + " Frequency",
                                 20.0f, 20000.0f, freq, "Hz", 0.0f, 1000.0f, 30));
    m.params.push_back(makeFloat(m.prefix, b + ".gain", "Band " + juce::String(band) + " Gain",
                                 -18.0f, 18.0f, 0.0f, "dB", 0.01f, 0.0f, 20));
    m.params.push_back(makeFloat(m.prefix, b + ".q", "Band " + juce::String(band) + " Q",
                                 0.1f, 18.0f, 0.707f, "", 0.001f, 1.0f, 20));
}

void addMultibandBand(ModuleSpec& m, int band)
{
    const auto b = "b" + juce::String(band);
    m.params.push_back(makeFloat(m.prefix, b + ".threshold", "Band " + juce::String(band) + " Threshold",
                                 -60.0f, 0.0f, -18.0f, "dB", 0.1f, 0.0f, 20));
    m.params.push_back(makeFloat(m.prefix, b + ".ratio", "Band " + juce::String(band) + " Ratio",
                                 1.0f, 20.0f, 2.0f, ":1", 0.01f, 2.0f, 20));
    m.params.push_back(makeFloat(m.prefix, b + ".attack", "Band " + juce::String(band) + " Attack",
                                 0.1f, 200.0f, 10.0f, "ms", 0.1f, 10.0f, 20));
    m.params.push_back(makeFloat(m.prefix, b + ".release", "Band " + juce::String(band) + " Release",
                                 10.0f, 2000.0f, 100.0f, "ms", 1.0f, 200.0f, 20));
    m.params.push_back(makeFloat(m.prefix, b + ".knee", "Band " + juce::String(band) + " Knee",
                                 0.0f, 40.0f, 10.0f, "dB", 0.1f, 0.0f, 20));
    m.params.push_back(makeFloat(m.prefix, b + ".makeup", "Band " + juce::String(band) + " Makeup",
                                 0.0f, 24.0f, 0.0f, "dB", 0.01f, 0.0f, 20));
    m.params.push_back(makeBool(m.prefix, b + ".mute", "Band " + juce::String(band) + " Mute", false));
    m.params.push_back(makeBool(m.prefix, b + ".solo", "Band " + juce::String(band) + " Solo", false));
}

Catalog buildCatalog()
{
    Catalog cat;

    cat.globals.push_back(makeFloat("global", "inputTrim", "Input Trim", -24.0f, 24.0f, 0.0f, "dB", 0.01f, 0.0f, 20));
    cat.globals.push_back(makeFloat("global", "outputTrim", "Output Trim", -24.0f, 24.0f, 0.0f, "dB", 0.01f, 0.0f, 20));
    cat.globals.push_back(makeFloat("aria", "strength", "ARIA Strength", 0.0f, 100.0f, 100.0f, "%", 1.0f));

    {
        auto m = makeModule(ModuleId::utilA, ModuleType::utility, "Utility", "UTIL", true);
        m.params.push_back(makeFloat(m.prefix, "gain", "Gain", -24.0f, 24.0f, 0.0f, "dB", 0.01f, 0.0f, 20));
        m.params.push_back(makeFloat(m.prefix, "pan", "Pan", -1.0f, 1.0f, 0.0f, "", 0.01f, 0.0f, 20));
        m.params.push_back(makeBool(m.prefix, "polarityL", "Polarity L", false));
        m.params.push_back(makeBool(m.prefix, "polarityR", "Polarity R", false));
        m.params.push_back(makeBool(m.prefix, "mono", "Mono", false));
        m.params.push_back(makeBool(m.prefix, "swap", "Swap L/R", false));
        cat.modules.push_back(std::move(m));
    }
    {
        auto m = makeModule(ModuleId::utilB, ModuleType::utility, "Utility", "UTIL", false);
        m.params.push_back(makeFloat(m.prefix, "gain", "Gain", -24.0f, 24.0f, 0.0f, "dB", 0.01f, 0.0f, 20));
        m.params.push_back(makeFloat(m.prefix, "pan", "Pan", -1.0f, 1.0f, 0.0f, "", 0.01f, 0.0f, 20));
        m.params.push_back(makeBool(m.prefix, "polarityL", "Polarity L", false));
        m.params.push_back(makeBool(m.prefix, "polarityR", "Polarity R", false));
        m.params.push_back(makeBool(m.prefix, "mono", "Mono", false));
        m.params.push_back(makeBool(m.prefix, "swap", "Swap L/R", false));
        cat.modules.push_back(std::move(m));
    }
    {
        auto m = makeModule(ModuleId::eqA, ModuleType::eq, "Parametric EQ", "EQ", true);
        m.params.push_back(makeFloat(m.prefix, "output", "Output", -12.0f, 12.0f, 0.0f, "dB", 0.01f, 0.0f, 20));
        const float freqs[8] = { 80.0f, 160.0f, 320.0f, 640.0f, 1250.0f, 2500.0f, 5000.0f, 10000.0f };
        for (int b = 1; b <= 8; ++b)
            addEqBand(m, b, freqs[b - 1]);
        cat.modules.push_back(std::move(m));
    }
    {
        auto m = makeModule(ModuleId::eqB, ModuleType::eq, "Parametric EQ", "EQ", false);
        m.params.push_back(makeFloat(m.prefix, "output", "Output", -12.0f, 12.0f, 0.0f, "dB", 0.01f, 0.0f, 20));
        const float freqs[8] = { 100.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 12000.0f };
        for (int b = 1; b <= 8; ++b)
            addEqBand(m, b, freqs[b - 1]);
        cat.modules.push_back(std::move(m));
    }
    for (auto id : { ModuleId::compA, ModuleId::compB })
    {
        auto m = makeModule(id, ModuleType::compressor, "Compressor", "COMP", false);
        m.params.push_back(makeFloat(m.prefix, "threshold", "Threshold", -60.0f, 0.0f, -18.0f, "dB", 0.1f, 0.0f, 20));
        m.params.push_back(makeFloat(m.prefix, "ratio", "Ratio", 1.0f, 20.0f, 4.0f, ":1", 0.01f, 4.0f, 20));
        m.params.push_back(makeFloat(m.prefix, "attack", "Attack", 0.1f, 200.0f, 10.0f, "ms", 0.1f, 10.0f, 20));
        m.params.push_back(makeFloat(m.prefix, "release", "Release", 10.0f, 2000.0f, 100.0f, "ms", 1.0f, 150.0f, 20));
        m.params.push_back(makeFloat(m.prefix, "knee", "Knee", 0.0f, 40.0f, 10.0f, "dB", 0.1f, 0.0f, 20));
        m.params.push_back(makeChoice(m.prefix, "detector", "Detector", { "Peak", "RMS" }, 0));
        m.params.push_back(makeFloat(m.prefix, "rmsWindow", "RMS Window", 1.0f, 50.0f, 10.0f, "ms", 0.1f, 10.0f, 20));
        m.params.push_back(makeFloat(m.prefix, "makeup", "Makeup", 0.0f, 24.0f, 0.0f, "dB", 0.01f, 0.0f, 20));
        m.params.push_back(makeBool(m.prefix, "autoMakeup", "Auto Makeup", false));
        m.params.push_back(makeFloat(m.prefix, "mix", "Mix", 0.0f, 100.0f, 100.0f, "%", 0.1f, 0.0f, 20));
        m.params.push_back(makeFloat(m.prefix, "scHPF", "Sidechain HPF", 20.0f, 500.0f, 20.0f, "Hz", 1.0f, 100.0f, 20));
        m.params.push_back(makeFloat(m.prefix, "link", "Stereo Link", 0.0f, 100.0f, 100.0f, "%", 0.1f, 0.0f, 20));
        cat.modules.push_back(std::move(m));
    }
    {
        auto m = makeModule(ModuleId::mbA, ModuleType::multiband, "Multiband Compressor", "MB", false);
        m.params.push_back(makeFloat(m.prefix, "x1", "Crossover 1", 40.0f, 500.0f, 120.0f, "Hz", 1.0f, 150.0f, 30));
        m.params.push_back(makeFloat(m.prefix, "x2", "Crossover 2", 200.0f, 3000.0f, 800.0f, "Hz", 1.0f, 800.0f, 30));
        m.params.push_back(makeFloat(m.prefix, "x3", "Crossover 3", 1000.0f, 12000.0f, 5000.0f, "Hz", 1.0f, 4000.0f, 30));
        for (int b = 1; b <= 4; ++b)
            addMultibandBand(m, b);
        m.params.push_back(makeFloat(m.prefix, "mix", "Mix", 0.0f, 100.0f, 100.0f, "%", 0.1f, 0.0f, 20));
        cat.modules.push_back(std::move(m));
    }
    {
        auto m = makeModule(ModuleId::dsA, ModuleType::deEsser, "De-Esser", "DE-ESS", false);
        m.params.push_back(makeFloat(m.prefix, "freq", "Frequency", 3000.0f, 10000.0f, 6000.0f, "Hz", 1.0f, 6000.0f, 20));
        m.params.push_back(makeFloat(m.prefix, "threshold", "Threshold", -60.0f, 0.0f, -24.0f, "dB", 0.1f, 0.0f, 20));
        m.params.push_back(makeFloat(m.prefix, "ratio", "Ratio", 1.0f, 20.0f, 4.0f, ":1", 0.01f, 4.0f, 20));
        m.params.push_back(makeFloat(m.prefix, "attack", "Attack", 0.1f, 50.0f, 1.0f, "ms", 0.01f, 1.0f, 20));
        m.params.push_back(makeFloat(m.prefix, "release", "Release", 10.0f, 500.0f, 50.0f, "ms", 1.0f, 50.0f, 20));
        m.params.push_back(makeFloat(m.prefix, "range", "Range", 0.0f, 24.0f, 12.0f, "dB", 0.1f, 0.0f, 20));
        m.params.push_back(makeBool(m.prefix, "listen", "Listen", false));
        m.params.push_back(makeFloat(m.prefix, "mix", "Mix", 0.0f, 100.0f, 100.0f, "%", 0.1f, 0.0f, 20));
        cat.modules.push_back(std::move(m));
    }
    {
        auto m = makeModule(ModuleId::gateA, ModuleType::gate, "Gate / Expander", "GATE", false);
        m.params.push_back(makeFloat(m.prefix, "threshold", "Threshold", -80.0f, 0.0f, -40.0f, "dB", 0.1f, 0.0f, 20));
        m.params.push_back(makeFloat(m.prefix, "range", "Range", -80.0f, 0.0f, -60.0f, "dB", 0.1f, 0.0f, 20));
        m.params.push_back(makeFloat(m.prefix, "attack", "Attack", 0.05f, 100.0f, 1.0f, "ms", 0.01f, 1.0f, 20));
        m.params.push_back(makeFloat(m.prefix, "hold", "Hold", 0.0f, 500.0f, 50.0f, "ms", 0.1f, 0.0f, 20));
        m.params.push_back(makeFloat(m.prefix, "release", "Release", 5.0f, 2000.0f, 200.0f, "ms", 1.0f, 200.0f, 20));
        m.params.push_back(makeFloat(m.prefix, "hysteresis", "Hysteresis", 0.0f, 12.0f, 3.0f, "dB", 0.1f, 0.0f, 20));
        m.params.push_back(makeFloat(m.prefix, "scHPF", "Sidechain HPF", 20.0f, 500.0f, 80.0f, "Hz", 1.0f, 100.0f, 20));
        m.params.push_back(makeFloat(m.prefix, "mix", "Mix", 0.0f, 100.0f, 100.0f, "%", 0.1f, 0.0f, 20));
        cat.modules.push_back(std::move(m));
    }
    {
        auto m = makeModule(ModuleId::transA, ModuleType::transient, "Transient Shaper", "TRANS", false);
        m.params.push_back(makeFloat(m.prefix, "attack", "Attack", -100.0f, 100.0f, 0.0f, "%", 0.1f, 0.0f, 20));
        m.params.push_back(makeFloat(m.prefix, "sustain", "Sustain", -100.0f, 100.0f, 0.0f, "%", 0.1f, 0.0f, 20));
        m.params.push_back(makeFloat(m.prefix, "sensitivity", "Sensitivity", 0.0f, 100.0f, 50.0f, "%", 0.1f, 0.0f, 20));
        m.params.push_back(makeFloat(m.prefix, "output", "Output", -12.0f, 12.0f, 0.0f, "dB", 0.01f, 0.0f, 20));
        m.params.push_back(makeFloat(m.prefix, "mix", "Mix", 0.0f, 100.0f, 100.0f, "%", 0.1f, 0.0f, 20));
        cat.modules.push_back(std::move(m));
    }
    for (auto id : { ModuleId::satA, ModuleId::satB })
    {
        auto m = makeModule(id, ModuleType::saturator, "Saturator", "SAT", false);
        m.params.push_back(makeChoice(m.prefix, "model", "Model", { "Tube", "Tape", "Diode", "Transistor" }, 0));
        m.params.push_back(makeFloat(m.prefix, "drive", "Drive", 0.0f, 24.0f, 6.0f, "dB", 0.01f, 0.0f, 20));
        m.params.push_back(makeFloat(m.prefix, "tone", "Tone", -12.0f, 12.0f, 0.0f, "dB", 0.01f, 0.0f, 20));
        m.params.push_back(makeFloat(m.prefix, "bias", "Bias", -0.5f, 0.5f, 0.1f, "", 0.001f, 0.0f, 20));
        m.params.push_back(makeBool(m.prefix, "autoGain", "Auto Gain", true));
        m.params.push_back(makeFloat(m.prefix, "output", "Output", -24.0f, 24.0f, 0.0f, "dB", 0.01f, 0.0f, 20));
        m.params.push_back(makeFloat(m.prefix, "mix", "Mix", 0.0f, 100.0f, 100.0f, "%", 0.1f, 0.0f, 20));
        cat.modules.push_back(std::move(m));
    }
    {
        auto m = makeModule(ModuleId::imgA, ModuleType::imager, "Stereo Imager", "IMAGE", false);
        m.params.push_back(makeFloat(m.prefix, "width", "Width", 0.0f, 200.0f, 100.0f, "%", 0.1f, 0.0f, 20));
        m.params.push_back(makeFloat(m.prefix, "mid", "Mid", -12.0f, 12.0f, 0.0f, "dB", 0.01f, 0.0f, 20));
        m.params.push_back(makeFloat(m.prefix, "side", "Side", -12.0f, 12.0f, 0.0f, "dB", 0.01f, 0.0f, 20));
        m.params.push_back(makeFloat(m.prefix, "bassMonoFreq", "Bass Mono Freq", 20.0f, 500.0f, 120.0f, "Hz", 1.0f, 100.0f, 20));
        m.params.push_back(makeFloat(m.prefix, "bassMonoAmount", "Bass Mono Amount", 0.0f, 100.0f, 100.0f, "%", 0.1f, 0.0f, 20));
        cat.modules.push_back(std::move(m));
    }
    {
        auto m = makeModule(ModuleId::revA, ModuleType::reverb, "Reverb", "VERB", false);
        m.params.push_back(makeFloat(m.prefix, "size", "Room Size", 0.0f, 100.0f, 50.0f, "%", 0.1f, 0.0f, 20));
        m.params.push_back(makeFloat(m.prefix, "decay", "Decay", 0.2f, 20.0f, 2.0f, "s", 0.01f, 2.0f, 30));
        m.params.push_back(makeFloat(m.prefix, "predelay", "Pre-Delay", 0.0f, 200.0f, 20.0f, "ms", 0.1f, 0.0f, 20));
        m.params.push_back(makeFloat(m.prefix, "damping", "Damping", 0.0f, 100.0f, 40.0f, "%", 0.1f, 0.0f, 20));
        m.params.push_back(makeFloat(m.prefix, "diffusion", "Diffusion", 0.0f, 100.0f, 70.0f, "%", 0.1f, 0.0f, 20));
        m.params.push_back(makeFloat(m.prefix, "width", "Width", 0.0f, 200.0f, 100.0f, "%", 0.1f, 0.0f, 20));
        m.params.push_back(makeFloat(m.prefix, "lowcut", "Low Cut", 20.0f, 500.0f, 80.0f, "Hz", 1.0f, 100.0f, 20));
        m.params.push_back(makeFloat(m.prefix, "highcut", "High Cut", 1000.0f, 20000.0f, 12000.0f, "Hz", 1.0f, 8000.0f, 20));
        m.params.push_back(makeFloat(m.prefix, "mix", "Mix", 0.0f, 100.0f, 25.0f, "%", 0.1f, 0.0f, 20));
        m.params.push_back(makeBool(m.prefix, "freeze", "Freeze", false));
        cat.modules.push_back(std::move(m));
    }
    {
        auto m = makeModule(ModuleId::dlyA, ModuleType::delay, "Stereo Delay", "DELAY", false);
        m.params.push_back(makeBool(m.prefix, "sync", "Tempo Sync", true));
        m.params.push_back(makeChoice(m.prefix, "division", "Division",
                                      { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/4 D", "1/8 D", "1/4 T", "1/8 T", "Free" }, 2));
        m.params.push_back(makeFloat(m.prefix, "timeL", "Time L", 1.0f, 2000.0f, 350.0f, "ms", 0.1f, 0.0f, 30));
        m.params.push_back(makeFloat(m.prefix, "timeR", "Time R", 1.0f, 2000.0f, 500.0f, "ms", 0.1f, 0.0f, 30));
        m.params.push_back(makeFloat(m.prefix, "feedback", "Feedback", 0.0f, 95.0f, 35.0f, "%", 0.1f, 0.0f, 20));
        m.params.push_back(makeBool(m.prefix, "pingpong", "Ping-Pong", false));
        m.params.push_back(makeFloat(m.prefix, "lowcut", "Low Cut", 20.0f, 2000.0f, 120.0f, "Hz", 1.0f, 200.0f, 20));
        m.params.push_back(makeFloat(m.prefix, "highcut", "High Cut", 1000.0f, 20000.0f, 8000.0f, "Hz", 1.0f, 8000.0f, 20));
        m.params.push_back(makeFloat(m.prefix, "mix", "Mix", 0.0f, 100.0f, 25.0f, "%", 0.1f, 0.0f, 20));
        cat.modules.push_back(std::move(m));
    }
    {
        auto m = makeModule(ModuleId::masterA, ModuleType::mastering, "Mastering", "MASTER", true);
        m.params.push_back(makeBool(m.prefix, "clipperOn", "Clipper", true));
        m.params.push_back(makeChoice(m.prefix, "clipperMode", "Clipper Mode", { "Soft", "Hard" }, 0));
        m.params.push_back(makeFloat(m.prefix, "drive", "Clipper Drive", 0.0f, 12.0f, 0.0f, "dB", 0.01f, 0.0f, 20));
        m.params.push_back(makeFloat(m.prefix, "ceiling", "Ceiling", -12.0f, 0.0f, -0.3f, "dBTP", 0.01f, 0.0f, 20));
        m.params.push_back(makeBool(m.prefix, "limiterOn", "Limiter", true));
        m.params.push_back(makeFloat(m.prefix, "release", "Limiter Release", 10.0f, 1000.0f, 100.0f, "ms", 1.0f, 100.0f, 20));
        cat.modules.push_back(std::move(m));
    }

    jassert(cat.modules.size() == static_cast<size_t>(kNumModules));
    return cat;
}

juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const auto& cat = catalog();

    auto addParam = [&layout](const ParamSpec& p)
    {
        const juce::ParameterID pid { p.id, 1 };
        switch (p.kind)
        {
            case ParamKind::boolean:
                layout.add(std::make_unique<juce::AudioParameterBool>(pid, p.name, p.defaultValue > 0.5f));
                break;
            case ParamKind::choice:
                layout.add(std::make_unique<juce::AudioParameterChoice>(pid, p.name, p.choices,
                                                                        static_cast<int>(p.defaultValue)));
                break;
            case ParamKind::continuous:
            default:
            {
                juce::NormalisableRange<float> range(p.minValue, p.maxValue, p.step);
                if (p.skewCentre > 0.0f)
                    range.setSkewForCentre(p.skewCentre);
                auto attrs = juce::AudioParameterFloatAttributes().withStringFromValueFunction(
                    [unit = p.unit](float value, int)
                    {
                        if (unit == "Hz")
                            return value >= 1000.0f ? juce::String(value / 1000.0f, 2) + " kHz"
                                                    : juce::String(value, 1) + " Hz";
                        if (unit == "%")
                            return juce::String(value, 1) + " %";
                        if (unit.isEmpty())
                            return juce::String(value, 2);
                        return juce::String(value, 2) + " " + unit;
                    });
                layout.add(std::make_unique<juce::AudioParameterFloat>(pid, p.name, range, p.defaultValue, attrs));
                break;
            }
        }
    };

    for (const auto& p : cat.globals)
        addParam(p);
    for (const auto& m : cat.modules)
        for (const auto& p : m.params)
            addParam(p);

    return layout;
}
} // namespace

const Catalog& catalog()
{
    static const Catalog instance = buildCatalog();
    return instance;
}

const ModuleSpec* Catalog::findModuleByPrefix(const juce::String& p) const noexcept
{
    for (const auto& m : modules)
        if (m.prefix == p)
            return &m;
    return nullptr;
}

const ParamSpec* Catalog::findParam(const juce::String& fullId) const noexcept
{
    for (const auto& p : globals)
        if (p.id == fullId)
            return &p;
    for (const auto& m : modules)
        for (const auto& p : m.params)
            if (p.id == fullId)
                return &p;
    return nullptr;
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    return makeLayout();
}

juce::String toString(ModuleId id)
{
    switch (id)
    {
        case ModuleId::utilA: return "utilA";
        case ModuleId::utilB: return "utilB";
        case ModuleId::eqA: return "eqA";
        case ModuleId::eqB: return "eqB";
        case ModuleId::compA: return "compA";
        case ModuleId::compB: return "compB";
        case ModuleId::mbA: return "mbA";
        case ModuleId::dsA: return "dsA";
        case ModuleId::gateA: return "gateA";
        case ModuleId::transA: return "transA";
        case ModuleId::satA: return "satA";
        case ModuleId::satB: return "satB";
        case ModuleId::imgA: return "imgA";
        case ModuleId::revA: return "revA";
        case ModuleId::dlyA: return "dlyA";
        case ModuleId::masterA: return "masterA";
    }
    return "utilA";
}

juce::String toString(ModuleType type)
{
    switch (type)
    {
        case ModuleType::utility: return "Utility";
        case ModuleType::eq: return "EQ";
        case ModuleType::compressor: return "Compressor";
        case ModuleType::multiband: return "Multiband";
        case ModuleType::deEsser: return "De-Esser";
        case ModuleType::gate: return "Gate";
        case ModuleType::transient: return "Transient";
        case ModuleType::saturator: return "Saturator";
        case ModuleType::imager: return "Imager";
        case ModuleType::reverb: return "Reverb";
        case ModuleType::delay: return "Delay";
        case ModuleType::mastering: return "Mastering";
    }
    return "Utility";
}

ModuleId moduleIdFromString(const juce::String& text, ModuleId fallback)
{
    for (auto id : allModuleIds())
        if (toString(id) == text)
            return id;
    return fallback;
}

ModuleType moduleTypeFromId(ModuleId id) noexcept
{
    return catalog().module(id).type;
}

juce::String instancePrefix(ModuleId id)
{
    return toString(id);
}

std::span<const ModuleId> allModuleIds()
{
    static const std::array<ModuleId, kNumModules> ids {
        ModuleId::utilA, ModuleId::utilB, ModuleId::eqA, ModuleId::eqB,
        ModuleId::compA, ModuleId::compB, ModuleId::mbA, ModuleId::dsA,
        ModuleId::gateA, ModuleId::transA, ModuleId::satA, ModuleId::satB,
        ModuleId::imgA, ModuleId::revA, ModuleId::dlyA, ModuleId::masterA
    };
    return ids;
}

std::array<ModuleId, kNumModules> defaultRackOrder()
{
    std::array<ModuleId, kNumModules> order {};
    const auto ids = allModuleIds();
    std::copy(ids.begin(), ids.end(), order.begin());
    // Put the mastering module at the end of the default chain.
    for (int i = 0; i < kNumModules; ++i)
        if (order[static_cast<size_t>(i)] == ModuleId::masterA)
        {
            for (int j = i; j < kNumModules - 1; ++j)
                order[static_cast<size_t>(j)] = order[static_cast<size_t>(j + 1)];
            order[kNumModules - 1] = ModuleId::masterA;
            break;
        }
    return order;
}

std::atomic<float>* rawParam(juce::AudioProcessorValueTreeState& state, const juce::String& id)
{
    return state.getRawParameterValue(id);
}

void SmoothedParam::bind(juce::AudioProcessorValueTreeState& state, const juce::String& id) noexcept
{
    source = rawParam(state, id);
    jassert(source != nullptr);
}

void SmoothedParam::prepare(double sampleRate, double rampMs, float initialValue) noexcept
{
    if (source != nullptr)
        initialValue = source->load(std::memory_order_relaxed);
    if (!std::isfinite(initialValue))
        initialValue = 0.0f;
    smoother.reset(sampleRate, juce::jmax(0.001, rampMs / 1000.0));
    smoother.setCurrentAndTargetValue(initialValue);
    lastTarget = initialValue;
}

void SmoothedParam::updateTarget() noexcept
{
    if (source == nullptr)
        return;
    const float t = source->load(std::memory_order_relaxed);
    if (!std::isfinite(t))
        return;
    if (std::abs(t - lastTarget) > 1.0e-7f)
    {
        lastTarget = t;
        smoother.setTargetValue(t);
    }
}

void SmoothedParam::snap(float value) noexcept
{
    if (!std::isfinite(value))
        value = 0.0f;
    smoother.setCurrentAndTargetValue(value);
    lastTarget = value;
}

void ParamRef::bind(juce::AudioProcessorValueTreeState& state, const juce::String& id) noexcept
{
    source = rawParam(state, id);
    jassert(source != nullptr);
}
} // namespace aurora
