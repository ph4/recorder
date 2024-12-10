//
// Created by pavel on 07.12.2024.
//

#ifndef CHUNKEDBUFFER_HPP
#define CHUNKEDBUFFER_HPP
#include <array>
#include <iterator>

#include <span>
#include <stdexcept>

template<typename T, size_t ChunkSize, size_t NChunks>
class ChunkedBuffer {
    using Chunk = std::span<T, ChunkSize>;
    using array_type = std::array<T, ChunkSize * NChunks>;
    array_type data_{};
    size_t write_idx_ = 0;
    size_t read_idx_ = 0;
    size_t size_ = 0;

public:
    ChunkedBuffer() = default;
    ~ChunkedBuffer() = default;

    bool IsEmpty() const {
        return size_ == 0;
    }

    bool HasChunks() const {
        return size_ >= ChunkSize;
    }

    Chunk Retrieve() {
        if (size_ < ChunkSize) {
            throw std::out_of_range("Retrieve out of range");
        }
        const auto start = read_idx_;
        const auto end = (start + ChunkSize) % (NChunks * ChunkSize);
        read_idx_ = end;
        size_ -= ChunkSize;
        return std::span<T, ChunkSize>(data_.begin() + start, ChunkSize);
    };

    std::span<T> RetrieveRemainder() {
        return std::span<T>(data_.begin() + size_ / ChunkSize, size_ % ChunkSize);
    };

    void Push(const std::span<const T> data) {
        if (size_ + data.size() > NChunks * ChunkSize) {
            throw std::runtime_error("ChunkedBuffer::Push: Tried to push more than buffer can hold");
        }
        auto can_write = data_.size() - write_idx_;
        auto iter = data_.begin() + write_idx_;
        if (data.size() < can_write) {
            iter = std::copy(data.begin(), data.end(), iter);
        } else {
            auto first = data.subspan(0, can_write);
            auto second = data.subspan(can_write);
            std::copy(first.begin(), first.end(), iter);
            iter = std::copy(second.begin(), second.end(), data_.begin());
        }
        write_idx_ = iter - data_.begin();
        size_ += data.size();
    }
};

#endif //CHUNKEDBUFFER_HPP
