#include "Utility.h"

namespace aurora
{
UtilityModule::UtilityModule(ModuleId id, juce::AudioProcessorValueTreeState& state)
    : ModuleBase(id)
{
    const auto prefix = instancePrefix(id);
    gain.bind(state, prefix + ".gain");
    pan.bind(state, prefix + ".pan");
    polarityL.bind(state, prefix + ".polarityL");
    polarityR.bind(state, prefix + ".polarityR");
    mono.bind(state, prefix + ".mono");
    swap.bind(state, prefix + ".swap");
}

void UtilityModule::prepare(double newSampleRate, int, int numChannels)
{
    sampleRate = newSampleRate;
    channels = juce::jlimit(1, 2, numChannels);
    gain.prepare(sampleRate, 20.0, 0.0f);
    pan.prepare(sampleRate, 20.0, 0.0f);
}

void UtilityModule::reset() noexcept
{
    gain.snap(gain.target());
    pan.snap(pan.target());
    outputLevel.store(-120.0f, std::memory_order_relaxed);
}

void UtilityModule::process(juce::AudioBuffer<float>& buffer) noexcept
{
    gain.updateTarget();
    pan.updateTarget();

    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin(channels, buffer.getNumChannels());
    if (numCh <= 0)
        return;

    const bool flipL = polarityL.getBool();
    const bool flipR = polarityR.getBool();
    const bool toMono = mono.getBool();
    const bool swapChannels = swap.getBool();

    auto* l = buffer.getWritePointer(0);
    auto* r = numCh > 1 ? buffer.getWritePointer(1) : nullptr;

    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float p = pan.next();
        const float g = dsp::dbToGain(gain.next());
        const float gL = g * (p <= 0.0f ? 1.0f : 1.0f - p);
        const float gR = g * (p >= 0.0f ? 1.0f : 1.0f + p);

        float xl = l[i] * gL;
        float xr = r != nullptr ? r[i] * gR : 0.0f;

        if (toMono)
        {
            const float m = (xl + xr) * 0.5f;
            xl = xr = m;
        }

        if (flipL) xl = -xl;
        if (flipR) xr = -xr;

        if (swapChannels)
            std::swap(xl, xr);

        l[i] = dsp::sanitize(xl);
        if (r != nullptr)
            r[i] = dsp::sanitize(xr);

        peak = juce::jmax(peak, std::abs(l[i]), r != nullptr ? std::abs(r[i]) : 0.0f);
    }

    outputLevel.store(dsp::gainToDb(peak), std::memory_order_relaxed);
}
} // namespace aurora
