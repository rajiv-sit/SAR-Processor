#include "rto/MemoryMappedFile.hpp"

#ifdef _WIN32
#include <io.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace rto {

namespace {
std::size_t clampSize(std::size_t requested) {
    constexpr std::size_t kMinSize = 1;
    return requested < kMinSize ? kMinSize : requested;
}
}  // namespace

MemoryMappedFile::MemoryMappedFile() = default;

MemoryMappedFile::~MemoryMappedFile() {
    unmap();
}

bool MemoryMappedFile::map(const std::filesystem::path& path, std::size_t size) {
    if (size == 0) {
        return false;
    }
    unmap();
    path_ = path;
    size = clampSize(size);
#ifdef _WIN32
    fileHandle_ = CreateFileA(
        path.string().c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (fileHandle_ == INVALID_HANDLE_VALUE) {
        fileHandle_ = INVALID_HANDLE_VALUE;
        return false;
    }

    LARGE_INTEGER distance{};
    distance.QuadPart = static_cast<LONGLONG>(size);
    if (!SetFilePointerEx(fileHandle_, distance, nullptr, FILE_BEGIN) ||
        !SetEndOfFile(fileHandle_)) {
        CloseHandle(fileHandle_);
        fileHandle_ = INVALID_HANDLE_VALUE;
        return false;
    }

    mappingHandle_ = CreateFileMappingA(fileHandle_,
                                        nullptr,
                                        PAGE_READWRITE,
                                        0,
                                        0,
                                        nullptr);
    if (!mappingHandle_) {
        CloseHandle(fileHandle_);
        fileHandle_ = INVALID_HANDLE_VALUE;
        return false;
    }

    data_ = MapViewOfFile(mappingHandle_, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (!data_) {
        CloseHandle(mappingHandle_);
        mappingHandle_ = nullptr;
        CloseHandle(fileHandle_);
        fileHandle_ = INVALID_HANDLE_VALUE;
        return false;
    }
#else
    fd_ = ::open(path.string().c_str(), O_RDWR | O_CREAT, 0666);
    if (fd_ < 0) {
        return false;
    }
    if (::ftruncate(fd_, static_cast<off_t>(size)) != 0) {
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    data_ = mmap(nullptr,
                 size,
                 PROT_READ | PROT_WRITE,
                 MAP_SHARED,
                 fd_,
                 0);
    if (data_ == MAP_FAILED) {
        ::close(fd_);
        fd_ = -1;
        data_ = nullptr;
        return false;
    }
#endif
    size_ = size;
    return true;
}

void MemoryMappedFile::unmap() {
    if (!data_) {
        return;
    }
#ifdef _WIN32
    UnmapViewOfFile(data_);
    data_ = nullptr;
    if (mappingHandle_) {
        CloseHandle(mappingHandle_);
        mappingHandle_ = nullptr;
    }
    if (fileHandle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(fileHandle_);
        fileHandle_ = INVALID_HANDLE_VALUE;
    }
#else
    munmap(data_, size_);
    data_ = nullptr;
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
#endif
    size_ = 0;
}

void* MemoryMappedFile::data() const {
    return data_;
}

std::size_t MemoryMappedFile::size() const {
    return size_;
}

bool MemoryMappedFile::flush(std::size_t length) {
    if (!data_ || length == 0) {
        return false;
    }
    const std::size_t flushLength = length < size_ ? length : size_;
#ifdef _WIN32
    if (!FlushViewOfFile(data_, static_cast<SIZE_T>(flushLength)) ||
        !FlushFileBuffers(fileHandle_)) {
        return false;
    }
#else
    if (msync(data_, flushLength, MS_SYNC) != 0) {
        return false;
    }
#endif
    return true;
}

}  // namespace rto
