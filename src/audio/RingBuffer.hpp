//
// Created by pavel on 22.12.2024.
//
#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <array>
#include <cassert>
#include <mutex>
#include <span>
#include <stdexcept>
#include <vector>

template <typename ArrayT, typename T, size_t NChannels, size_t NChunks>
class InterleaveRingBufferBase {
protected:
    std::mutex mutex;
    const size_t chunk_frames_;
    const size_t chunk_samples_;
    // using Chunk = std::span<T, ChunkSamples>;
    ArrayT data_;
    std::array<size_t, NChannels> write_frame_idx_{};
    std::array<size_t, NChannels> sizes_frames_{};
    // size_t write_idx_ = 0;
    size_t read_chunk_idx_ = 0;
    size_t min_size_frames_ = 0;

    explicit InterleaveRingBufferBase(ArrayT &&data, const size_t chunk_frames)
        : chunk_frames_(chunk_frames),
          chunk_samples_(chunk_frames_ * NChannels),
          data_(std::move(data)) {}

    template <size_t Channel, bool DoSum = false>
    void PushChannelImpl(const std::span<const T> in) {
        static_assert(Channel < NChannels, "Channel out of range");
        if (sizes_frames_[Channel] + in.size() > NChunks * chunk_frames_) {
            throw std::runtime_error(
                  "ChunkedBuffer::Push: Tried to push more than buffer can hold"
            );
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
            for (; i_src < can_write_frames; ++i_dst_frame, ++i_src) {
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

        {
            std::lock_guard lock(mutex);
            min_size_frames_ = *std::min_element(sizes_frames_.begin(), sizes_frames_.end());
        }
    }

public:
    ~InterleaveRingBufferBase() = default;

    void Clear() {
        for (size_t i = 0; i < NChannels; ++i) {
            write_frame_idx_[i] = 0;
            sizes_frames_[i] = 0;
        }
        min_size_frames_ = 0;
        read_chunk_idx_ = 0;
    }

    [[nodiscard]] bool IsEmpty() const {
        return *std::max_element(sizes_frames_.begin(), sizes_frames_.end()) == 0;
    }

    [[nodiscard]] bool HasChunks() const { return min_size_frames_ >= chunk_frames_; }

    [[nodiscard]] size_t chunk_frames() const { return chunk_frames_; }

    template <size_t Channel>
        requires(Channel < NChannels)
    [[nodiscard]] size_t CanPushSamples() const {
        return chunk_frames_ * NChunks - sizes_frames_[Channel];
    }

    std::span<T> Retrieve() {
        if (min_size_frames_ < chunk_frames_) {
            throw std::out_of_range("Retrieve out of range");
        }

        const auto start = read_chunk_idx_ * chunk_samples_;
        read_chunk_idx_ = (read_chunk_idx_ + 1) % NChunks;

        {
            std::lock_guard lock(mutex);
            min_size_frames_ -= chunk_frames_;
            for (auto &i : sizes_frames_) {
                assert(i >= chunk_frames_);
                i -= chunk_frames_;
            }
        }
        return std::span<T>(data_.begin() + start, chunk_samples_);
    };

    std::span<T> remainder() {
        return std::span<T>(data_.begin() + read_chunk_idx_ * chunk_samples_, min_size_frames_);
    };

    template <bool DoSum = false> void Push(const std::span<const T> in)
        requires(NChannels == 1)
    {
        return PushChannelImpl<0, DoSum>(in);
    }

    template <size_t Channel> void AddChannel(const std::span<const T> in) {
        return PushChannelImpl<Channel, true>(in);
    }

    template <size_t Channel> void PushChannel(const std::span<const T> in) {
        return PushChannelImpl<Channel, false>(in);
    }
};

template <typename T, size_t NChannels, size_t ChunkFrames, size_t NChunks>
class InterleaveRingBuffer
    : public InterleaveRingBufferBase<
            std::array<T, ChunkFrames * NChannels * NChunks>,
            T,
            NChannels,
            NChunks> {
public:
    InterleaveRingBuffer()
        : InterleaveRingBufferBase<
                std::array<T, ChunkFrames * NChannels * NChunks>,
                T,
                NChannels,
                NChunks>(
                std::move(std::array<T, ChunkFrames * NChannels * NChunks>()), ChunkFrames
          ) {}
};

template <typename T, size_t NChannels, size_t NChunks> class InterleaveRingBufferHeap
    : public InterleaveRingBufferBase<std::vector<T>, T, NChannels, NChunks> {
public:
    explicit InterleaveRingBufferHeap(const size_t chunk_frames)
        : InterleaveRingBufferBase<std::vector<T>, T, NChannels, NChunks>(
                std::move(std::vector<T>(chunk_frames * NChannels * NChunks, 0)), chunk_frames
          ) {}
};

template <typename T, size_t ChunkFrames, size_t NChunks> using RingBuffer =
      InterleaveRingBuffer<T, 1, ChunkFrames, NChunks>;
#endif
