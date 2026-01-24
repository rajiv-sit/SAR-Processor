#include "rto/RtoDataBus.hpp"

#include <cstring>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace rto {

namespace {

struct FrameHeader {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t timestampNs = 0;
};

std::string stripPrefix(const std::string& value, const std::string& prefix) {
    if (value.rfind(prefix, 0) == 0) {
        return value.substr(prefix.size());
    }
    return value;
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

    FrameHeader header{};
    header.width = frame.width;
    header.height = frame.height;
    header.timestampNs = frame.timestampNs;

    std::vector<std::uint8_t> payload(sizeof(FrameHeader) + frame.pixels.size() * sizeof(float));
    std::memcpy(payload.data(), &header, sizeof(FrameHeader));
    if (!frame.pixels.empty()) {
        std::memcpy(payload.data() + sizeof(FrameHeader),
                    frame.pixels.data(),
                    frame.pixels.size() * sizeof(float));
    }

#ifdef _WIN32
    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return false;
    }
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }
#else
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        return false;
    }
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    if (inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) != 1) {
#ifdef _WIN32
        closesocket(sock);
        WSACleanup();
#else
        close(sock);
#endif
        return false;
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
