//
// Created by pavel on 13.12.2024.
//

module;

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <variant>

export module AudioSource;

namespace recorder::audio {
    export struct AudioFormat {
        uint16_t channels;
        uint32_t sampleRate;
    };


    export template<typename S>
    class AudioSource {
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

    export template<typename S>
    class ProcessAudioSource : public AudioSource<S> {
    protected:

    public:
        virtual uint32_t GetPid() = 0;
    };

    export template<typename T>
    class IAudioSourceFactory {
    public:
        virtual ~IAudioSourceFactory() = default;

        virtual std::variant<std::unique_ptr<T>, std::string> Create(const AudioFormat &format) = 0;
    };

    export template<typename T>
    class IProcessAudioSourceFactory {
    public:
        virtual ~IProcessAudioSourceFactory() = default;

        virtual std::variant<std::unique_ptr<T>, std::string> Create(const AudioFormat &format, uint32_t pid) = 0;
    };
}
