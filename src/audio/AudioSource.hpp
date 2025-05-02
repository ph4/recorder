//
// Created by pavel on 13.12.2024.
//
#ifndef AUDIO_AUDIO_SOURCE_H
#define AUDIO_AUDIO_SOURCE_H

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <variant>

namespace recorder::audio {
    struct AudioFormat {
        uint16_t channels;
        uint32_t sampleRate;
    };

    template <typename S> class AudioSource {
      public:
        using CallBackT = std::function<void(std::span<S>)>;
        virtual ~AudioSource() = default;

        enum State {
            Uninitialized,
            Error,
            Playing,
            Stopped,
        };

        virtual State GetState() = 0;

        virtual void SetCallback(CallBackT) = 0;

        virtual void Play() = 0;

        virtual void Stop() = 0;
    };

    template <typename S> class ProcessAudioSource : public AudioSource<S> {
      protected:
      public:
        virtual uint32_t GetPid() = 0;
    };

    template <typename T> class IAudioSourceFactory {
      public:
        virtual ~IAudioSourceFactory() = default;

        virtual std::variant<std::unique_ptr<T>, std::string> Create(const AudioFormat &format) = 0;
    };

    template <typename T> class IProcessAudioSourceFactory {
      public:
        virtual ~IProcessAudioSourceFactory() = default;

        virtual std::variant<std::unique_ptr<T>, std::string> Create(
              const AudioFormat &format, uint32_t pid
        ) = 0;
    };
} // namespace recorder::audio
#endif
