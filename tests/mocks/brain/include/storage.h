#ifndef TEST_MOCK_BRAIN_INCLUDE_STORAGE_H
#define TEST_MOCK_BRAIN_INCLUDE_STORAGE_H

#include <cstddef>
#include <cstdint>

namespace StorageLayout {
constexpr std::size_t kAppDataRegionSizeBytes = 4096;
}

enum class StorageRegion : uint8_t {
	kAppData = 0,
	kCalibration,
};

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

struct CvCalibrationV1 {
	int16_t a_offset_lsb[10];
	int16_t b_offset_lsb[10];
};

class Storage {
public:
	bool init(bool require_protected_layout = false);
	bool is_initialized() const;

	bool is_layout_protected() const;
	uint32_t region_offset(StorageRegion region) const;
	std::size_t region_size(StorageRegion region) const;

	StorageStatus read_region(StorageRegion region, uint32_t offset, void* out, std::size_t size) const;
	StorageStatus write_region(StorageRegion region, uint32_t offset, const void* data, std::size_t size) const;
	StorageStatus erase_region(StorageRegion region) const;

	StorageStatus read_cv_calibration(CvCalibrationV1* out) const;
	StorageStatus write_cv_calibration(const CvCalibrationV1* in) const;
	StorageStatus clear_cv_calibration() const;

	StorageStatus read_app_blob(void* out, std::size_t max_size, std::size_t* actual_size) const;
	StorageStatus write_app_blob(const void* data, std::size_t size) const;
	StorageStatus clear_app_blob() const;

private:
	StorageStatus check_ready_(bool write_operation) const;

	bool initialized_ = false;
	bool require_protected_layout_ = false;
};

StorageStatus read_app_blob(void* out, std::size_t max_size, std::size_t* actual_size);
StorageStatus write_app_blob(const void* data, std::size_t size);

#endif
