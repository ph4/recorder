//
// Created by pavel on 04.12.2024.
//
module;
#include <span>
#include <array>
#include <memory>
#include <ostream>

#include <opus.h>
#include <random>
#include <ogg/ogg.h>
#include <variant>
#include <spdlog/spdlog.h>

export module OggOpusEncoder;
import RingBuffer;
import AudioSource;

namespace recorder::audio {
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

    export class OggOpusEncoder {
        static constexpr size_t OpusFrameSizeMS = 20;
        std::shared_ptr<std::ostream> writer_;
        AudioFormat format_;
        int32_t bitrate_kbps_;

        uint32_t max_packets_in_page_ = 64;
        uint32_t packets_in_page_ = 0;
        uint32_t packet_no_ = 0;
        uint32_t granule_pos_ = 0;
        std::unique_ptr<InterleaveRingBufferHeap<int16_t, 1, 3>> frame_buffer_;

        static void opusEncoderDeleter(OpusEncoder *encoder) {
            if (encoder != nullptr)
                opus_encoder_destroy(encoder);
        };
        std::unique_ptr<OpusEncoder, decltype(&opusEncoderDeleter)> encoder_;
        ogg_stream_state ogg_stream_state_{};

        [[nodiscard]] size_t samples_in_opus_frame() const {
            return OpusFrameSizeMS * format_.sampleRate * format_.channels / 1000;
        }

    public:
        OggOpusEncoder(std::shared_ptr<std::ostream> writer_, const AudioFormat format, const int32_t bitrate_kbps)
            : writer_(std::move(writer_)),
              format_(format),
              bitrate_kbps_(bitrate_kbps),
              frame_buffer_(std::make_unique<InterleaveRingBufferHeap<int16_t, 1, 3>>(samples_in_opus_frame())),
              encoder_(
                  static_cast<std::unique_ptr<OpusEncoder, decltype(&opusEncoderDeleter)>::pointer>(malloc(
                      opus_encoder_get_size(format.channels))),
                  &opusEncoderDeleter
              ) {
        }

        int Init() {
            auto err = opus_encoder_init(encoder_.get(), format_.sampleRate , format_.channels, OPUS_APPLICATION_VOIP);
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
            header.channels = format_.channels;
            header.preSkip = skip_samples * 48'000 / format_.sampleRate;
            header.sampleRate = format_.sampleRate;

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
            frame_buffer_->Push(data);
            return PostPush();
        }

        int Finalize() {
            // Pushing zeroes sometimes crashes the encoder
            frame_buffer_->Push(std::vector<int16_t>(samples_in_opus_frame(), 1));
            auto res = EncodeFrame(frame_buffer_->Retrieve(), true);
            if (res != 0) {
                SPDLOG_ERROR("Flush err = {}", res);
                return res;
            }
            ogg_stream_clear(&ogg_stream_state_);
            return 0;
        }

    private:
        int WritePage(const ogg_page &page) const {
            writer_->write(reinterpret_cast<char *>(page.header), page.header_len);
            writer_->write(reinterpret_cast<char *>(page.body), page.body_len);
            writer_->flush();
            return 0;
        }

        int EncodeFrame(const std::span<const int16_t> frame, const bool last = false) {
            //maximum size recommended by opus
            std::array<uint8_t, 4000> encoded{};
            const auto encoded_size = opus_encode(encoder_.get(), frame.data(), frame.size() / format_.channels,
                                                  encoded.data(),
                                                  encoded.size());
            if (encoded_size < 0) {
                SPDLOG_ERROR("opus_encode failed: {}", opus_strerror(encoded_size));
                return encoded_size;
            }
            // Number of samples that would be written if input sample rate was = 48000
            granule_pos_ += frame.size() / format_.channels * 48'000 / format_.sampleRate;

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

            if (++packets_in_page_ > max_packets_in_page_ || last) {
                packets_in_page_ = 0;
                if (res = Flush(); res != 0) {
                    SPDLOG_ERROR("Flush failed: {}", res);
                    return res;
                }
            }

            return 0;
        }

        int PostPush() {
            while (frame_buffer_->HasChunks()) {
                if (auto res = EncodeFrame(frame_buffer_->Retrieve())) return res;
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
}
