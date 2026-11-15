#include "PluginProcessor.h"

#include "AI/ARIAAnalyzer.h"
#include "Core/ThemeManager.h"
#include "PluginEditor.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace aurora
{
namespace
{
constexpr const char* kStateVersion = "stateVersion";
constexpr const char* kRackOrder = "rackOrder";
constexpr const char* kThemeId = "uiThemeId";

juce::String serializeOrder(const std::array<ModuleId, kNumModules>& order)
{
    juce::String result;
    for (size_t i = 0; i < order.size(); ++i)
    {
        if (i > 0)
            result << ",";
        result << toString(order[i]);
    }
    return result;
}

bool parseOrder(const juce::String& text, std::array<ModuleId, kNumModules>& out)
{
    auto tokens = juce::StringArray::fromTokens(text, ",", "");
    if (tokens.size() != kNumModules)
        return false;

    std::array<bool, kNumModules> seen {};
    for (int i = 0; i < kNumModules; ++i)
    {
        const auto token = tokens[i].trim();
        bool found = false;
        for (auto id : allModuleIds())
        {
            if (toString(id) == token)
            {
                const auto index = static_cast<size_t>(id);
                if (seen[index])
                    return false;
                seen[index] = true;
                out[static_cast<size_t>(i)] = id;
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }
    return true;
}
} // namespace

AuroraProcessor::AuroraProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout()),
      rack(apvts)
{
    inputTrim.bind(apvts, "global.inputTrim");
    outputTrim.bind(apvts, "global.outputTrim");

    themeManager = std::make_unique<ThemeManager>();
    aria = std::make_unique<ARIAAnalyzer>(*this);

    uiOrder = defaultRackOrder();
    apvts.state.setProperty(kStateVersion, 1, nullptr);
    apvts.state.setProperty(kRackOrder, serializeOrder(uiOrder), nullptr);
    apvts.state.setProperty(kThemeId, themeId, nullptr);
}

AuroraProcessor::~AuroraProcessor()
{
    if (aria != nullptr)
        aria->stop();
    themeManager.reset();
    aria.reset();
}

void AuroraProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    maxBlockPrepared = juce::jmax(16, samplesPerBlock);
    numChannelsPrepared = juce::jlimit(1, 2, getTotalNumOutputChannels());

    rack.prepare(sampleRate, maxBlockPrepared, numChannelsPrepared);
    inputTrim.prepare(sampleRate, 20.0, 0.0f);
    outputTrim.prepare(sampleRate, 20.0, 0.0f);

    reportedLatency = rack.latencySamples();
    setLatencySamples(reportedLatency);

    if (aria != nullptr)
        aria->prepare(sampleRate, maxBlockPrepared, numChannelsPrepared);

    inputPeak.store(-120.0f, std::memory_order_relaxed);
    outputPeak.store(-120.0f, std::memory_order_relaxed);
}

void AuroraProcessor::releaseResources()
{
    if (aria != nullptr)
        aria->stop();
}

bool AuroraProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    return (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo())
        && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
        && in.size() == out.size();
}

void AuroraProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    juce::ScopedNoDenormals noDenormals;

    const int numCh = buffer.getNumChannels();
    for (int ch = 2; ch < numCh; ++ch)
        buffer.clear(ch, 0, buffer.getNumSamples());

    if (buffer.getNumSamples() == 0)
        return;

    if (auto* playHead = getPlayHead())
    {
        if (const auto position = playHead->getPosition())
        {
            double bpm = 120.0;
            if (const auto tempo = position->getBpm())
                bpm = *tempo;
            rack.setTempo(bpm, position->isPlaying);
        }
    }

    const int total = buffer.getNumSamples();
    for (int offset = 0; offset < total; offset += maxBlockPrepared)
    {
        const int count = juce::jmin(maxBlockPrepared, total - offset);
        juce::AudioBuffer<float> view(buffer.getArrayOfWritePointers(), numCh, offset, count);
        processChunk(view);
    }
}

void AuroraProcessor::processChunk(juce::AudioBuffer<float>& buffer) noexcept
{
    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin(buffer.getNumChannels(), numChannelsPrepared);
    if (n <= 0 || numCh <= 0)
        return;

    inputTrim.updateTarget();
    outputTrim.updateTarget();

    float peakIn = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float g = dsp::dbToGain(inputTrim.next());
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float x = dsp::sanitize(buffer.getSample(ch, i) * g);
            buffer.setSample(ch, i, x);
            peakIn = juce::jmax(peakIn, std::abs(x));
        }
    }
    inputPeak.store(dsp::gainToDb(peakIn), std::memory_order_relaxed);

    rack.process(buffer);

    float peakOut = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float g = dsp::dbToGain(outputTrim.next());
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float x = dsp::sanitize(buffer.getSample(ch, i) * g);
            buffer.setSample(ch, i, x);
            peakOut = juce::jmax(peakOut, std::abs(x));
        }
    }
    outputPeak.store(dsp::gainToDb(peakOut), std::memory_order_relaxed);
}

juce::AudioProcessorEditor* AuroraProcessor::createEditor()
{
    return new AuroraEditor(*this);
}

ThemeManager& AuroraProcessor::getThemeManager() noexcept
{
    jassert(themeManager != nullptr);
    return *themeManager;
}

void AuroraProcessor::setThemeId(const juce::String& id)
{
    themeId = id;
    if (themeManager != nullptr)
        themeManager->setTheme(id);
    apvts.state.setProperty(kThemeId, themeId, nullptr);
}

juce::String AuroraProcessor::getThemeId() const
{
    return themeId;
}

std::array<ModuleId, kNumModules> AuroraProcessor::getRackOrder() const
{
    return uiOrder;
}

void AuroraProcessor::setRackOrder(std::span<const ModuleId> newOrder)
{
    if (newOrder.size() != kNumModules)
        return;

    std::array<bool, kNumModules> seen {};
    for (auto id : newOrder)
    {
        const auto index = static_cast<size_t>(id);
        if (seen[index])
            return;
        seen[index] = true;
    }

    std::copy(newOrder.begin(), newOrder.end(), uiOrder.begin());
    rack.requestOrder(uiOrder);
    updateRackOrderProperty();
}

void AuroraProcessor::setModuleActive(ModuleId id, bool shouldBeActive)
{
    const auto paramId = instancePrefix(id) + ".active";
    if (auto* p = apvts.getParameter(paramId))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(shouldBeActive ? 1.0f : 0.0f);
        p->endChangeGesture();
    }
}

bool AuroraProcessor::isModuleActive(ModuleId id) const
{
    if (auto* p = apvts.getRawParameterValue(instancePrefix(id) + ".active"))
        return p->load(std::memory_order_relaxed) > 0.5f;
    return false;
}

void AuroraProcessor::updateRackOrderProperty()
{
    apvts.state.setProperty(kRackOrder, serializeOrder(uiOrder), nullptr);
}

void AuroraProcessor::loadRackOrderFromState(const juce::ValueTree& state)
{
    std::array<ModuleId, kNumModules> parsed = defaultRackOrder();
    if (parseOrder(state.getProperty(kRackOrder, "").toString(), parsed))
        uiOrder = parsed;
    else
        uiOrder = defaultRackOrder();

    rack.requestOrder(uiOrder);
    updateRackOrderProperty();
}

void AuroraProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty(kStateVersion, 1, nullptr);
    state.setProperty(kRackOrder, serializeOrder(uiOrder), nullptr);
    state.setProperty(kThemeId, themeId, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
    else
        destData.reset();
}

void AuroraProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || !xml->hasTagName(apvts.state.getType()))
    {
        lastStateError = "Invalid preset XML";
        return;
    }

    auto newState = juce::ValueTree::fromXml(*xml);
    if (!newState.isValid())
    {
        lastStateError = "Invalid preset tree";
        return;
    }

    const int version = static_cast<int>(newState.getProperty(kStateVersion, 1));
    if (version > 1)
    {
        lastStateError = "Preset was created by a newer AURORA version";
        return;
    }

    apvts.replaceState(newState);

    const auto loadedTheme = newState.getProperty(kThemeId, themeId).toString();
    if (loadedTheme.isNotEmpty())
    {
        themeId = loadedTheme;
        if (themeManager != nullptr)
            themeManager->setTheme(themeId);
    }

    loadRackOrderFromState(apvts.state);
    lastStateError.clear();
}

juce::String AuroraProcessor::getLastStateError() const
{
    return lastStateError;
}

void AuroraProcessor::requestAriaAnalysis()
{
    if (aria != nullptr)
        aria->requestAnalysis();
}

bool AuroraProcessor::isAriaAnalysisRunning() const noexcept
{
    return aria != nullptr && aria->isRunning();
}
} // namespace aurora
