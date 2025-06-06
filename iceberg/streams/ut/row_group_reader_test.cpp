#include "iceberg/streams/arrow/row_group_reader.h"

#include "gtest/gtest.h"
#include "iceberg/streams/ut/batch_maker.h"
#include "iceberg/streams/ut/local_file_reader_provider.h"
#include "iceberg/test_utils/assertions.h"
#include "iceberg/test_utils/column.h"
#include "iceberg/test_utils/write.h"

namespace iceberg {
namespace {

TEST(RowGroupReader, Trivial) {
  ScopedTempDir dir;

  std::string data_path = "file://" + (dir.path() / "data.parquet").generic_string();

  auto col1_data = OptionalVector<int32_t>{std::nullopt, 3, 7};
  auto col2_data = OptionalVector<int32_t>{std::nullopt, 4, 9};

  auto column1 = MakeInt32Column("f1", 1, col1_data);
  auto column2 = MakeInt32Column("f2", 2, col2_data);
  ASSERT_OK(WriteToFile({{column1, column2}, std::vector<size_t>{1, 1, 1}}, data_path));

  auto provider = std::make_shared<LocalFileReaderProvider>();
  ASSIGN_OR_FAIL(auto input, provider->Open(data_path));

  auto stream = std::make_shared<RowGroupReader>(input, 1, std::vector<int>{1});
  auto batch = stream->ReadNext();

  auto expected_result = MakeBatch({MakeInt32ArrowColumn({4})}, {"f2"});

  EXPECT_TRUE(batch->GetColumnByName("f2")->Equals(expected_result->GetColumnByName("f2")));

  ASSERT_EQ(stream->ReadNext(), nullptr);
}

TEST(RowGroupReader, WithRowNumber) {
  ScopedTempDir dir;

  std::string data_path = "file://" + (dir.path() / "data.parquet").generic_string();

  auto col1_data = OptionalVector<int32_t>{std::nullopt, 3, 7};
  auto col2_data = OptionalVector<int32_t>{std::nullopt, 4, 9};

  auto column1 = MakeInt32Column("f1", 1, col1_data);
  auto column2 = MakeInt32Column("f2", 2, col2_data);
  ASSERT_OK(WriteToFile({{column1, column2}, std::vector<size_t>{1, 1, 1}}, data_path));

  auto provider = std::make_shared<LocalFileReaderProvider>();
  ASSIGN_OR_FAIL(auto input, provider->Open(data_path));

  auto stream = std::make_shared<RowGroupReaderWithRowNumber>(input, 1, std::vector<int>{1});
  auto batch = stream->ReadNext();

  auto expected_result = MakeBatch({MakeInt32ArrowColumn({4})}, {"f2"});

  EXPECT_TRUE(batch->GetRecordBatch()->GetColumnByName("f2")->Equals(expected_result->GetColumnByName("f2")));
  EXPECT_EQ(batch->row_position, 1);

  ASSERT_EQ(stream->ReadNext(), nullptr);
}

}  // namespace
}  // namespace iceberg
