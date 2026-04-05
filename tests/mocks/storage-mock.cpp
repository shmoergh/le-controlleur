#include "storage-mock.h"

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

bool Storage::init(bool require_protected_layout) {
	require_protected_layout_ = require_protected_layout;
	initialized_ = true;
	return true;
}

bool Storage::is_initialized() const {
	return initialized_;
}

bool Storage::is_layout_protected() const {
	return true;
}

uint32_t Storage::region_offset(StorageRegion region) const {
	(void)region;
	return 0u;
}

size_t Storage::region_size(StorageRegion region) const {
	(void)region;
	return 0u;
}

StorageStatus Storage::read_region(StorageRegion region, uint32_t offset, void* out, size_t size) const {
	(void)region;
	(void)offset;
	(void)out;
	(void)size;
	return StorageStatus::kNotPermitted;
}

StorageStatus Storage::write_region(StorageRegion region, uint32_t offset, const void* data, size_t size) const {
	(void)region;
	(void)offset;
	(void)data;
	(void)size;
	return StorageStatus::kNotPermitted;
}

StorageStatus Storage::erase_region(StorageRegion region) const {
	(void)region;
	return StorageStatus::kNotPermitted;
}

StorageStatus Storage::read_cv_calibration(CvCalibrationV1* out) const {
	(void)out;
	return StorageStatus::kNotPermitted;
}

StorageStatus Storage::write_cv_calibration(const CvCalibrationV1* in) const {
	(void)in;
	return StorageStatus::kNotPermitted;
}

StorageStatus Storage::clear_cv_calibration() const {
	return StorageStatus::kNotPermitted;
}

StorageStatus Storage::read_app_blob(void* out, size_t max_size, size_t* actual_size) const {
	return ::read_app_blob(out, max_size, actual_size);
}

StorageStatus Storage::write_app_blob(const void* data, size_t size) const {
	return ::write_app_blob(data, size);
}

StorageStatus Storage::clear_app_blob() const {
	g_state.persisted_blob.clear();
	g_state.read_status = StorageStatus::kNotFound;
	return StorageStatus::kOk;
}

StorageStatus Storage::check_ready_(bool write_operation) const {
	(void)write_operation;
	return initialized_ ? StorageStatus::kOk : StorageStatus::kNotPermitted;
}
