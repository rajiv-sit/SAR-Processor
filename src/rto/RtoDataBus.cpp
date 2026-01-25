#include "rto/RtoDataBus.hpp"

#include <bit>
#include <cstring>
#include <limits>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace rto {

namespace {

constexpr std::size_t kHeaderSize = 16;

void writeU32Le(std::uint8_t* dest, std::uint32_t value) {
    dest[0] = static_cast<std::uint8_t>(value & 0xFF);
    dest[1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    dest[2] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
    dest[3] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
}

void writeU64Le(std::uint8_t* dest, std::uint64_t value) {
    dest[0] = static_cast<std::uint8_t>(value & 0xFF);
    dest[1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    dest[2] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
    dest[3] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
    dest[4] = static_cast<std::uint8_t>((value >> 32) & 0xFF);
    dest[5] = static_cast<std::uint8_t>((value >> 40) & 0xFF);
    dest[6] = static_cast<std::uint8_t>((value >> 48) & 0xFF);
    dest[7] = static_cast<std::uint8_t>((value >> 56) & 0xFF);
}

std::string stripPrefix(const std::string& value, const std::string& prefix) {
    if (value.rfind(prefix, 0) == 0) {
        return value.substr(prefix.size());
    }
    return value;
}

bool resolveAddress(const std::string& host,
                    std::uint16_t port,
                    sockaddr_in& addr) {
    addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (host.empty()) {
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        return true;
    }

    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) == 1) {
        return true;
    }

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* result = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || !result) {
        return false;
    }
    auto* addrIn = reinterpret_cast<sockaddr_in*>(result->ai_addr);
    addr.sin_addr = addrIn->sin_addr;
    freeaddrinfo(result);
    return true;
}

}  // namespace

RtoDataBus::RtoDataBus(std::string endpoint)
    : endpoint_(std::move(endpoint)) {
    std::string raw = stripPrefix(endpoint_, "udp://");
    const auto colon = raw.find(':');
    if (colon != std::string::npos) {
        host_ = raw.substr(0, colon);
        port_ = static_cast<std::uint16_t>(std::stoi(raw.substr(colon + 1)));
    }
}

bool RtoDataBus::publish(const RtoFrame& frame) {
    if (host_.empty() || port_ == 0 || frame.width == 0 || frame.height == 0) {
        return false;
    }

#ifdef _WIN32
    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return false;
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) return (WSACleanup(), false);
#else
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        return false;
    }
#endif

    sockaddr_in addr{};
    if (!resolveAddress(host_, port_, addr)) {
#ifdef _WIN32
        closesocket(sock);
        WSACleanup();
#else
        close(sock);
#endif
        return false;
    }

    const std::size_t payloadSize = kHeaderSize + frame.pixels.size() * sizeof(float);
    if (payloadSize > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
#ifdef _WIN32
        closesocket(sock);
        WSACleanup();
#else
        close(sock);
#endif
        return false;
    }

    std::vector<std::uint8_t> payload(payloadSize);
    writeU32Le(payload.data(), frame.width);
    writeU32Le(payload.data() + 4, frame.height);
    writeU64Le(payload.data() + 8, frame.timestampNs);

    if (!frame.pixels.empty()) {
        std::uint8_t* pixelPtr = payload.data() + kHeaderSize;
        if (std::endian::native == std::endian::little) {
            std::memcpy(pixelPtr,
                        frame.pixels.data(),
                        frame.pixels.size() * sizeof(float));
        } else {
            for (std::size_t i = 0; i < frame.pixels.size(); ++i) {
                std::uint32_t raw = 0;
                std::memcpy(&raw, &frame.pixels[i], sizeof(raw));
                writeU32Le(pixelPtr + i * sizeof(float), raw);
            }
        }
    }

    const auto sent = sendto(sock,
                             reinterpret_cast<const char*>(payload.data()),
                             static_cast<int>(payload.size()),
                             0,
                             reinterpret_cast<sockaddr*>(&addr),
                             sizeof(addr));

#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif

    return sent == static_cast<int>(payload.size());
}

}  // namespace rto
