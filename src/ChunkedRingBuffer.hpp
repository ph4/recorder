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
    array_type chunks_{};
    size_t writepos_ = 0;
    array_type::iterator write_iter_ = chunks_.begin();
    size_t head_chunk_idx = 0;
    size_t read_chunk_idx = 0;
    size_t n_chunks_ = 0;
public:
    [[nodiscard]] size_t n_chunks() const {
        return n_chunks_;
    }
    ChunkedBuffer() {
    };
    ~ChunkedBuffer() = default;
    Chunk ReadChunk() {
        if (n_chunks_ == 0) {
            throw std::runtime_error("ChunkedBuffer::ReadChunk: Tried to read an empty buffer");
        }
        n_chunks_--;
        const auto idx = read_chunk_idx;
        read_chunk_idx = (idx + 1) % NChunks;
        auto start = chunks_.begin() + (read_chunk_idx * ChunkSize);
        auto end = chunks_.begin() + (read_chunk_idx * ChunkSize);
        return std::span<T, ChunkSize>(start, ChunkSize);
    };
    int WriteLooping(const std::span<const T> data) {
        auto can_write = chunks_.end() - write_iter_;
        if (data.size() < can_write) {
            write_iter_ = std::copy(data.begin(), data.end(), write_iter_);
            return 0;
        } else {
            auto first = data.subspan(0, can_write);
            auto second = data.subspan(can_write);
            std::copy(first.begin(), first.end(), write_iter_);
            write_iter_ = std::copy(second.begin(), second.end(), chunks_.begin());
            return false;
        }
    }
    void PushData(const std::span<const T> data) {
        if (n_chunks_ == NChunks) {
            throw std::runtime_error("ChunkedBuffer::PushData: Tried to push to a full buffer");
        }
        auto full_chunks = data.size() / ChunkSize;
        auto remainder = data.size() % ChunkSize;
        //TODO Properly handle remainder
        if (n_chunks_ + full_chunks > NChunks || remainder > ChunkSize) {
            throw std::runtime_error("ChunkedBuffer::PushData: Tried to push more than buffer can hold");
        }
        WriteLooping(data);
        n_chunks_ += full_chunks;
        head_chunk_idx = (head_chunk_idx + full_chunks) % NChunks;
        if (remainder > 0) {
            if (write_iter_ > chunks_.begin() + head_chunk_idx + 1) {
                head_chunk_idx += 1;
                n_chunks_ += 1;
            }
        }
    }
};

#endif //CHUNKEDBUFFER_HPP
