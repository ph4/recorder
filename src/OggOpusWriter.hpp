//
// Created by pavel on 04.12.2024.
//

#ifndef OGGOPUSWRITER_HPP
#define OGGOPUSWRITER_HPP

#include <array>
#include <memory>
#include <ostream>

#include <opus.h>
#include <random>
#include <ogg/ogg.h>
#include <span>
#include <variant>
#include <spdlog/spdlog.h>



#pragma pack(push, 1)
// Assume LittleEndian
extern "C" struct OpusHeader {
    uint8_t magic[8] = {'O', 'p', 'u', 's', 'H', 'e', 'a', 'd'};
    uint8_t version = 1;
    uint8_t channels = 2;
    uint16_t preSkip = 0;
    uint32_t sampleRate = 0;
    uint16_t gain = 0;
    uint8_t channelMap = 0;
};
#pragma pack(pop)

// template<typename T>
// concept wstream = std::is_base_of<std::ostream, T>::type;

template <typename  W, size_t FRAME_SIZE_MS, size_t SAMPLE_RATE>
class OggOpusWriter {
public:
    constexpr static size_t FRAME_SIZE = FRAME_SIZE_MS * SAMPLE_RATE / 1000;
    using FrameStereo = std::span<const int16_t, FRAME_SIZE * 2>;
private:
    W writer_;
    const int32_t bitrate_kbps_;

    uint32_t packets_in_page_ = 0;
    uint32_t packet_no_ = 0;
    uint32_t granule_pos_ = 0;
    std::vector<int16_t> buffer_{};

    static void opusEncoderDeleter(OpusEncoder *encoder) {
        if (encoder != nullptr)
            opus_encoder_destroy(encoder);
    };
    std::unique_ptr<OpusEncoder, decltype(&opusEncoderDeleter)> encoder_{
        (OpusEncoder*)malloc(opus_encoder_get_size(2)), &opusEncoderDeleter
    };
    ogg_stream_state ogg_stream_state_{};
public:
    OggOpusWriter(W writer_, int32_t bitrate_kbps);

    int WritePage(ogg_page &page) {
        writer_.write(reinterpret_cast<char *>(page.header), page.header_len);
        writer_.write(reinterpret_cast<char *>(page.body), page.body_len);
        writer_.flush();
        return 0;
    }
    int Init() {
        auto err = opus_encoder_init(encoder_.get(), SAMPLE_RATE, 2, OPUS_APPLICATION_VOIP);
        if (err != OPUS_OK) {
            SPDLOG_ERROR("opus_encoder_init failed: {}", err);
            return -1;
        }

        err = opus_encoder_ctl(encoder_.get(), OPUS_SET_FORCE_CHANNELS(2));
        if (err != OPUS_OK) {
            SPDLOG_ERROR("opus_encoder_ctl(OPUS_SET_FORCE_CHANNELS) failed: {}", opus_strerror(err));
            return -1;
        }
        err = opus_encoder_ctl(encoder_.get(), OPUS_SET_BITRATE(bitrate_kbps_ * 1024));
        if (err != OPUS_OK) {
            SPDLOG_ERROR("opus_encoder_ctl(OPUS_SET_BUTRATE) failed: {}", opus_strerror(err));
            return -1;
        }
        int skip_samples;
        err = opus_encoder_ctl(encoder_.get(), OPUS_GET_LOOKAHEAD(&skip_samples));
        if (err != OPUS_OK) {
            SPDLOG_ERROR("opus_encoder_ctl(OPUS_GET_LOOKAHEAD) failed: {}", opus_strerror(err));
            return -1;
        }
        OpusHeader header;
        header.preSkip = skip_samples * 48'000 / SAMPLE_RATE;
        header.sampleRate = SAMPLE_RATE;

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution dis(0, std::numeric_limits<int32_t>::max());
        const auto serial = dis(gen) ^ _getpid();
        ogg_stream_init(&ogg_stream_state_, serial);

        std::vector<uint8_t> opus_tags;
        std::string vendor = "ogg-opus 0.1.0";
        std::string otags = "OpusTags";
        opus_tags.insert(opus_tags.end(), otags.begin(), otags.end());
        // Write integer byte by byte (assumes little endian)
        for (auto i = 0; i < 4; i++) {
            opus_tags.push_back(vendor.length() >> (i * 8));
        }
        opus_tags.insert(opus_tags.end(), vendor.begin(), vendor.end());
        opus_tags.insert(opus_tags.end(), {0, 0, 0, 0});

        ogg_packet oggpack{};
        ogg_packet_clear(&oggpack);

        oggpack.packet = reinterpret_cast<unsigned char *>(&header);
        oggpack.bytes = sizeof(header);
        oggpack.b_o_s = 1;
        oggpack.granulepos = 0;
        oggpack.packetno = packet_no_++;

        ogg_stream_packetin(&ogg_stream_state_, &oggpack);

        // ogg_packet_clear(&oggpack); tries to free oggpack.packet
        memset(&oggpack, 0, sizeof(oggpack));
        oggpack.packet = opus_tags.data();
        oggpack.bytes = opus_tags.size();
        oggpack.granulepos = 0;
        oggpack.packetno = packet_no_++;

        ogg_stream_packetin(&ogg_stream_state_, &oggpack);

        return Flush();
    }

    int Push(FrameStereo frames) {
        //maximum size recommended by opus
        std::array<uint8_t, 4000> encoded{};
        const auto encoded_size = opus_encode(encoder_.get(), frames.data(), frames.size(), encoded.data(),
                                              encoded.size());
        if (encoded_size < 0) {
            return encoded_size;
        }
        // Number of samples that would be written if input sample rate was = 48000
        granule_pos_ += FRAME_SIZE * 48000 / SAMPLE_RATE;

        ogg_packet oggpack{};
        ogg_packet_clear(&oggpack);

        oggpack.packet = encoded.data();
        oggpack.bytes = encoded_size;
        oggpack.granulepos = granule_pos_;
        oggpack.packetno = packet_no_++;

        auto res = ogg_stream_packetin(&ogg_stream_state_, &oggpack);
        if (res == -1) {
            return res;
        }
        // Exclude OpusHead and OpusTags packets
        if ((packet_no_ - 2) % 127 == 0) {
            if (res = Flush(); res != 0) {
                SPDLOG_ERROR("Flush failed: {}", res);
                return res;
            }
        }
        return 0;
    }
    int Flush() {
        ogg_page page;
        ogg_stream_flush(&ogg_stream_state_, &page);
        return WritePage(page);
    }
    std::variant<W, int> Finalize() {
        ogg_packet pack{};
        ogg_packet_clear(&pack);
        //auto res = ogg_stream_packetin(&ogg_stream_state_, &pack);
        // if (res != 0) {
        //     SPDLOG_ERROR("ogg_stream_packetin err = {}", res);
        //     return res;
        // }

        auto res = Flush();
        if (res != 0) {
            SPDLOG_ERROR("Flush err = {}", res);
            return res;
        }
        ogg_stream_clear(&ogg_stream_state_);
        return std::move(writer_);
    }
};

template<typename W, size_t FRAME_SIZE_MS, size_t SAMPLE_RATE>
OggOpusWriter<W, FRAME_SIZE_MS, SAMPLE_RATE>
::OggOpusWriter(W writer_, int32_t bitrate_kbps): writer_(std::move(writer_)), bitrate_kbps_(bitrate_kbps) {
}

#endif //OGGOPUSWRITER_HPP
