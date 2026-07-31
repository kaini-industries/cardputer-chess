#ifndef NVS_BLOB_STORE_H
#define NVS_BLOB_STORE_H

#include <cstddef>
#include <cstdint>

namespace NvsBlobStore {

enum class ReadStatus : uint8_t {
    Ok,
    Missing,
    WrongType,
    TooLarge,
    IoError,
};

struct Blob {
    uint8_t* data = nullptr;
    size_t size = 0;

    Blob() = default;
    Blob(const Blob&) = delete;
    Blob& operator=(const Blob&) = delete;
    ~Blob();
};

// Reads an NVS blob into heap storage. The caller owns the returned Blob.
// TooLarge is distinct so callers can preserve records from future formats.
ReadStatus read(const char* nameSpace, const char* key, size_t maximumSize,
                Blob& blob);

// set_blob + commit. Returns true only after the commit succeeds.
bool write(const char* nameSpace, const char* key,
           const uint8_t* data, size_t size);

// Idempotent erase: an absent namespace/key is considered success.
bool erase(const char* nameSpace, const char* key);

} // namespace NvsBlobStore

#endif // NVS_BLOB_STORE_H
