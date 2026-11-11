#include "instruments/drums/drumkit.h"

#include "instruments/sampler/sampler.h"

DrumKit::DrumKit(std::shared_ptr<drumkit::Library> library)
    : m_sampler(std::make_shared<Sampler>()), m_library(std::move(library))
{
    m_sampler->setPolyphony(Polyphony);
    if (m_library) m_library->request(m_sampler, m_settings);
}

DrumKit::~DrumKit()
{
    if (m_library) m_library->trimSoon();                  // this track's samples may go
}

void DrumKit::prepare(double sampleRate, int maxFrames)
{
    m_sampler->prepare(sampleRate, maxFrames);
}

void DrumKit::reset() noexcept
{
    m_sampler->reset();
}

void DrumKit::process(ProcessContext& context) noexcept
{
    if (context.midi) {
        for (const auto& event : context.midi->events()) {
            if ((event.status & 0xF0) != 0x90 || event.data2 == 0) continue;
            const auto key = std::size_t(event.data1 & 0x7F);
            m_hitVelocity[key].store(event.data2, std::memory_order_relaxed);
            m_hitCount[key].fetch_add(1, std::memory_order_release);
        }
    }
    m_sampler->process(context);
}

DrumKit::Hits DrumKit::hits(int key) const noexcept
{
    if (key < 0 || key > 127) return {};
    const auto count = m_hitCount[std::size_t(key)].load(std::memory_order_acquire);
    return {count, m_hitVelocity[std::size_t(key)].load(std::memory_order_relaxed)};
}

void DrumKit::setSettings(const drumkit::Settings& input)
{
    const auto settings = drumkit::sanitize(input);
    {
        std::lock_guard lock(m_mutex);
        if (settings == m_settings) return;
        m_settings = settings;
    }
    if (m_library) m_library->request(m_sampler, settings);
}

drumkit::Settings DrumKit::settings() const
{
    std::lock_guard lock(m_mutex);
    return m_settings;
}
