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

#include "ChunkedRingBuffer.hpp"

// Assume LittleEndian
#pragma pack(push, 1)
extern "C" struct OpusHeader {
    uint8_t magic[8] = {'O', 'p', 'u', 's', 'H', 'e', 'a', 'd'};
    uint8_t version = 1;
    uint8_t channels = 0;
    uint16_t preSkip = 0;
    uint32_t sampleRate = 0;
    uint16_t gain = 0;
    uint8_t channelMap = 0;
};
#pragma pack(pop)

template<typename T>
concept ostream = std::is_base_of_v<std::ostream, T>;

template<ostream W, size_t FRAME_SIZE_MS, size_t SAMPLE_RATE, int CHANNELS>
class OggOpusWriter {
public:
    constexpr static size_t FRAME_SIZE = FRAME_SIZE_MS * SAMPLE_RATE / 1000;
    using Frame = std::span<const int16_t, FRAME_SIZE * CHANNELS>;

private:
    W writer_;
    const int32_t bitrate_kbps_;

    consteval uint32_t max_packets_in_page_ = 64;
    uint32_t packets_in_page_ = 0;
    uint32_t packet_no_ = 0;
    uint32_t granule_pos_ = 0;
    ChunkedBuffer<int16_t, FRAME_SIZE * CHANNELS, 3> frame_buffer_{};

    static void opusEncoderDeleter(OpusEncoder *encoder) {
        if (encoder != nullptr)
            opus_encoder_destroy(encoder);
    };
    std::unique_ptr<OpusEncoder, decltype(&opusEncoderDeleter)> encoder_{
        reinterpret_cast<OpusEncoder *>(malloc(opus_encoder_get_size(CHANNELS))), &opusEncoderDeleter
    };
    ogg_stream_state ogg_stream_state_{};

public:
    OggOpusWriter(W writer_, const int32_t bitrate_kbps): writer_(std::move(writer_)), bitrate_kbps_(bitrate_kbps) { }

    int Init() {
        auto err = opus_encoder_init(encoder_.get(), SAMPLE_RATE, CHANNELS, OPUS_APPLICATION_VOIP);
        if (err != OPUS_OK) {
            SPDLOG_ERROR("opus_encoder_init failed: {}", err);
            return -1;
        }
        err = opus_encoder_ctl(encoder_.get(), OPUS_SET_BITRATE(bitrate_kbps_ * 1024));
        if (err != OPUS_OK) {
            SPDLOG_ERROR("opus_encoder_ctl(OPUS_SET_BITRATE) failed: {}", opus_strerror(err));
            return -1;
        }
        int skip_samples;
        err = opus_encoder_ctl(encoder_.get(), OPUS_GET_LOOKAHEAD(&skip_samples));
        if (err != OPUS_OK) {
            SPDLOG_ERROR("opus_encoder_ctl(OPUS_GET_LOOKAHEAD) failed: {}", opus_strerror(err));
            return -1;
        }
        OpusHeader header;
        header.channels = CHANNELS;
        header.preSkip = skip_samples * 48'000 / SAMPLE_RATE;
        header.sampleRate = SAMPLE_RATE;

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution dis(0, std::numeric_limits<int32_t>::max());
        const auto serial = dis(gen) ^ _getpid();
        ogg_stream_init(&ogg_stream_state_, serial);

        std::vector<uint8_t> opus_tags;
        std::string vendor = "recorder ogg-opus 0.0.1";
        std::string ot = "OpusTags";
        opus_tags.insert(opus_tags.end(), ot.begin(), ot.end());
        // Write integer byte by byte (assumes little endian)
        for (auto i = 0; i < 4; i++) {
            opus_tags.push_back(vendor.length() >> (i * 8));
        }
        opus_tags.insert(opus_tags.end(), vendor.begin(), vendor.end());
        opus_tags.insert(opus_tags.end(), {0, 0, 0, 0});

        ogg_packet packet = {};

        packet.packet = reinterpret_cast<unsigned char *>(&header);
        packet.bytes = sizeof(header);
        packet.b_o_s = 1;
        packet.granulepos = 0;
        packet.packetno = packet_no_++;

        ogg_stream_packetin(&ogg_stream_state_, &packet);

        // ogg_packet_clear(&oggpack); tries to free oggpack.packet
        memset(&packet, 0, sizeof(packet));
        packet.packet = opus_tags.data();
        packet.bytes = opus_tags.size();
        packet.granulepos = 0;
        packet.packetno = packet_no_++;

        ogg_stream_packetin(&ogg_stream_state_, &packet);
        return Flush();
    }

    int Push(std::span<const int16_t> data) {
        frame_buffer_.Push(data);
        return PostPush();
    }

    std::variant<W, int> Finalize() {
        frame_buffer_.Push(std::vector<int16_t>(FRAME_SIZE * CHANNELS, 0));
        auto res = EncodeFrame(frame_buffer_.Retrieve(), true);
        if (res != 0) {
            SPDLOG_ERROR("Flush err = {}", res);
            return res;
        }
        ogg_stream_clear(&ogg_stream_state_);
        return std::move(writer_);
    }

private:
    int WritePage(ogg_page &page) {
        writer_.write(reinterpret_cast<char *>(page.header), page.header_len);
        writer_.write(reinterpret_cast<char *>(page.body), page.body_len);
        writer_.flush();
        return 0;
    }

    int EncodeFrame(const std::span<const int16_t> frame, const bool last = false) {
        //maximum size recommended by opus
        std::array<uint8_t, 4000> encoded{};
        const auto encoded_size = opus_encode(encoder_.get(), frame.data(), frame.size() / CHANNELS, encoded.data(),
                                              encoded.size());
        if (encoded_size < 0) {
            SPDLOG_ERROR("opus_encode failed: {}", opus_strerror(encoded_size));
            return encoded_size;
        }
        // Number of samples that would be written if input sample rate was = 48000
        granule_pos_ += frame.size() / CHANNELS * 48000 / SAMPLE_RATE;

        ogg_packet packet = {};
        packet.packet = encoded.data();
        packet.bytes = encoded_size;
        packet.granulepos = granule_pos_;
        packet.packetno = packet_no_++;
        if (last) {
            packet.e_o_s = 1;
        }

        auto res = ogg_stream_packetin(&ogg_stream_state_, &packet);
        if (res == -1) {
            return res;
        }

        if (++max_packets_in_page_ > 32 || last) {
            packets_in_page_ = 0;
            if (res = Flush(); res != 0) {
                SPDLOG_ERROR("Flush failed: {}", res);
                return res;
            }
        }

        return 0;
    }

    int PostPush() {
        while (frame_buffer_.HasChunks()) {
            if (auto res = EncodeFrame(frame_buffer_.Retrieve())) return res;
        }
        return 0;
    }

    int Flush() {
        ogg_page page;
        while (ogg_stream_flush(&ogg_stream_state_, &page)) {
            if (auto res = WritePage(page); res) return res;
        }
        return 0;
    }
};


#endif //OGGOPUSWRITER_HPP
