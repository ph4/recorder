#pragma once

#include <optional>
#include <span>
#include <string>

namespace recorder::audio {

struct AudioFormat {
    uint16_t channels;
    uint32_t sampleRate;
};

template <typename S> class IAudioSource {
public:
    virtual const AudioFormat& GetFormat() const = 0;
};

template <typename S> class IAudioSink {
public:
    virtual void OnNewPacket(std::span<S>) = 0;
    virtual ~IAudioSink() = default;
};

template <typename S> class IActivityListener {
public:
    virtual void OnInactive() = 0;
    virtual void OnActive(std::optional<std::string> metadata = std::nullopt) = 0;
    virtual ~IActivityListener() = default;
};

} // namespace recorder::audio
