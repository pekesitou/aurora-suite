#include "RackEngine.h"

#include "DSP/Compressor.h"
#include "DSP/DynamicsCore.h"
#include "DSP/MasteringStage.h"
#include "DSP/MultibandComp.h"
#include "DSP/ParametricEQ.h"
#include "DSP/Saturator.h"
#include "DSP/SpatialFX.h"
#include "DSP/Utility.h"

#include <algorithm>

namespace aurora
{
namespace
{
std::unique_ptr<ModuleBase> createModule(ModuleId id, juce::AudioProcessorValueTreeState& state)
{
    switch (moduleTypeFromId(id))
    {
        case ModuleType::utility: return std::make_unique<UtilityModule>(id, state);
        case ModuleType::eq: return std::make_unique<ParametricEQ>(id, state);
        case ModuleType::compressor: return std::make_unique<CompressorModule>(id, state);
        case ModuleType::multiband: return std::make_unique<MultibandComp>(id, state);
        case ModuleType::deEsser: return std::make_unique<DeEsserModule>(id, state);
        case ModuleType::gate: return std::make_unique<GateModule>(id, state);
        case ModuleType::transient: return std::make_unique<TransientModule>(id, state);
        case ModuleType::saturator: return std::make_unique<SaturatorModule>(id, state);
        case ModuleType::imager: return std::make_unique<StereoImagerModule>(id, state);
        case ModuleType::reverb: return std::make_unique<ReverbModule>(id, state);
        case ModuleType::delay: return std::make_unique<DelayModule>(id, state);
        case ModuleType::mastering: return std::make_unique<MasteringStage>(id, state);
    }
    return nullptr;
}
} // namespace

RackEngine::RackEngine(juce::AudioProcessorValueTreeState& state)
    : apvts(state)
{
    order = defaultRackOrder();
    pendingOrder = order;

    for (auto id : allModuleIds())
    {
        auto& slot = slots[static_cast<size_t>(id)];
        slot.module = createModule(id, apvts);
        slot.activeParam.bind(apvts, instancePrefix(id) + ".active");
        jassert(slot.module != nullptr);
    }
}

RackEngine::~RackEngine() = default;

void RackEngine::prepare(double newSampleRate, int maxBlockSize, int numChannels)
{
    sampleRate = newSampleRate;
    maxBlock = juce::jmax(16, maxBlockSize);
    channels = juce::jlimit(1, 2, numChannels);
    totalLatency = 0;

    for (auto id : allModuleIds())
    {
        auto& slot = slots[static_cast<size_t>(id)];
        if (slot.module == nullptr)
            continue;

        slot.module->prepare(sampleRate, maxBlock, channels);
        slot.latency = juce::jmax(0, slot.module->latencySamples());
        totalLatency += slot.latency;

        slot.scratch.setSize(channels, maxBlock);
        slot.scratch.clear();

        const int capacity = juce::jmax(1, slot.latency);
        slot.latencyStorage.setSize(channels, capacity);
        slot.latencyStorage.clear();
        slot.latencyWritePos = 0;

        slot.lastTarget = slot.activeParam.getBool();
        slot.state = slot.lastTarget ? SlotState::active : SlotState::inactive;
        slot.fadeGain = slot.lastTarget ? 1.0f : 0.0f;
        slot.primingRemaining = 0;
    }

    orderFade = 1.0f;
    orderFadingOut = false;
    hasPendingOrder = false;
    pendingOrder = order;
    commandRead.store(0, std::memory_order_relaxed);
    commandWrite.store(0, std::memory_order_relaxed);
}

void RackEngine::reset() noexcept
{
    resetRequested.store(true, std::memory_order_release);
}

void RackEngine::setTempo(double bpm, bool isPlaying) noexcept
{
    tempo.store(juce::jlimit(20.0, 300.0, bpm), std::memory_order_relaxed);
    playing.store(isPlaying, std::memory_order_relaxed);
}

std::span<const ModuleId> RackEngine::currentOrder() const noexcept
{
    return order;
}

ModuleBase* RackEngine::getModule(ModuleId id) noexcept
{
    return slots[static_cast<size_t>(id)].module.get();
}

const ModuleBase* RackEngine::getModule(ModuleId id) const noexcept
{
    return slots[static_cast<size_t>(id)].module.get();
}

bool RackEngine::requestOrder(std::span<const ModuleId> newOrder)
{
    if (newOrder.size() != kNumModules)
        return false;

    const auto w = commandWrite.load(std::memory_order_relaxed);
    const auto r = commandRead.load(std::memory_order_acquire);
    if (w - r >= static_cast<uint32_t>(commandCapacity))
        return false;

    OrderCommand cmd;
    std::copy(newOrder.begin(), newOrder.end(), cmd.order.begin());
    commands[static_cast<size_t>(w % static_cast<uint32_t>(commandCapacity))] = cmd;
    commandWrite.store(w + 1, std::memory_order_release);
    return true;
}

bool RackEngine::popCommand(OrderCommand& out) noexcept
{
    const auto r = commandRead.load(std::memory_order_relaxed);
    const auto w = commandWrite.load(std::memory_order_acquire);
    if (r == w)
        return false;
    out = commands[static_cast<size_t>(r % static_cast<uint32_t>(commandCapacity))];
    commandRead.store(r + 1, std::memory_order_release);
    return true;
}

void RackEngine::applyCommands() noexcept
{
    OrderCommand cmd;
    while (popCommand(cmd))
    {
        pendingOrder = cmd.order;
        hasPendingOrder = true;
        orderFadingOut = true;
    }
}

float RackEngine::delaySample(Slot& slot, int channel, float input) noexcept
{
    if (slot.latency <= 0)
        return input;

    const int cap = slot.latencyStorage.getNumSamples();
    int readPos = slot.latencyWritePos;
    if (readPos >= cap)
        readPos = 0;
    const float delayed = slot.latencyStorage.getSample(channel, readPos);
    slot.latencyStorage.setSample(channel, readPos, dsp::sanitize(input));
    return delayed;
}

void RackEngine::processSlot(Slot& slot, juce::AudioBuffer<float>& buffer, int numChannels) noexcept
{
    if (slot.module == nullptr)
        return;

    const int n = buffer.getNumSamples();
    const bool target = slot.activeParam.getBool();

    if (target && slot.state == SlotState::inactive)
    {
        slot.module->reset();
        slot.fadeGain = 0.0f;
        if (slot.latency > 0)
        {
            slot.state = SlotState::priming;
            slot.primingRemaining = slot.latency;
        }
        else
        {
            slot.state = SlotState::fadeIn;
        }
    }
    else if (!target && (slot.state == SlotState::active || slot.state == SlotState::fadeIn))
    {
        slot.state = SlotState::fadeOut;
    }
    else if (target && slot.state == SlotState::fadeOut)
    {
        slot.state = SlotState::fadeIn;
    }
    else if (!target && slot.state == SlotState::priming)
    {
        slot.state = SlotState::inactive;
        slot.module->reset();
        slot.primingRemaining = 0;
    }

    const bool needsWet = slot.state != SlotState::inactive;
    const bool needsDry = slot.state != SlotState::active;

    if (needsWet && needsDry)
    {
        jassert(slot.scratch.getNumSamples() >= n);
        for (int ch = 0; ch < numChannels; ++ch)
            slot.scratch.copyFrom(ch, 0, buffer, ch, 0, n);
        slot.module->process(slot.scratch);
    }

    if (slot.state == SlotState::active)
    {
        // Feed the latency line so a future bypass is aligned.
        for (int i = 0; i < n; ++i)
        {
            for (int ch = 0; ch < numChannels; ++ch)
                delaySample(slot, ch, buffer.getSample(ch, i));
            if (slot.latency > 0)
            {
                const int cap = slot.latencyStorage.getNumSamples();
                if (++slot.latencyWritePos >= cap)
                    slot.latencyWritePos = 0;
            }
        }
        slot.module->process(buffer);
        slot.lastTarget = true;
        return;
    }

    const float fadeStep = static_cast<float>(1.0 / juce::jmax(1.0, sampleRate * 0.005));

    for (int i = 0; i < n; ++i)
    {
        float dry[2] { 0.0f, 0.0f };
        for (int ch = 0; ch < numChannels; ++ch)
            dry[ch] = delaySample(slot, ch, buffer.getSample(ch, i));

        if (slot.latency > 0)
        {
            const int cap = slot.latencyStorage.getNumSamples();
            if (++slot.latencyWritePos >= cap)
                slot.latencyWritePos = 0;
        }

        if (slot.state == SlotState::priming)
        {
            for (int ch = 0; ch < numChannels; ++ch)
                buffer.setSample(ch, i, dsp::sanitize(dry[ch]));
        }
        else if (slot.state == SlotState::fadeIn)
        {
            slot.fadeGain = juce::jmin(1.0f, slot.fadeGain + fadeStep);
            const float g = slot.fadeGain;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float wet = slot.scratch.getSample(ch, i);
                buffer.setSample(ch, i, dsp::sanitize(dry[ch] + g * (wet - dry[ch])));
            }
        }
        else if (slot.state == SlotState::fadeOut)
        {
            slot.fadeGain = juce::jmax(0.0f, slot.fadeGain - fadeStep);
            const float g = slot.fadeGain;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float wet = slot.scratch.getSample(ch, i);
                buffer.setSample(ch, i, dsp::sanitize(dry[ch] + g * (wet - dry[ch])));
            }
        }
        else // inactive
        {
            for (int ch = 0; ch < numChannels; ++ch)
                buffer.setSample(ch, i, dsp::sanitize(dry[ch]));
        }
    }

    if (slot.state == SlotState::priming)
    {
        slot.primingRemaining -= n;
        if (slot.primingRemaining <= 0)
        {
            slot.state = SlotState::fadeIn;
            slot.fadeGain = 0.0f;
        }
    }
    else if (slot.state == SlotState::fadeIn && slot.fadeGain >= 1.0f)
    {
        slot.state = SlotState::active;
    }
    else if (slot.state == SlotState::fadeOut && slot.fadeGain <= 0.0f)
    {
        slot.state = SlotState::inactive;
        slot.module->reset();
    }

    slot.lastTarget = target;
}

void RackEngine::applyOrderFade(juce::AudioBuffer<float>& buffer, float from, float to) noexcept
{
    const int n = buffer.getNumSamples();
    if (n <= 0)
        return;
    const int numCh = juce::jmin(channels, buffer.getNumChannels());
    for (int i = 0; i < n; ++i)
    {
        const float g = from + (to - from) * (static_cast<float>(i + 1) / static_cast<float>(n));
        for (int ch = 0; ch < numCh; ++ch)
            buffer.setSample(ch, i, buffer.getSample(ch, i) * g);
    }
}

void RackEngine::process(juce::AudioBuffer<float>& buffer) noexcept
{
    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin(channels, buffer.getNumChannels());
    if (n <= 0 || numCh <= 0)
        return;

    if (resetRequested.exchange(false, std::memory_order_acq_rel))
    {
        for (auto& slot : slots)
        {
            if (slot.module != nullptr)
                slot.module->reset();
            slot.latencyStorage.clear();
            slot.latencyWritePos = 0;
            slot.state = slot.activeParam.getBool() ? SlotState::active : SlotState::inactive;
            slot.fadeGain = slot.state == SlotState::active ? 1.0f : 0.0f;
            slot.primingRemaining = 0;
        }
    }

    applyCommands();

    const auto bpm = tempo.load(std::memory_order_relaxed);
    const bool isPlaying = playing.load(std::memory_order_relaxed);

    for (auto id : order)
    {
        auto& slot = slots[static_cast<size_t>(id)];
        if (slot.module != nullptr)
            slot.module->setTempo(bpm, isPlaying);
        processSlot(slot, buffer, numCh);
    }

    if (hasPendingOrder)
    {
        const float ramp = static_cast<float>(juce::jmax(1, static_cast<int>(sampleRate * 0.005)));
        if (orderFadingOut)
        {
            const float from = orderFade;
            const float to = juce::jmax(0.0f, orderFade - static_cast<float>(n) / ramp);
            applyOrderFade(buffer, from, to);
            orderFade = to;
            if (orderFade <= 0.0f)
            {
                order = pendingOrder;
                hasPendingOrder = false;
                orderFadingOut = false;
            }
        }
        else
        {
            const float from = orderFade;
            const float to = juce::jmin(1.0f, orderFade + static_cast<float>(n) / ramp);
            applyOrderFade(buffer, from, to);
            orderFade = to;
            if (orderFade >= 1.0f)
                hasPendingOrder = false;
        }
    }
    else if (orderFade < 1.0f)
    {
        const float ramp = static_cast<float>(juce::jmax(1, static_cast<int>(sampleRate * 0.005)));
        const float from = orderFade;
        const float to = juce::jmin(1.0f, orderFade + static_cast<float>(n) / ramp);
        applyOrderFade(buffer, from, to);
        orderFade = to;
    }
}
} // namespace aurora
