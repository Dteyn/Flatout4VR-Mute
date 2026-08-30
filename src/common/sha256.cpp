#include "common/sha256.h"

#include <windows.h>
#include <bcrypt.h>

#include <array>
#include <iomanip>
#include <memory>
#include <sstream>
#include <vector>

namespace fl4tout {
namespace {

class HandleGuard {
public:
    explicit HandleGuard(HANDLE handle) : handle_(handle) {}
    ~HandleGuard() {
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
    }
    HANDLE get() const { return handle_; }

private:
    HANDLE handle_;
};

struct AlgorithmDeleter {
    void operator()(void* handle) const {
        if (handle != nullptr) {
            BCryptCloseAlgorithmProvider(static_cast<BCRYPT_ALG_HANDLE>(handle), 0);
        }
    }
};

struct HashDeleter {
    void operator()(void* handle) const {
        if (handle != nullptr) {
            BCryptDestroyHash(static_cast<BCRYPT_HASH_HANDLE>(handle));
        }
    }
};

using AlgorithmHandle = std::unique_ptr<void, AlgorithmDeleter>;
using HashHandle = std::unique_ptr<void, HashDeleter>;

bool GetDwordProperty(BCRYPT_HANDLE handle, const wchar_t* property, DWORD& value) {
    DWORD written = 0;
    return BCryptGetProperty(
        handle, property, reinterpret_cast<PUCHAR>(&value), sizeof(value), &written, 0) >= 0 &&
        written == sizeof(value);
}

}  // namespace

std::optional<std::string> Sha256File(const std::filesystem::path& path) {
    HandleGuard file(CreateFileW(
        path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (file.get() == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }

    BCRYPT_ALG_HANDLE algorithm_raw = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm_raw, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) {
        return std::nullopt;
    }
    AlgorithmHandle algorithm(algorithm_raw);

    DWORD object_size = 0;
    DWORD hash_size = 0;
    if (!GetDwordProperty(algorithm.get(), BCRYPT_OBJECT_LENGTH, object_size) ||
        !GetDwordProperty(algorithm.get(), BCRYPT_HASH_LENGTH, hash_size)) {
        return std::nullopt;
    }

    std::vector<UCHAR> hash_object(object_size);
    std::vector<UCHAR> digest(hash_size);
    BCRYPT_HASH_HANDLE hash_raw = nullptr;
    if (BCryptCreateHash(
            algorithm.get(), &hash_raw, hash_object.data(), static_cast<ULONG>(hash_object.size()),
            nullptr, 0, 0) < 0) {
        return std::nullopt;
    }
    HashHandle hash(hash_raw);

    std::array<UCHAR, 64 * 1024> buffer{};
    for (;;) {
        DWORD bytes_read = 0;
        if (!ReadFile(file.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read,
                      nullptr)) {
            return std::nullopt;
        }
        if (bytes_read == 0) {
            break;
        }
        if (BCryptHashData(hash.get(), buffer.data(), bytes_read, 0) < 0) {
            return std::nullopt;
        }
    }

    if (BCryptFinishHash(hash.get(), digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) {
        return std::nullopt;
    }

    std::ostringstream text;
    text << std::hex << std::setfill('0');
    for (const UCHAR byte : digest) {
        text << std::setw(2) << static_cast<unsigned>(byte);
    }
    return text.str();
}

}  // namespace fl4tout
