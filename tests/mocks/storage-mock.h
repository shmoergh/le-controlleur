#ifndef TEST_MOCK_STORAGE_MOCK_H
#define TEST_MOCK_STORAGE_MOCK_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "brain/include/storage.h"

namespace storage_mock {

void reset();
void set_read_status(StorageStatus status);
void set_write_status(StorageStatus status);
void set_read_blob(const void* data, std::size_t size, StorageStatus status = StorageStatus::kOk);

std::size_t write_count();
const std::vector<uint8_t>& persisted_blob();

}  // namespace storage_mock

#endif
