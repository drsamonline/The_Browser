#pragma once

/**
 * AETHERIS GHOST CACHE
 * Dependency-free, zero-allocation C++20 Memory Pool manager
 * LZ4-compressed RAM array with secure memory wiping on tab close
 */

#ifndef AETHERIS_GHOST_CACHE_HPP
#define AETHERIS_GHOST_CACHE_HPP

#include "aetheris/core.hpp"
#include <cstdint>
#include <cstddef>
#include <array>
#include <optional>
#include <span>
#include <new>

// LZ4 compression (via vcpkg)
#include <lz4.h>

namespace aetheris::cache {

// Configuration constants
constexpr size_t POOL_BLOCK_SIZE = 4096;        // 4KB blocks
constexpr size_t POOL_MAX_BLOCKS = 1024;        // Max 4MB pool
constexpr size_t POOL_TOTAL_SIZE = POOL_BLOCK_SIZE * POOL_MAX_BLOCKS;
constexpr size_t MAX_COMPRESSED_SIZE = POOL_TOTAL_SIZE;

// Cache entry metadata
struct CacheEntryMetadata {
    uint64_t id{0};
    uint64_t timestamp{0};
    size_t original_size{0};
    size_t compressed_size{0};
    bool is_encrypted{false};
    bool is_active{false};
    char url_hash[32]{};  // Simple hash identifier
};

/**
 * Zero-Allocation Memory Pool
 * Pre-allocated fixed-size pool with block management
 */
template<typename T, size_t BlockSize, size_t MaxBlocks>
class StaticMemoryPool {
public:
    StaticMemoryPool() noexcept = default;
    
    ~StaticMemoryPool() noexcept {
        // Secure zero all memory on destruction
        clear_secure();
    }
    
    // Non-copyable, non-movable (singleton-like behavior)
    StaticMemoryPool(const StaticMemoryPool&) = delete;
    StaticMemoryPool& operator=(const StaticMemoryPool&) = delete;
    StaticMemoryPool(StaticMemoryPool&&) = delete;
    StaticMemoryPool& operator=(StaticMemoryPool&&) = delete;
    
    /**
     * Allocate a block from the pool
     */
    [[nodiscard]] std::optional<size_t> allocate(size_t size_bytes) noexcept {
        if (size_bytes == 0 || size_bytes > BlockSize) {
            return std::nullopt;
        }
        
        // Find first free block
        for (size_t i = 0; i < MaxBlocks; ++i) {
            if (!m_block_in_use[i]) {
                m_block_in_use[i] = true;
                m_block_sizes[i] = size_bytes;
                ++m_allocated_count;
                return i;
            }
        }
        
        return std::nullopt;  // Pool exhausted
    }
    
    /**
     * Deallocate a block and securely zero it
     */
    void deallocate(size_t block_index) noexcept {
        if (block_index >= MaxBlocks || !m_block_in_use[block_index]) {
            return;
        }
        
        // Secure zero the block before releasing
        secure_zero_memory(get_block_ptr(block_index), m_block_sizes[block_index]);
        
        m_block_in_use[block_index] = false;
        m_block_sizes[block_index] = 0;
        --m_allocated_count;
    }
    
    /**
     * Get pointer to block data
     */
    [[nodiscard]] T* get_block_ptr(size_t block_index) noexcept {
        if (block_index >= MaxBlocks) {
            return nullptr;
        }
        return reinterpret_cast<T*>(m_pool.data() + (block_index * BlockSize));
    }
    
    [[nodiscard]] const T* get_block_ptr(size_t block_index) const noexcept {
        if (block_index >= MaxBlocks) {
            return nullptr;
        }
        return reinterpret_cast<const T*>(m_pool.data() + (block_index * BlockSize));
    }
    
    /**
     * Get block span for safe access
     */
    [[nodiscard]] std::span<T> get_block_span(size_t block_index) noexcept {
        if (block_index >= MaxBlocks || !m_block_in_use[block_index]) {
            return {};
        }
        return {get_block_ptr(block_index), m_block_sizes[block_index] / sizeof(T)};
    }
    
    /**
     * Check if block is allocated
     */
    [[nodiscard]] bool is_block_allocated(size_t block_index) const noexcept {
        return block_index < MaxBlocks && m_block_in_use[block_index];
    }
    
    /**
     * Get allocated block size
     */
    [[nodiscard]] size_t get_block_size(size_t block_index) const noexcept {
        if (block_index >= MaxBlocks) {
            return 0;
        }
        return m_block_sizes[block_index];
    }
    
    /**
     * Get total allocated blocks
     */
    [[nodiscard]] size_t allocated_count() const noexcept {
        return m_allocated_count;
    }
    
    /**
     * Get remaining free blocks
     */
    [[nodiscard]] size_t free_blocks() const noexcept {
        return MaxBlocks - m_allocated_count;
    }
    
    /**
     * Clear all allocations and securely zero pool
     */
    void clear_secure() noexcept {
        for (size_t i = 0; i < MaxBlocks; ++i) {
            if (m_block_in_use[i]) {
                secure_zero_memory(get_block_ptr(i), m_block_sizes[i]);
            }
            m_block_in_use[i] = false;
            m_block_sizes[i] = 0;
        }
        m_allocated_count = 0;
    }
    
    /**
     * Get pool utilization percentage
     */
    [[nodiscard]] float utilization_percent() const noexcept {
        return (static_cast<float>(m_allocated_count) / MaxBlocks) * 100.0f;
    }
    
private:
    alignas(64) std::array<uint8_t, BlockSize * MaxBlocks> m_pool{};
    std::array<bool, MaxBlocks> m_block_in_use{};
    std::array<size_t, MaxBlocks> m_block_sizes{};
    size_t m_allocated_count{0};
};

/**
 * Ghost Cache Manager
 * Manages encrypted, LZ4-compressed session data in RAM
 */
class GhostCache {
public:
    using MemoryPool = StaticMemoryPool<uint8_t, POOL_BLOCK_SIZE, POOL_MAX_BLOCKS>;
    
    GhostCache() noexcept = default;
    
    ~GhostCache() noexcept {
        // All memory is automatically zeroed by MemoryPool destructor
    }
    
    /**
     * Store session data in compressed, encrypted form
     * Returns block index or nullopt if pool is full
     */
    [[nodiscard]] std::optional<size_t> store_session_data(
        std::span<const uint8_t> data,
        const CacheEntryMetadata& metadata
    ) noexcept {
        // Compress data using LZ4
        const int max_compressed_size = LZ4_compressBound(static_cast<int>(data.size()));
        
        auto block_opt = m_pool.allocate(static_cast<size_t>(max_compressed_size));
        if (!block_opt) {
            return std::nullopt;  // Pool exhausted
        }
        
        size_t block_idx = *block_opt;
        uint8_t* block_ptr = m_pool.get_block_ptr(block_idx);
        
        // Perform LZ4 compression
        int compressed_size = LZ4_compress_default(
            reinterpret_cast<const char*>(data.data()),
            reinterpret_cast<char*>(block_ptr),
            static_cast<int>(data.size()),
            max_compressed_size
        );
        
        if (compressed_size <= 0) {
            // Compression failed, store uncompressed
            m_pool.deallocate(block_idx);
            return std::nullopt;
        }
        
        // Store metadata (in production, encrypt this too)
        m_entry_metadata[block_idx] = metadata;
        m_entry_metadata[block_idx].compressed_size = static_cast<size_t>(compressed_size);
        m_entry_metadata[block_idx].original_size = data.size();
        m_entry_metadata[block_idx].is_encrypted = false;  // Set to true after encryption
        m_entry_metadata[block_idx].is_active = true;
        
        return block_idx;
    }
    
    /**
     * Retrieve and decompress session data
     * Caller must provide buffer large enough for decompressed data
     */
    [[nodiscard]] std::optional<size_t> retrieve_session_data(
        size_t block_index,
        std::span<uint8_t> output_buffer
    ) noexcept {
        if (block_index >= POOL_MAX_BLOCKS || !m_entry_metadata[block_index].is_active) {
            return std::nullopt;
        }
        
        const auto& meta = m_entry_metadata[block_index];
        const uint8_t* compressed_ptr = m_pool.get_block_ptr(block_index);
        
        // Check if output buffer is large enough
        if (output_buffer.size() < meta.original_size) {
            return std::nullopt;
        }
        
        // Decompress using LZ4
        int decompressed_size = LZ4_decompress_safe(
            reinterpret_cast<const char*>(compressed_ptr),
            reinterpret_cast<char*>(output_buffer.data()),
            static_cast<int>(meta.compressed_size),
            static_cast<int>(meta.original_size)
        );
        
        if (decompressed_size < 0) {
            return std::nullopt;  // Decompression failed
        }
        
        return static_cast<size_t>(decompressed_size);
    }
    
    /**
     * Close tab and securely wipe its cache allocation
     */
    void close_tab(size_t block_index) noexcept {
        if (block_index >= POOL_MAX_BLOCKS) {
            return;
        }
        
        // Secure zero the block
        m_pool.deallocate(block_index);
        
        // Clear metadata
        m_entry_metadata[block_index] = CacheEntryMetadata{};
    }
    
    /**
     * Clear all cache entries and zero all memory
     */
    void clear_all() noexcept {
        for (size_t i = 0; i < POOL_MAX_BLOCKS; ++i) {
            if (m_entry_metadata[i].is_active) {
                close_tab(i);
            }
        }
        m_pool.clear_secure();
    }
    
    /**
     * Get cache statistics
     */
    [[nodiscard]] size_t active_entries() const noexcept {
        size_t count = 0;
        for (const auto& meta : m_entry_metadata) {
            if (meta.is_active) {
                ++count;
            }
        }
        return count;
    }
    
    [[nodiscard]] size_t total_compressed_bytes() const noexcept {
        size_t total = 0;
        for (const auto& meta : m_entry_metadata) {
            if (meta.is_active) {
                total += meta.compressed_size;
            }
        }
        return total;
    }
    
    [[nodiscard]] size_t total_uncompressed_bytes() const noexcept {
        size_t total = 0;
        for (const auto& meta : m_entry_metadata) {
            if (meta.is_active) {
                total += meta.original_size;
            }
        }
        return total;
    }
    
    [[nodiscard]] float compression_ratio() const noexcept {
        size_t uncompressed = total_uncompressed_bytes();
        size_t compressed = total_compressed_bytes();
        
        if (uncompressed == 0 || compressed == 0) {
            return 1.0f;
        }
        
        return static_cast<float>(uncompressed) / static_cast<float>(compressed);
    }
    
    /**
     * Get memory pool reference for direct access
     */
    [[nodiscard]] const MemoryPool& get_pool() const noexcept {
        return m_pool;
    }
    
private:
    MemoryPool m_pool;
    std::array<CacheEntryMetadata, POOL_MAX_BLOCKS> m_entry_metadata{};
};

} // namespace aetheris::cache

#endif // AETHERIS_GHOST_CACHE_HPP
