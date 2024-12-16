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

#include <chrono>


export module AudioSource;

namespace recorder::audio {
    export struct AudioFormat {
        uint16_t channels;
        uint32_t sampleRate;
    };

    export struct AudioSourceActivation {
        std::chrono::time_point<std::chrono::utc_clock> timestamp;
        std::string activationSource;
        /**
         * JSON metadata
         */
        std::optional<std::string> metadata;
    };

    export struct AudioSourceDeactivation {
    };

    export template<typename S>
    class AudioActivityStatus {
    protected:
        const std::function<void(AudioSourceActivation)> onActivated;
        const std::function<void(AudioSourceDeactivation)> onDeactivated;

        AudioActivityStatus(const std::function<void(AudioSourceActivation)> &onActivated,
                            const std::function<void(AudioSourceDeactivation)> &onDeactivated)
            : onActivated(onActivated), onDeactivated(onDeactivated) {
        }

    public:
        virtual ~AudioActivityStatus() = default;

        virtual bool isActive() = 0;

        virtual bool checkNewAudio(std::span<S>) = 0;
    };

    export template<typename S>
    class AudioSource {
    protected:
        std::unique_ptr<AudioActivityStatus<S> > activationChecker;

        explicit AudioSource(std::unique_ptr<AudioActivityStatus<S> > activationChecker)
            : activationChecker(std::move(activationChecker)) {
        }

        using CallBackT = std::function<void(std::span<S>)>;

    public:
        AudioSource(const AudioSource &source) = delete;

        AudioSource &operator=(const AudioSource &source) = delete;

        AudioSource(AudioSource &&source) = default;

        AudioSource &operator=(AudioSource &&source) = default;

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
        using AudioSource<S>::AudioSource;

    public:
        ProcessAudioSource(const ProcessAudioSource &source) = delete;

        ProcessAudioSource &operator=(const ProcessAudioSource &source) = delete;

        ProcessAudioSource(ProcessAudioSource &&source) = default;

        ProcessAudioSource &operator=(ProcessAudioSource &&source) = default;

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
