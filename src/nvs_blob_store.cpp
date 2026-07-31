#include "nvs_blob_store.h"

#include <nvs.h>
#include <new>

namespace NvsBlobStore {

Blob::~Blob() {
    delete[] data;
}

ReadStatus read(const char* nameSpace, const char* key, size_t maximumSize,
                Blob& blob) {
    delete[] blob.data;
    blob.data = nullptr;
    blob.size = 0;

    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open(nameSpace, NVS_READONLY, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) return ReadStatus::Missing;
    if (error != ESP_OK) return ReadStatus::IoError;

    size_t size = 0;
    error = nvs_get_blob(handle, key, nullptr, &size);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return ReadStatus::Missing;
    }
    if (error == ESP_ERR_NVS_TYPE_MISMATCH) {
        nvs_close(handle);
        return ReadStatus::WrongType;
    }
    if (error != ESP_OK) {
        nvs_close(handle);
        return ReadStatus::IoError;
    }
    if (size > maximumSize) {
        nvs_close(handle);
        return ReadStatus::TooLarge;
    }

    uint8_t* data = nullptr;
    if (size != 0) {
        data = new (std::nothrow) uint8_t[size];
        if (!data) {
            nvs_close(handle);
            return ReadStatus::IoError;
        }
        size_t bytesToRead = size;
        error = nvs_get_blob(handle, key, data, &bytesToRead);
        if (error != ESP_OK || bytesToRead != size) {
            delete[] data;
            nvs_close(handle);
            return ReadStatus::IoError;
        }
    }
    nvs_close(handle);
    blob.data = data;
    blob.size = size;
    return ReadStatus::Ok;
}

bool write(const char* nameSpace, const char* key,
           const uint8_t* data, size_t size) {
    if (!data && size != 0) return false;

    nvs_handle_t handle = 0;
    if (nvs_open(nameSpace, NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t error = nvs_set_blob(handle, key, data, size);
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
    return error == ESP_OK;
}

bool erase(const char* nameSpace, const char* key) {
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open(nameSpace, NVS_READWRITE, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) return true;
    if (error != ESP_OK) return false;

    error = nvs_erase_key(handle, key);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return true;
    }
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
    return error == ESP_OK;
}

} // namespace NvsBlobStore
