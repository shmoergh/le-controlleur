#include "storage-mock.h"

#include <algorithm>
#include <cstring>

namespace {

struct MockStorageState {
	StorageStatus read_status = StorageStatus::kNotFound;
	StorageStatus write_status = StorageStatus::kOk;
	std::vector<uint8_t> persisted_blob;
	std::size_t write_count = 0;
};

MockStorageState g_state;

}  // namespace

namespace storage_mock {

void reset() {
	g_state = MockStorageState{};
}

void set_read_status(StorageStatus status) {
	g_state.read_status = status;
}

void set_write_status(StorageStatus status) {
	g_state.write_status = status;
}

void set_read_blob(const void* data, std::size_t size, StorageStatus status) {
	const auto* bytes = static_cast<const uint8_t*>(data);
	g_state.persisted_blob.assign(bytes, bytes + size);
	g_state.read_status = status;
}

std::size_t write_count() {
	return g_state.write_count;
}

const std::vector<uint8_t>& persisted_blob() {
	return g_state.persisted_blob;
}

}  // namespace storage_mock

StorageStatus read_app_blob(void* out, std::size_t max_size, std::size_t* actual_size) {
	if (actual_size == nullptr) {
		return StorageStatus::kInvalidArgument;
	}

	if (g_state.read_status != StorageStatus::kOk) {
		*actual_size = 0;
		return g_state.read_status;
	}

	*actual_size = g_state.persisted_blob.size();
	if (*actual_size > max_size) {
		return StorageStatus::kTooLarge;
	}

	if (*actual_size > 0 && out == nullptr) {
		return StorageStatus::kInvalidArgument;
	}

	if (*actual_size > 0) {
		std::memcpy(out, g_state.persisted_blob.data(), *actual_size);
	}
	return StorageStatus::kOk;
}

StorageStatus write_app_blob(const void* data, std::size_t size) {
	if (g_state.write_status != StorageStatus::kOk) {
		return g_state.write_status;
	}

	const auto* bytes = static_cast<const uint8_t*>(data);
	g_state.persisted_blob.assign(bytes, bytes + size);
	g_state.read_status = StorageStatus::kOk;
	++g_state.write_count;
	return StorageStatus::kOk;
}
