#pragma once

#include <cstddef>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

namespace rto {

class MemoryMappedFile {
public:
    MemoryMappedFile();
    ~MemoryMappedFile();

    MemoryMappedFile(const MemoryMappedFile&) = delete;
    MemoryMappedFile& operator=(const MemoryMappedFile&) = delete;

    bool map(const std::filesystem::path& path, std::size_t size);
    void unmap();
    void* data() const;
    std::size_t size() const;
    bool flush(std::size_t length);

private:
#ifdef _WIN32
    HANDLE fileHandle_ = INVALID_HANDLE_VALUE;
    HANDLE mappingHandle_ = nullptr;
#else
    int fd_ = -1;
#endif
    void* data_ = nullptr;
    std::size_t size_ = 0;
    std::filesystem::path path_;
};

}  // namespace rto
