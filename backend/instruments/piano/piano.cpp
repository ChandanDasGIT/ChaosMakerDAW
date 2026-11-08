#include "instruments/piano/piano.h"

#include "instruments/sampler/sampler.h"

Piano::Piano(std::shared_ptr<piano::Library> library)
    : m_sampler(std::make_shared<Sampler>()), m_library(std::move(library))
{
    m_sampler->setPolyphony(Polyphony);
    if (m_library) m_library->request(m_sampler, m_settings);
}

Piano::~Piano() = default;

void Piano::prepare(double sampleRate, int maxFrames)
{
    m_sampler->prepare(sampleRate, maxFrames);
}

void Piano::reset() noexcept
{
    m_sampler->reset();
    for (auto& key : m_down) key.store(0, std::memory_order_relaxed);
    m_pedal.store(0, std::memory_order_relaxed);
}

void Piano::process(ProcessContext& context) noexcept
{
    if (context.midi) {
        for (const auto& event : context.midi->events()) {
            const int type = event.status & 0xF0;
            const auto key = std::size_t(event.data1 & 0x7F);
            if (type == 0x90 && event.data2 > 0) {
                m_down[key].store(event.data2, std::memory_order_relaxed);
                m_presses[key].fetch_add(1, std::memory_order_release);
            } else if (type == 0x80 || type == 0x90) {
                m_down[key].store(0, std::memory_order_relaxed);
            } else if (type == 0xB0 && event.data1 == 64) {
                m_pedal.store(event.data2 & 0x7F, std::memory_order_relaxed);
            } else if (type == 0xB0 && (event.data1 == 123 || event.data1 == 120)) {
                for (auto& down : m_down) down.store(0, std::memory_order_relaxed);
            }
        }
    }
    m_sampler->process(context);
}

bool Piano::keyDown(int key) const noexcept
{
    return keyVelocity(key) > 0;
}

int Piano::keyVelocity(int key) const noexcept
{
    if (key < 0 || key > 127) return 0;
    return m_down[std::size_t(key)].load(std::memory_order_relaxed);
}

std::uint32_t Piano::presses(int key) const noexcept
{
    if (key < 0 || key > 127) return 0;
    return m_presses[std::size_t(key)].load(std::memory_order_acquire);
}

void Piano::setSettings(const piano::Settings& input)
{
    const auto settings = piano::sanitize(input);
    {
        std::lock_guard lock(m_mutex);
        if (settings == m_settings) return;
        m_settings = settings;
    }
    if (m_library) m_library->request(m_sampler, settings);
}

piano::Settings Piano::settings() const
{
    std::lock_guard lock(m_mutex);
    return m_settings;
}
