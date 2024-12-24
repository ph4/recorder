//
// Created by pavel on 22.12.2024.
//
module;

#include <span>
#include <stdexcept>

export module RingBuffer;

export template<typename T, size_t NChannels, size_t ChunkFrames, size_t NChunks>
class InterleaveRingBuffer {
    static constexpr size_t ChunkSamples = ChunkFrames * NChannels;
    using Chunk = std::span<T, ChunkSamples>;
    using array_type = std::array<T, ChunkSamples * NChunks>;
    array_type data_{};
    std::array<size_t, NChannels> write_frame_idx_{};
    std::array<size_t, NChannels> sizes_frames_{};
    // size_t write_idx_ = 0;
    size_t read_idx_ = 0;
    size_t min_size_frames_ = 0;

    template<size_t Channel, bool DoSum = false>
    void PushChannelImpl(const std::span<const T> in) {
        static_assert(Channel < NChannels, "Channel out of range");
        if (sizes_frames_[Channel] + in.size() > NChunks * ChunkFrames) {
            throw std::runtime_error("ChunkedBuffer::Push: Tried to push more than buffer can hold");
        }
        size_t i_dst_frame = write_frame_idx_[Channel];
        auto can_write_frames = data_.size() / NChannels - i_dst_frame;
        if (in.size() < can_write_frames) {
            auto i_src = 0;
            for (; i_src < in.size(); ++i_dst_frame, ++i_src) {
                if constexpr (DoSum) {
                    data_[i_dst_frame * NChannels + Channel] += in[i_src];
                } else {
                    data_[i_dst_frame * NChannels + Channel] = in[i_src];
                }
            }
        } else {
            auto i_src = 0;
            for (; i_src < can_write_frames; ++i_dst_frame , ++i_src){
                if constexpr (DoSum) {
                    data_[i_dst_frame * NChannels + Channel] += in[i_src];
                } else {
                    data_[i_dst_frame * NChannels + Channel] = in[i_src];
                }
            }
            i_dst_frame = 0;
            for (; i_src < in.size(); ++i_dst_frame, ++i_src) {
                if constexpr (DoSum) {
                    data_[i_dst_frame * NChannels + Channel] += in[i_src];
                } else {
                    data_[i_dst_frame * NChannels + Channel] = in[i_src];
                }
            }
        }
        write_frame_idx_[Channel] = i_dst_frame;
        sizes_frames_[Channel] += in.size();

        min_size_frames_ = *std::min_element(sizes_frames_.begin(), sizes_frames_.end());
    }
public:
    InterleaveRingBuffer() = default;
    ~InterleaveRingBuffer() = default;

    [[nodiscard]] bool IsEmpty() const {
        return *std::max_element(sizes_frames_.begin(), sizes_frames_.end()) == 0;
    }

    [[nodiscard]] bool HasChunks() const {
        return min_size_frames_ >= ChunkFrames;
    }

    constexpr size_t GetChunkSizeFrames() const {
        return ChunkFrames;
    }
    Chunk Retrieve() {
        if (min_size_frames_ < ChunkFrames) {
            throw std::out_of_range("Retrieve out of range");
        }

        const auto start = read_idx_;
        const auto end = (start + ChunkSamples) % (NChunks * ChunkSamples);
        read_idx_ = end;
        min_size_frames_ -= ChunkFrames;
        for (auto& i: sizes_frames_) {
            i -= ChunkFrames;
        }
        return std::span<T, ChunkSamples>(data_.begin() + start, ChunkSamples);
    };

    std::span<T> RetrieveRemainder() {
        return std::span<T>(data_.begin() + min_size_frames_ / ChunkSamples, min_size_frames_ % ChunkSamples);
    };

    template<bool DoSum = false>
    void Push(const std::span<const T> in) requires (NChannels == 1) {
        return PushChannelImpl<0, DoSum>(in);
    }

    template<size_t Channel>
    void AddChannel(const std::span<const T> in) {
        return PushChannelImpl<Channel, true>(in);
    }

    template<size_t Channel>
    void PushChannel(const std::span<const T> in) {
        return PushChannelImpl<Channel, false>(in);
    }

};

export template<typename T, size_t NChannels, size_t ChunkFrames, size_t NChunks>
class SinkRingBuffer {
    static constexpr size_t ChunkSamples = ChunkFrames;
    using Chunk = std::span<T, ChunkFrames>;
    using array_type = std::array<T, ChunkFrames * NChunks>;
    array_type data_{};
    std::array<size_t, NChannels> write_frame_idx_{};
    std::array<size_t, NChannels> sizes_frames_{};
    // size_t write_idx_ = 0;
    size_t read_idx_ = 0;
    size_t min_size_frames_ = 0;

    template<size_t Channel>
    void AddChannel(const std::span<const T> in) {
        static_assert(Channel < NChannels, "Channel out of range");
        if (sizes_frames_[Channel] + in.size() > NChunks * ChunkFrames) {
            throw std::runtime_error("ChunkedBuffer::Push: Tried to push more than buffer can hold");
        }
        size_t i_dst_frame = write_frame_idx_[Channel];
        auto can_write_frames = data_.size() / NChannels - i_dst_frame;
        if (in.size() < can_write_frames) {
            auto i_src = 0;
            for (; i_src < in.size(); ++i_dst_frame, ++i_src) {
                data_[i_dst_frame] += in[i_src];
            }
        } else {
            auto i_src = 0;
            for (; i_src < can_write_frames; ++i_dst_frame , ++i_src){
                data_[i_dst_frame] += in[i_src];
            }
            i_dst_frame = 0;
            for (; i_src < in.size(); ++i_dst_frame, ++i_src) {
                data_[i_dst_frame] = in[i_src];
            }
        }
        write_frame_idx_[Channel] = i_dst_frame;
        sizes_frames_[Channel] += in.size();

        min_size_frames_ = *std::min_element(sizes_frames_.begin(), sizes_frames_.end());
    }
public:
    SinkRingBuffer() = default;
    ~SinkRingBuffer() = default;

    [[nodiscard]] bool IsEmpty() const {
        return *std::max_element(sizes_frames_.begin(), sizes_frames_.end()) == 0;
    }

    [[nodiscard]] bool HasChunks() const {
        return min_size_frames_ >= ChunkFrames;
    }

    Chunk Retrieve() {
        if (min_size_frames_ < ChunkFrames) {
            throw std::out_of_range("Retrieve out of range");
        }

        const auto start = read_idx_;
        const auto end = (start + ChunkSamples) % (NChunks * ChunkSamples);
        read_idx_ = end;
        min_size_frames_ -= ChunkFrames;
        for (auto& i: sizes_frames_) {
            i -= ChunkFrames;
        }
        return std::span<T, ChunkSamples>(data_.begin() + start, ChunkSamples);
    };

    std::span<T> RetrieveRemainder() {
        return std::span<T>(data_.begin() + min_size_frames_ / ChunkSamples, min_size_frames_ % ChunkSamples);
    };

    template<bool DoSum = false>
    void Push(const std::span<const T> in) requires (NChannels == 1) {
        return PushChannelImpl<0, DoSum>(in);
    }

    // template<size_t Channel>
    // void AddChannel(const std::span<const T> in) {
    //     return PushChannelImpl<Channel, true>(in);
    // }

    template<size_t Channel>
    void PushChannel(const std::span<const T> in) {
        return PushChannelImpl<Channel, false>(in);
    }

};
