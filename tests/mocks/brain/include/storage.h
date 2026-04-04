#ifndef TEST_MOCK_BRAIN_INCLUDE_STORAGE_H
#define TEST_MOCK_BRAIN_INCLUDE_STORAGE_H

#include <cstddef>
#include <cstdint>

namespace StorageLayout {
constexpr std::size_t kAppDataRegionSizeBytes = 4096;
}

enum class StorageStatus : uint8_t {
	kOk = 0,
	kInvalidArgument,
	kNotFound,
	kCorrupt,
	kOutOfBounds,
	kTooLarge,
	kUnprotectedLayout,
	kFlashError,
	kTimeout,
	kNotPermitted,
};

StorageStatus read_app_blob(void* out, std::size_t max_size, std::size_t* actual_size);
StorageStatus write_app_blob(const void* data, std::size_t size);

#endif
