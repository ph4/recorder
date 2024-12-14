//
// Created by pavel on 13.12.2024.
//

module;

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <optional>
#include <variant>


export module IAudioSource;

namespace recorder {
    export struct AudioFormat {
        uint16_t channels;
        uint32_t sampleRate;
    };

    export template<typename S>
    class IAudioSource {
    public:
        using CallBackT = std::function<void(std::span<S>)>;

        virtual ~IAudioSource() = default;

        enum State {
            Uninitialized,
            Error,
            Playing,
            Silent,
            Stopped,
        };

        // std::optional<std::string> GetErrorMessage() {};
        virtual State GetState() = 0;

        virtual void SetCallback(CallBackT) = 0;

        virtual void Play() = 0;

        virtual void Stop() = 0;
    };

    export template<typename S>
    class IProcessAudioSource : public IAudioSource<S> {
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
