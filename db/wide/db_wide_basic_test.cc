//  Copyright (c) Meta Platforms, Inc. and affiliates.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#include <array>
#include <cctype>
#include <memory>

#include "db/db_test_util.h"
#include "port/stack_trace.h"
#include "test_util/testutil.h"
#include "util/overload.h"
#include "utilities/merge_operators.h"

namespace ROCKSDB_NAMESPACE {

class DBWideBasicTest : public DBTestBase {
 protected:
  explicit DBWideBasicTest()
      : DBTestBase("db_wide_basic_test", /* env_do_fsync */ false) {}
};

TEST_F(DBWideBasicTest, PutEntity) {
  Options options = GetDefaultOptions();

  // Write a couple of wide-column entities and a plain old key-value, then read
  // them back.
  constexpr char first_key[] = "first";
  constexpr char first_value_of_default_column[] = "hello";
  WideColumns first_columns{
      {kDefaultWideColumnName, first_value_of_default_column},
      {"attr_name1", "foo"},
      {"attr_name2", "bar"}};

  constexpr char second_key[] = "second";
  WideColumns second_columns{{"attr_one", "two"}, {"attr_three", "four"}};

  constexpr char third_key[] = "third";
  constexpr char third_value[] = "baz";

  auto verify = [&]() {
    const WideColumns expected_third_columns{
        {kDefaultWideColumnName, third_value}};

    {
      PinnableSlice result;
      ASSERT_OK(db_->Get(ReadOptions(), db_->DefaultColumnFamily(), first_key,
                         &result));
      ASSERT_EQ(result, first_value_of_default_column);
    }

    {
      PinnableWideColumns result;
      ASSERT_OK(db_->GetEntity(ReadOptions(), db_->DefaultColumnFamily(),
                               first_key, &result));
      ASSERT_EQ(result.columns(), first_columns);
    }

    {
      PinnableSlice result;
      ASSERT_OK(db_->Get(ReadOptions(), db_->DefaultColumnFamily(), second_key,
                         &result));
      ASSERT_TRUE(result.empty());
    }

    {
      PinnableWideColumns result;
      ASSERT_OK(db_->GetEntity(ReadOptions(), db_->DefaultColumnFamily(),
                               second_key, &result));
      ASSERT_EQ(result.columns(), second_columns);
    }

    {
      PinnableSlice result;
      ASSERT_OK(db_->Get(ReadOptions(), db_->DefaultColumnFamily(), third_key,
                         &result));
      ASSERT_EQ(result, third_value);
    }

    {
      PinnableWideColumns result;
      ASSERT_OK(db_->GetEntity(ReadOptions(), db_->DefaultColumnFamily(),
                               third_key, &result));

      ASSERT_EQ(result.columns(), expected_third_columns);
    }

    {
      constexpr size_t num_keys = 3;

      std::array<Slice, num_keys> keys{{first_key, second_key, third_key}};
      std::array<PinnableSlice, num_keys> values;
      std::array<Status, num_keys> statuses;

      db_->MultiGet(ReadOptions(), db_->DefaultColumnFamily(), num_keys,
                    &keys[0], &values[0], &statuses[0]);

      ASSERT_OK(statuses[0]);
      ASSERT_EQ(values[0], first_value_of_default_column);

      ASSERT_OK(statuses[1]);
      ASSERT_TRUE(values[1].empty());

      ASSERT_OK(statuses[2]);
      ASSERT_EQ(values[2], third_value);
    }

    {
      constexpr size_t num_keys = 3;

      std::array<Slice, num_keys> keys{{first_key, second_key, third_key}};
      std::array<PinnableWideColumns, num_keys> results;
      std::array<Status, num_keys> statuses;

      db_->MultiGetEntity(ReadOptions(), db_->DefaultColumnFamily(), num_keys,
                          &keys[0], &results[0], &statuses[0]);

      ASSERT_OK(statuses[0]);
      ASSERT_EQ(results[0].columns(), first_columns);

      ASSERT_OK(statuses[1]);
      ASSERT_EQ(results[1].columns(), second_columns);

      ASSERT_OK(statuses[2]);
      ASSERT_EQ(results[2].columns(), expected_third_columns);
    }

    {
      std::unique_ptr<Iterator> iter(db_->NewIterator(ReadOptions()));

      iter->SeekToFirst();
      ASSERT_TRUE(iter->Valid());
      ASSERT_OK(iter->status());
      ASSERT_EQ(iter->key(), first_key);
      ASSERT_EQ(iter->value(), first_value_of_default_column);
      ASSERT_EQ(iter->columns(), first_columns);

      iter->Next();
      ASSERT_TRUE(iter->Valid());
      ASSERT_OK(iter->status());
      ASSERT_EQ(iter->key(), second_key);
      ASSERT_TRUE(iter->value().empty());
      ASSERT_EQ(iter->columns(), second_columns);

      iter->Next();
      ASSERT_TRUE(iter->Valid());
      ASSERT_OK(iter->status());
      ASSERT_EQ(iter->key(), third_key);
      ASSERT_EQ(iter->value(), third_value);
      ASSERT_EQ(iter->columns(), expected_third_columns);

      iter->Next();
      ASSERT_FALSE(iter->Valid());
      ASSERT_OK(iter->status());

      iter->SeekToLast();
      ASSERT_TRUE(iter->Valid());
      ASSERT_OK(iter->status());
      ASSERT_EQ(iter->key(), third_key);
      ASSERT_EQ(iter->value(), third_value);
      ASSERT_EQ(iter->columns(), expected_third_columns);

      iter->Prev();
      ASSERT_TRUE(iter->Valid());
      ASSERT_OK(iter->status());
      ASSERT_EQ(iter->key(), second_key);
      ASSERT_TRUE(iter->value().empty());
      ASSERT_EQ(iter->columns(), second_columns);

      iter->Prev();
      ASSERT_TRUE(iter->Valid());
      ASSERT_OK(iter->status());
      ASSERT_EQ(iter->key(), first_key);
      ASSERT_EQ(iter->value(), first_value_of_default_column);
      ASSERT_EQ(iter->columns(), first_columns);

      iter->Prev();
      ASSERT_FALSE(iter->Valid());
      ASSERT_OK(iter->status());
    }
  };

  // Use the DB::PutEntity API to write the first entity
  ASSERT_OK(db_->PutEntity(WriteOptions(), db_->DefaultColumnFamily(),
                           first_key, first_columns));

  // Use WriteBatch to write the second entity
  WriteBatch batch;
  ASSERT_OK(
      batch.PutEntity(db_->DefaultColumnFamily(), second_key, second_columns));
  ASSERT_OK(db_->Write(WriteOptions(), &batch));

  // Use Put to write the plain key-value
  ASSERT_OK(db_->Put(WriteOptions(), db_->DefaultColumnFamily(), third_key,
                     third_value));

  // Try reading from memtable
  verify();

  // Try reading after recovery
  Close();
  options.avoid_flush_during_recovery = true;
  Reopen(options);

  verify();

  // Try reading from storage
  ASSERT_OK(Flush());

  verify();

  // Reopen as Readonly DB and verify
  Close();
  ASSERT_OK(ReadOnlyReopen(options));
  verify();
}

TEST_F(DBWideBasicTest, PutEntityColumnFamily) {
  Options options = GetDefaultOptions();
  CreateAndReopenWithCF({"corinthian"}, options);

  // Use the DB::PutEntity API
  constexpr char first_key[] = "first";
  WideColumns first_columns{{"attr_name1", "foo"}, {"attr_name2", "bar"}};

  ASSERT_OK(
      db_->PutEntity(WriteOptions(), handles_[1], first_key, first_columns));

  // Use WriteBatch
  constexpr char second_key[] = "second";
  WideColumns second_columns{{"attr_one", "two"}, {"attr_three", "four"}};

  WriteBatch batch;
  ASSERT_OK(batch.PutEntity(handles_[1], second_key, second_columns));
  ASSERT_OK(db_->Write(WriteOptions(), &batch));
}

TEST_F(DBWideBasicTest, MultiCFMultiGetEntity) {
  Options options = GetDefaultOptions();
  CreateAndReopenWithCF({"corinthian"}, options);

  constexpr char first_key[] = "first";
  WideColumns first_columns{{"attr_name1", "foo"}, {"attr_name2", "bar"}};

  ASSERT_OK(db_->PutEntity(WriteOptions(), db_->DefaultColumnFamily(),
                           first_key, first_columns));

  constexpr char second_key[] = "second";
  WideColumns second_columns{{"attr_one", "two"}, {"attr_three", "four"}};

  ASSERT_OK(
      db_->PutEntity(WriteOptions(), handles_[1], second_key, second_columns));

  constexpr size_t num_keys = 2;

  std::array<ColumnFamilyHandle*, num_keys> column_families{
      {db_->DefaultColumnFamily(), handles_[1]}};
  std::array<Slice, num_keys> keys{{first_key, second_key}};
  std::array<PinnableWideColumns, num_keys> results;
  std::array<Status, num_keys> statuses;

  db_->MultiGetEntity(ReadOptions(), num_keys, &column_families[0], &keys[0],
                      &results[0], &statuses[0]);

  ASSERT_OK(statuses[0]);
  ASSERT_EQ(results[0].columns(), first_columns);

  ASSERT_OK(statuses[1]);
  ASSERT_EQ(results[1].columns(), second_columns);
}

TEST_F(DBWideBasicTest, MergePlainKeyValue) {
  Options options = GetDefaultOptions();
  options.create_if_missing = true;
  options.merge_operator = MergeOperators::CreateStringAppendOperator();
  Reopen(options);

  // Put + Merge
  constexpr char first_key[] = "first";
  constexpr char first_base_value[] = "hello";
  constexpr char first_merge_op[] = "world";

  // Delete + Merge
  constexpr char second_key[] = "second";
  constexpr char second_merge_op[] = "foo";

  // Merge without any preceding KV
  constexpr char third_key[] = "third";
  constexpr char third_merge_op[] = "bar";

  auto write_base = [&]() {
    // Write "base" KVs: a Put for the 1st key and a Delete for the 2nd one;
    // note there is no "base" KV for the 3rd
    ASSERT_OK(db_->Put(WriteOptions(), db_->DefaultColumnFamily(), first_key,
                       first_base_value));
    ASSERT_OK(
        db_->Delete(WriteOptions(), db_->DefaultColumnFamily(), second_key));
  };

  auto write_merge = [&]() {
    // Write Merge operands
    ASSERT_OK(db_->Merge(WriteOptions(), db_->DefaultColumnFamily(), first_key,
                         first_merge_op));
    ASSERT_OK(db_->Merge(WriteOptions(), db_->DefaultColumnFamily(), second_key,
                         second_merge_op));
    ASSERT_OK(db_->Merge(WriteOptions(), db_->DefaultColumnFamily(), third_key,
                         third_merge_op));
  };

  const std::string expected_first_column(std::string(first_base_value) + "," +
                                          first_merge_op);
  const WideColumns expected_first_columns{
      {kDefaultWideColumnName, expected_first_column}};
  const WideColumns expected_second_columns{
      {kDefaultWideColumnName, second_merge_op}};
  const WideColumns expected_third_columns{
      {kDefaultWideColumnName, third_merge_op}};

  auto verify = [&]() {
    {
      PinnableWideColumns result;
      ASSERT_OK(db_->GetEntity(ReadOptions(), db_->DefaultColumnFamily(),
                               first_key, &result));
      ASSERT_EQ(result.columns(), expected_first_columns);
    }

    {
      PinnableWideColumns result;
      ASSERT_OK(db_->GetEntity(ReadOptions(), db_->DefaultColumnFamily(),
                               second_key, &result));
      ASSERT_EQ(result.columns(), expected_second_columns);
    }

    {
      PinnableWideColumns result;
      ASSERT_OK(db_->GetEntity(ReadOptions(), db_->DefaultColumnFamily(),
                               third_key, &result));

      ASSERT_EQ(result.columns(), expected_third_columns);
    }

    {
      constexpr size_t num_keys = 3;

      std::array<Slice, num_keys> keys{{first_key, second_key, third_key}};
      std::array<PinnableWideColumns, num_keys> results;
      std::array<Status, num_keys> statuses;

      db_->MultiGetEntity(ReadOptions(), db_->DefaultColumnFamily(), num_keys,
                          &keys[0], &results[0], &statuses[0]);

      ASSERT_OK(statuses[0]);
      ASSERT_EQ(results[0].columns(), expected_first_columns);

      ASSERT_OK(statuses[1]);
      ASSERT_EQ(results[1].columns(), expected_second_columns);

      ASSERT_OK(statuses[2]);
      ASSERT_EQ(results[2].columns(), expected_third_columns);
    }

    {
      std::unique_ptr<Iterator> iter(db_->NewIterator(ReadOptions()));

      iter->SeekToFirst();
      ASSERT_TRUE(iter->Valid());
      ASSERT_OK(iter->status());
      ASSERT_EQ(iter->key(), first_key);
      ASSERT_EQ(iter->value(), expected_first_columns[0].value());
      ASSERT_EQ(iter->columns(), expected_first_columns);

      iter->Next();
      ASSERT_TRUE(iter->Valid());
      ASSERT_OK(iter->status());
      ASSERT_EQ(iter->key(), second_key);
      ASSERT_EQ(iter->value(), expected_second_columns[0].value());
      ASSERT_EQ(iter->columns(), expected_second_columns);

      iter->Next();
      ASSERT_TRUE(iter->Valid());
      ASSERT_OK(iter->status());
      ASSERT_EQ(iter->key(), third_key);
      ASSERT_EQ(iter->value(), expected_third_columns[0].value());
      ASSERT_EQ(iter->columns(), expected_third_columns);

      iter->Next();
      ASSERT_FALSE(iter->Valid());
      ASSERT_OK(iter->status());

      iter->SeekToLast();
      ASSERT_TRUE(iter->Valid());
      ASSERT_OK(iter->status());
      ASSERT_EQ(iter->key(), third_key);
      ASSERT_EQ(iter->value(), expected_third_columns[0].value());
      ASSERT_EQ(iter->columns(), expected_third_columns);

      iter->Prev();
      ASSERT_TRUE(iter->Valid());
      ASSERT_OK(iter->status());
      ASSERT_EQ(iter->key(), second_key);
      ASSERT_EQ(iter->value(), expected_second_columns[0].value());
      ASSERT_EQ(iter->columns(), expected_second_columns);

      iter->Prev();
      ASSERT_TRUE(iter->Valid());
      ASSERT_OK(iter->status());
      ASSERT_EQ(iter->key(), first_key);
      ASSERT_EQ(iter->value(), expected_first_columns[0].value());
      ASSERT_EQ(iter->columns(), expected_first_columns);

      iter->Prev();
      ASSERT_FALSE(iter->Valid());
      ASSERT_OK(iter->status());
    }
  };

  {
    // Base KVs (if any) and Merge operands both in memtable (note: we take a
    // snapshot in between to make sure they do not get reconciled during the
    // subsequent flush)
    write_base();
    ManagedSnapshot snapshot(db_);
    write_merge();
    verify();

    // Base KVs (if any) and Merge operands both in storage
    ASSERT_OK(Flush());
    verify();
  }

  // Base KVs (if any) in storage, Merge operands in memtable
  DestroyAndReopen(options);
  write_base();
  ASSERT_OK(Flush());
  write_merge();
  verify();
}

TEST_F(DBWideBasicTest, MergeEntity) {
  Options options = GetDefaultOptions();
  options.create_if_missing = true;

  const std::string delim("|");
  options.merge_operator = MergeOperators::CreateStringAppendOperator(delim);

  Reopen(options);

  // Test Merge with two entities: one that has the default column and one that
  // doesn't
  constexpr char first_key[] = "first";
  WideColumns first_columns{{kDefaultWideColumnName, "a"},
                            {"attr_name1", "foo"},
                            {"attr_name2", "bar"}};
  constexpr char first_merge_operand[] = "bla1";

  constexpr char second_key[] = "second";
  WideColumns second_columns{{"attr_one", "two"}, {"attr_three", "four"}};
  constexpr char second_merge_operand[] = "bla2";

  auto write_base = [&]() {
    // Use the DB::PutEntity API
    ASSERT_OK(db_->PutEntity(WriteOptions(), db_->DefaultColumnFamily(),
                             first_key, first_columns));

    // Use WriteBatch
    WriteBatch batch;
    ASSERT_OK(batch.PutEntity(db_->DefaultColumnFamily(), second_key,
                              second_columns));
    ASSERT_OK(db_->Write(WriteOptions(), &batch));
  };

  auto write_merge = [&]() {
    ASSERT_OK(db_->Merge(WriteOptions(), db_->DefaultColumnFamily(), first_key,
                         first_merge_operand));
    ASSERT_OK(db_->Merge(WriteOptions(), db_->DefaultColumnFamily(), second_key,
                         second_merge_operand));
  };

  const std::string first_expected_default(first_columns[0].value().ToString() +
                                           delim + first_merge_operand);
  const std::string second_expected_default(delim + second_merge_operand);

  auto verify_basic = [&]() {
    WideColumns first_expected_columns{
        {kDefaultWideColumnName, first_expected_default},
        first_columns[1],
        first_columns[2]};

    WideColumns second_expected_columns{
        {kDefaultWideColumnName, second_expected_default},
        second_columns[0],
        second_columns[1]};

    {
      PinnableSlice result;
      ASSERT_OK(db_->Get(ReadOptions(), db_->DefaultColumnFamily(), first_key,
                         &result));
      ASSERT_EQ(result, first_expected_default);
    }

    {
      PinnableWideColumns result;
      ASSERT_OK(db_->GetEntity(ReadOptions(), db_->DefaultColumnFamily(),
                               first_key, &result));
      ASSERT_EQ(result.columns(), first_expected_columns);
    }

    {
      PinnableSlice result;
      ASSERT_OK(db_->Get(ReadOptions(), db_->DefaultColumnFamily(), second_key,
                         &result));
      ASSERT_EQ(result, second_expected_default);
    }

    {
      PinnableWideColumns result;
      ASSERT_OK(db_->GetEntity(ReadOptions(), db_->DefaultColumnFamily(),
                               second_key, &result));
      ASSERT_EQ(result.columns(), second_expected_columns);
    }

    {
      constexpr size_t num_keys = 2;

      std::array<Slice, num_keys> keys{{first_key, second_key}};
      std::array<PinnableSlice, num_keys> values;
      std::array<Status, num_keys> statuses;

      db_->MultiGet(ReadOptions(), db_->DefaultColumnFamily(), num_keys,
                    &keys[0], &values[0], &statuses[0]);

      ASSERT_EQ(values[0], first_expected_default);
      ASSERT_OK(statuses[0]);

      ASSERT_EQ(values[1], second_expected_default);
      ASSERT_OK(statuses[1]);
    }

    {
      constexpr size_t num_keys = 2;

      std::array<Slice, num_keys> keys{{first_key, second_key}};
      std::array<PinnableWideColumns, num_keys> results;
      std::array<Status, num_keys> statuses;

      db_->MultiGetEntity(ReadOptions(), db_->DefaultColumnFamily(), num_keys,
                          &keys[0], &results[0], &statuses[0]);

      ASSERT_OK(statuses[0]);
      ASSERT_EQ(results[0].columns(), first_expected_columns);

      ASSERT_OK(statuses[1]);
      ASSERT_EQ(results[1].columns(), second_expected_columns);
    }

    {
      std::unique_ptr<Iterator> iter(db_->NewIterator(ReadOptions()));

      iter->SeekToFirst();
      ASSERT_TRUE(iter->Valid());
      ASSERT_OK(iter->status());
      ASSERT_EQ(iter->key(), first_key);
      ASSERT_EQ(iter->value(), first_expected_default);
      ASSERT_EQ(iter->columns(), first_expected_columns);

      iter->Next();
      ASSERT_TRUE(iter->Valid());
      ASSERT_OK(iter->status());
      ASSERT_EQ(iter->key(), second_key);
      ASSERT_EQ(iter->value(), second_expected_default);
      ASSERT_EQ(iter->columns(), second_expected_columns);

      iter->Next();
      ASSERT_FALSE(iter->Valid());
      ASSERT_OK(iter->status());

      iter->SeekToLast();
      ASSERT_TRUE(iter->Valid());
      ASSERT_OK(iter->status());
      ASSERT_EQ(iter->key(), second_key);
      ASSERT_EQ(iter->value(), second_expected_default);
      ASSERT_EQ(iter->columns(), second_expected_columns);

      iter->Prev();
      ASSERT_TRUE(iter->Valid());
      ASSERT_OK(iter->status());
      ASSERT_EQ(iter->key(), first_key);
      ASSERT_EQ(iter->value(), first_expected_default);
      ASSERT_EQ(iter->columns(), first_expected_columns);

      iter->Prev();
      ASSERT_FALSE(iter->Valid());
      ASSERT_OK(iter->status());
    }
  };

  auto verify_merge_ops_pre_compaction = [&]() {
    constexpr size_t num_merge_operands = 2;

    GetMergeOperandsOptions get_merge_opts;
    get_merge_opts.expected_max_number_of_operands = num_merge_operands;

    {
      std::array<PinnableSlice, num_merge_operands> merge_operands;
      int number_of_operands = 0;

      ASSERT_OK(db_->GetMergeOperands(ReadOptions(), db_->DefaultColumnFamily(),
                                      first_key, &merge_operands[0],
                                      &get_merge_opts, &number_of_operands));

      ASSERT_EQ(number_of_operands, num_merge_operands);
      ASSERT_EQ(merge_operands[0], first_columns[0].value());
      ASSERT_EQ(merge_operands[1], first_merge_operand);
    }

    {
      std::array<PinnableSlice, num_merge_operands> merge_operands;
      int number_of_operands = 0;

      ASSERT_OK(db_->GetMergeOperands(ReadOptions(), db_->DefaultColumnFamily(),
                                      second_key, &merge_operands[0],
                                      &get_merge_opts, &number_of_operands));

      ASSERT_EQ(number_of_operands, num_merge_operands);
      ASSERT_TRUE(merge_operands[0].empty());
      ASSERT_EQ(merge_operands[1], second_merge_operand);
    }
  };

  auto verify_merge_ops_post_compaction = [&]() {
    constexpr size_t num_merge_operands = 1;

    GetMergeOperandsOptions get_merge_opts;
    get_merge_opts.expected_max_number_of_operands = num_merge_operands;

    {
      std::array<PinnableSlice, num_merge_operands> merge_operands;
      int number_of_operands = 0;

      ASSERT_OK(db_->GetMergeOperands(ReadOptions(), db_->DefaultColumnFamily(),
                                      first_key, &merge_operands[0],
                                      &get_merge_opts, &number_of_operands));

      ASSERT_EQ(number_of_operands, num_merge_operands);
      ASSERT_EQ(merge_operands[0], first_expected_default);
    }

    {
      std::array<PinnableSlice, num_merge_operands> merge_operands;
      int number_of_operands = 0;

      ASSERT_OK(db_->GetMergeOperands(ReadOptions(), db_->DefaultColumnFamily(),
                                      second_key, &merge_operands[0],
                                      &get_merge_opts, &number_of_operands));

      ASSERT_EQ(number_of_operands, num_merge_operands);
      ASSERT_EQ(merge_operands[0], second_expected_default);
    }
  };

  {
    // Base KVs and Merge operands both in memtable (note: we take a snapshot in
    // between to make sure they do not get reconciled during the subsequent
    // flush)
    write_base();
    ManagedSnapshot snapshot(db_);
    write_merge();
    verify_basic();
    verify_merge_ops_pre_compaction();

    // Base KVs and Merge operands both in storage
    ASSERT_OK(Flush());
    verify_basic();
    verify_merge_ops_pre_compaction();
  }

  // Base KVs in storage, Merge operands in memtable
  DestroyAndReopen(options);
  write_base();
  ASSERT_OK(Flush());
  write_merge();
  verify_basic();
  verify_merge_ops_pre_compaction();

  // Flush and compact
  ASSERT_OK(Flush());
  ASSERT_OK(db_->CompactRange(CompactRangeOptions(), /* begin */ nullptr,
                              /* end */ nullptr));
  verify_basic();
  verify_merge_ops_post_compaction();
}

class DBWideMergeV3Test : public DBWideBasicTest {
 protected:
  void RunTest(const WideColumns& first_expected,
               const WideColumns& second_expected,
               const WideColumns& third_expected) {
    // Note: we'll take some snapshots to prevent merging during flush
    snapshots_.reserve(6);

    // Test reading from memtables
    WriteKeyValues();
    VerifyKeyValues(first_expected, second_expected, third_expected);
    VerifyMergeOperandCount(first_key, 2);
    VerifyMergeOperandCount(second_key, 3);
    VerifyMergeOperandCount(third_key, 3);

    // Test reading from SST files
    ASSERT_OK(Flush());
    VerifyKeyValues(first_expected, second_expected, third_expected);
    VerifyMergeOperandCount(first_key, 2);
    VerifyMergeOperandCount(second_key, 3);
    VerifyMergeOperandCount(third_key, 3);

    // Test reading from SSTs after compaction. Note that we write the same KVs
    // and flush again so we have two overlapping files. We also release the
    // snapshots so that the compaction can merge all keys.
    WriteKeyValues();
    ASSERT_OK(Flush());

    snapshots_.clear();

    ASSERT_OK(db_->CompactRange(CompactRangeOptions(), /* begin */ nullptr,
                                /* end */ nullptr));
    VerifyKeyValues(first_expected, second_expected, third_expected);
    VerifyMergeOperandCount(first_key, 1);
    VerifyMergeOperandCount(second_key, 1);
    VerifyMergeOperandCount(third_key, 1);
  }

  void WriteKeyValues() {
    // Base values
    ASSERT_OK(db_->Delete(WriteOptions(), db_->DefaultColumnFamily(),
                          first_key));  // no base value
    ASSERT_OK(db_->Put(WriteOptions(), db_->DefaultColumnFamily(), second_key,
                       second_base_value));  // plain base value
    ASSERT_OK(db_->PutEntity(WriteOptions(), db_->DefaultColumnFamily(),
                             third_key,
                             third_columns));  // wide-column base value

    snapshots_.emplace_back(db_);

    // First round of merge operands
    ASSERT_OK(db_->Merge(WriteOptions(), db_->DefaultColumnFamily(), first_key,
                         first_merge_op1));
    ASSERT_OK(db_->Merge(WriteOptions(), db_->DefaultColumnFamily(), second_key,
                         second_merge_op1));
    ASSERT_OK(db_->Merge(WriteOptions(), db_->DefaultColumnFamily(), third_key,
                         third_merge_op1));

    snapshots_.emplace_back(db_);

    // Second round of merge operands
    ASSERT_OK(db_->Merge(WriteOptions(), db_->DefaultColumnFamily(), first_key,
                         first_merge_op2));
    ASSERT_OK(db_->Merge(WriteOptions(), db_->DefaultColumnFamily(), second_key,
                         second_merge_op2));
    ASSERT_OK(db_->Merge(WriteOptions(), db_->DefaultColumnFamily(), third_key,
                         third_merge_op2));

    snapshots_.emplace_back(db_);
  }

  void VerifyKeyValues(const WideColumns& first_expected,
                       const WideColumns& second_expected,
                       const WideColumns& third_expected) {
    assert(!first_expected.empty() &&
           first_expected[0].name() == kDefaultWideColumnName);
    assert(!second_expected.empty() &&
           second_expected[0].name() == kDefaultWideColumnName);
    assert(!third_expected.empty() &&
           third_expected[0].name() == kDefaultWideColumnName);

    // Get
    {
      PinnableSlice result;
      ASSERT_OK(db_->Get(ReadOptions(), db_->DefaultColumnFamily(), first_key,
                         &result));
      ASSERT_EQ(result, first_expected[0].value());
    }

    {
      PinnableSlice result;
      ASSERT_OK(db_->Get(ReadOptions(), db_->DefaultColumnFamily(), second_key,
                         &result));
      ASSERT_EQ(result, second_expected[0].value());
    }

    {
      PinnableSlice result;
      ASSERT_OK(db_->Get(ReadOptions(), db_->DefaultColumnFamily(), third_key,
                         &result));
      ASSERT_EQ(result, third_expected[0].value());
    }

    // MultiGet
    {
      std::array<Slice, num_keys> keys{{first_key, second_key, third_key}};
      std::array<PinnableSlice, num_keys> values;
      std::array<Status, num_keys> statuses;

      db_->MultiGet(ReadOptions(), db_->DefaultColumnFamily(), num_keys,
                    keys.data(), values.data(), statuses.data());
      ASSERT_OK(statuses[0]);
      ASSERT_EQ(values[0], first_expected[0].value());
      ASSERT_OK(statuses[1]);
      ASSERT_EQ(values[1], second_expected[0].value());
      ASSERT_OK(statuses[2]);
      ASSERT_EQ(values[2], third_expected[0].value());
    }

    // GetEntity
    {
      PinnableWideColumns result;

      ASSERT_OK(db_->GetEntity(ReadOptions(), db_->DefaultColumnFamily(),
                               first_key, &result));
      ASSERT_EQ(result.columns(), first_expected);
    }

    {
      PinnableWideColumns result;

      ASSERT_OK(db_->GetEntity(ReadOptions(), db_->DefaultColumnFamily(),
                               second_key, &result));
      ASSERT_EQ(result.columns(), second_expected);
    }

    {
      PinnableWideColumns result;

      ASSERT_OK(db_->GetEntity(ReadOptions(), db_->DefaultColumnFamily(),
                               third_key, &result));
      ASSERT_EQ(result.columns(), third_expected);
    }

    // MultiGetEntity
    {
      std::array<Slice, num_keys> keys{{first_key, second_key, third_key}};
      std::array<PinnableWideColumns, num_keys> results;
      std::array<Status, num_keys> statuses;

      db_->MultiGetEntity(ReadOptions(), db_->DefaultColumnFamily(), num_keys,
                          keys.data(), results.data(), statuses.data());
      ASSERT_OK(statuses[0]);
      ASSERT_EQ(results[0].columns(), first_expected);
      ASSERT_OK(statuses[1]);
      ASSERT_EQ(results[1].columns(), second_expected);
      ASSERT_OK(statuses[2]);
      ASSERT_EQ(results[2].columns(), third_expected);
    }

    // Iterator
    {
      std::unique_ptr<Iterator> iter(db_->NewIterator(ReadOptions()));

      iter->SeekToFirst();
      ASSERT_TRUE(iter->Valid());
      ASSERT_OK(iter->status());
      ASSERT_EQ(iter->key(), first_key);
      ASSERT_EQ(iter->value(), first_expected[0].value());
      ASSERT_EQ(iter->columns(), first_expected);

      iter->Next();
      ASSERT_TRUE(iter->Valid());
      ASSERT_OK(iter->status());
      ASSERT_EQ(iter->key(), second_key);
      ASSERT_EQ(iter->value(), second_expected[0].value());
      ASSERT_EQ(iter->columns(), second_expected);

      iter->Next();
      ASSERT_TRUE(iter->Valid());
      ASSERT_OK(iter->status());
      ASSERT_EQ(iter->key(), third_key);
      ASSERT_EQ(iter->value(), third_expected[0].value());
      ASSERT_EQ(iter->columns(), third_expected);

      iter->Next();
      ASSERT_FALSE(iter->Valid());
      ASSERT_OK(iter->status());

      iter->SeekToLast();
      ASSERT_TRUE(iter->Valid());
      ASSERT_OK(iter->status());
      ASSERT_EQ(iter->key(), third_key);
      ASSERT_EQ(iter->value(), third_expected[0].value());
      ASSERT_EQ(iter->columns(), third_expected);

      iter->Prev();
      ASSERT_TRUE(iter->Valid());
      ASSERT_OK(iter->status());
      ASSERT_EQ(iter->key(), second_key);
      ASSERT_EQ(iter->value(), second_expected[0].value());
      ASSERT_EQ(iter->columns(), second_expected);

      iter->Prev();
      ASSERT_TRUE(iter->Valid());
      ASSERT_OK(iter->status());
      ASSERT_EQ(iter->key(), first_key);
      ASSERT_EQ(iter->value(), first_expected[0].value());
      ASSERT_EQ(iter->columns(), first_expected);

      iter->Prev();
      ASSERT_FALSE(iter->Valid());
      ASSERT_OK(iter->status());
    }
  }

  void VerifyMergeOperandCount(const Slice& key, int expected_merge_ops) {
    GetMergeOperandsOptions get_merge_opts;
    get_merge_opts.expected_max_number_of_operands = expected_merge_ops;

    std::vector<PinnableSlice> merge_operands(expected_merge_ops);
    int number_of_operands = 0;

    ASSERT_OK(db_->GetMergeOperands(ReadOptions(), db_->DefaultColumnFamily(),
                                    key, merge_operands.data(), &get_merge_opts,
                                    &number_of_operands));
    ASSERT_EQ(number_of_operands, expected_merge_ops);
  }

  std::vector<ManagedSnapshot> snapshots_;

  static constexpr size_t num_keys = 3;

  static constexpr char first_key[] = "first";
  static constexpr char first_merge_op1[] = "hello";
  static constexpr char first_merge_op1_upper[] = "HELLO";
  static constexpr char first_merge_op2[] = "world";
  static constexpr char first_merge_op2_upper[] = "WORLD";

  static constexpr char second_key[] = "second";
  static constexpr char second_base_value[] = "foo";
  static constexpr char second_base_value_upper[] = "FOO";
  static constexpr char second_merge_op1[] = "bar";
  static constexpr char second_merge_op1_upper[] = "BAR";
  static constexpr char second_merge_op2[] = "baz";
  static constexpr char second_merge_op2_upper[] = "BAZ";

  static constexpr char third_key[] = "third";
  static const WideColumns third_columns;
  static constexpr char third_merge_op1[] = "three";
  static constexpr char third_merge_op1_upper[] = "THREE";
  static constexpr char third_merge_op2[] = "four";
  static constexpr char third_merge_op2_upper[] = "FOUR";
};

const WideColumns DBWideMergeV3Test::third_columns{{"one", "ONE"},
                                                   {"two", "TWO"}};

TEST_F(DBWideMergeV3Test, MergeV3WideColumnOutput) {
  // A test merge operator that always returns a wide-column result. It adds any
  // base values and merge operands to a single wide-column entity, and converts
  // all column values to uppercase. In addition, it puts "none", "plain", or
  // "wide" into the value of the default column depending on the type of the
  // base value (if any).
  static constexpr char kNone[] = "none";
  static constexpr char kPlain[] = "plain";
  static constexpr char kWide[] = "wide";

  class WideColumnOutputMergeOperator : public MergeOperator {
   public:
    bool FullMergeV3(const MergeOperationInputV3& merge_in,
                     MergeOperationOutputV3* merge_out) const override {
      assert(merge_out);

      merge_out->new_value = MergeOperationOutputV3::NewColumns();
      auto& new_columns =
          std::get<MergeOperationOutputV3::NewColumns>(merge_out->new_value);

      auto upper = [](std::string str) {
        for (char& c : str) {
          c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }

        return str;
      };

      std::visit(overload{[&](const std::monostate&) {
                            new_columns.emplace_back(
                                kDefaultWideColumnName.ToString(), kNone);
                          },
                          [&](const Slice& value) {
                            new_columns.emplace_back(
                                kDefaultWideColumnName.ToString(), kPlain);

                            const std::string val = value.ToString();
                            new_columns.emplace_back(val, upper(val));
                          },
                          [&](const WideColumns& columns) {
                            new_columns.emplace_back(
                                kDefaultWideColumnName.ToString(), kWide);

                            for (const auto& column : columns) {
                              new_columns.emplace_back(
                                  column.name().ToString(),
                                  upper(column.value().ToString()));
                            }
                          }},
                 merge_in.existing_value);

      for (const auto& operand : merge_in.operand_list) {
        const std::string op = operand.ToString();
        new_columns.emplace_back(op, upper(op));
      }

      return true;
    }

    const char* Name() const override {
      return "WideColumnOutputMergeOperator";
    }
  };

  Options options = GetDefaultOptions();
  options.create_if_missing = true;
  options.merge_operator = std::make_shared<WideColumnOutputMergeOperator>();
  Reopen(options);

  // Expected results
  // Lexicographical order: [default] < hello < world
  const WideColumns first_expected{{kDefaultWideColumnName, kNone},
                                   {first_merge_op1, first_merge_op1_upper},
                                   {first_merge_op2, first_merge_op2_upper}};
  // Lexicographical order: [default] < bar < baz < foo
  const WideColumns second_expected{
      {kDefaultWideColumnName, kPlain},
      {second_merge_op1, second_merge_op1_upper},
      {second_merge_op2, second_merge_op2_upper},
      {second_base_value, second_base_value_upper}};
  // Lexicographical order: [default] < four < one < three < two
  const WideColumns third_expected{
      {kDefaultWideColumnName, kWide},
      {third_merge_op2, third_merge_op2_upper},
      {third_columns[0].name(), third_columns[0].value()},
      {third_merge_op1, third_merge_op1_upper},
      {third_columns[1].name(), third_columns[1].value()}};

  RunTest(first_expected, second_expected, third_expected);
}

TEST_F(DBWideMergeV3Test, MergeV3PlainOutput) {
  // A test merge operator that always returns a plain value as result, namely
  // the total number of operands serialized as a string. Base values are also
  // counted as operands; specifically, a plain base value is counted as one
  // operand, while a wide-column base value is counted as as many operands as
  // the number of columns.
  class PlainOutputMergeOperator : public MergeOperator {
   public:
    bool FullMergeV3(const MergeOperationInputV3& merge_in,
                     MergeOperationOutputV3* merge_out) const override {
      assert(merge_out);

      size_t count = 0;
      std::visit(
          overload{[&](const std::monostate&) {},
                   [&](const Slice&) { count = 1; },
                   [&](const WideColumns& columns) { count = columns.size(); }},
          merge_in.existing_value);

      count += merge_in.operand_list.size();

      merge_out->new_value = std::string();
      std::get<std::string>(merge_out->new_value) = std::to_string(count);

      return true;
    }

    const char* Name() const override { return "PlainOutputMergeOperator"; }
  };

  Options options = GetDefaultOptions();
  options.create_if_missing = true;
  options.merge_operator = std::make_shared<PlainOutputMergeOperator>();
  Reopen(options);

  const WideColumns first_expected{{kDefaultWideColumnName, "2"}};
  const WideColumns second_expected{{kDefaultWideColumnName, "3"}};
  const WideColumns third_expected{{kDefaultWideColumnName, "4"}};

  RunTest(first_expected, second_expected, third_expected);
}

TEST_F(DBWideBasicTest, CompactionFilter) {
  Options options = GetDefaultOptions();
  options.create_if_missing = true;

  // Wide-column entity with default column
  constexpr char first_key[] = "first";
  WideColumns first_columns{{kDefaultWideColumnName, "a"},
                            {"attr_name1", "foo"},
                            {"attr_name2", "bar"}};
  WideColumns first_columns_uppercase{{kDefaultWideColumnName, "A"},
                                      {"attr_name1", "FOO"},
                                      {"attr_name2", "BAR"}};

  // Wide-column entity without default column
  constexpr char second_key[] = "second";
  WideColumns second_columns{{"attr_one", "two"}, {"attr_three", "four"}};
  WideColumns second_columns_uppercase{{"attr_one", "TWO"},
                                       {"attr_three", "FOUR"}};

  // Plain old key-value
  constexpr char last_key[] = "last";
  constexpr char last_value[] = "baz";
  constexpr char last_value_uppercase[] = "BAZ";

  auto write = [&] {
    ASSERT_OK(db_->PutEntity(WriteOptions(), db_->DefaultColumnFamily(),
                             first_key, first_columns));
    ASSERT_OK(db_->PutEntity(WriteOptions(), db_->DefaultColumnFamily(),
                             second_key, second_columns));

    ASSERT_OK(Flush());

    ASSERT_OK(db_->Put(WriteOptions(), db_->DefaultColumnFamily(), last_key,
                       last_value));

    ASSERT_OK(Flush());
    ASSERT_OK(db_->CompactRange(CompactRangeOptions(), /* begin */ nullptr,
                                /* end */ nullptr));
  };

  // Test a compaction filter that keeps all entries
  {
    class KeepFilter : public CompactionFilter {
     public:
      Decision FilterV3(
          int /* level */, const Slice& /* key */, ValueType /* value_type */,
          const Slice* /* existing_value */,
          const WideColumns* /* existing_columns */,
          std::string* /* new_value */,
          std::vector<std::pair<std::string, std::string>>* /* new_columns */,
          std::string* /* skip_until */) const override {
        return Decision::kKeep;
      }

      const char* Name() const override { return "KeepFilter"; }
    };

    KeepFilter filter;
    options.compaction_filter = &filter;

    DestroyAndReopen(options);

    write();

    {
      PinnableWideColumns result;
      ASSERT_OK(db_->GetEntity(ReadOptions(), db_->DefaultColumnFamily(),
                               first_key, &result));
      ASSERT_EQ(result.columns(), first_columns);
    }

    {
      PinnableWideColumns result;
      ASSERT_OK(db_->GetEntity(ReadOptions(), db_->DefaultColumnFamily(),
                               second_key, &result));
      ASSERT_EQ(result.columns(), second_columns);
    }

    // Note: GetEntity should return an entity with a single default column,
    // since last_key is a plain key-value
    {
      PinnableWideColumns result;
      ASSERT_OK(db_->GetEntity(ReadOptions(), db_->DefaultColumnFamily(),
                               last_key, &result));

      WideColumns expected_columns{{kDefaultWideColumnName, last_value}};
      ASSERT_EQ(result.columns(), expected_columns);
    }
  }

  // Test a compaction filter that removes all entries
  {
    class RemoveFilter : public CompactionFilter {
     public:
      Decision FilterV3(
          int /* level */, const Slice& /* key */, ValueType /* value_type */,
          const Slice* /* existing_value */,
          const WideColumns* /* existing_columns */,
          std::string* /* new_value */,
          std::vector<std::pair<std::string, std::string>>* /* new_columns */,
          std::string* /* skip_until */) const override {
        return Decision::kRemove;
      }

      const char* Name() const override { return "RemoveFilter"; }
    };

    RemoveFilter filter;
    options.compaction_filter = &filter;

    DestroyAndReopen(options);

    write();

    {
      PinnableWideColumns result;
      ASSERT_TRUE(db_->GetEntity(ReadOptions(), db_->DefaultColumnFamily(),
                                 first_key, &result)
                      .IsNotFound());
    }

    {
      PinnableWideColumns result;
      ASSERT_TRUE(db_->GetEntity(ReadOptions(), db_->DefaultColumnFamily(),
                                 second_key, &result)
                      .IsNotFound());
    }

    {
      PinnableWideColumns result;
      ASSERT_TRUE(db_->GetEntity(ReadOptions(), db_->DefaultColumnFamily(),
                                 last_key, &result)
                      .IsNotFound());
    }
  }

  // Test a compaction filter that changes the values of entries to uppercase.
  // The new entry is always a plain key-value; if the existing entry is a
  // wide-column entity, only the value of its first column is kept.
  {
    class ChangeValueFilter : public CompactionFilter {
     public:
      Decision FilterV3(
          int /* level */, const Slice& /* key */, ValueType value_type,
          const Slice* existing_value, const WideColumns* existing_columns,
          std::string* new_value,
          std::vector<std::pair<std::string, std::string>>* /* new_columns */,
          std::string* /* skip_until */) const override {
        assert(new_value);

        auto upper = [](const std::string& str) {
          std::string result(str);

          for (char& c : result) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
          }

          return result;
        };

        if (value_type == ValueType::kWideColumnEntity) {
          assert(existing_columns);

          if (!existing_columns->empty()) {
            *new_value = upper(existing_columns->front().value().ToString());
          }
        } else {
          assert(existing_value);

          *new_value = upper(existing_value->ToString());
        }

        return Decision::kChangeValue;
      }

      const char* Name() const override { return "ChangeValueFilter"; }
    };

    ChangeValueFilter filter;
    options.compaction_filter = &filter;

    DestroyAndReopen(options);

    write();

    // Note: GetEntity should return entities with a single default column,
    // since all entries are now plain key-values
    {
      PinnableWideColumns result;
      ASSERT_OK(db_->GetEntity(ReadOptions(), db_->DefaultColumnFamily(),
                               first_key, &result));

      WideColumns expected_columns{
          {kDefaultWideColumnName, first_columns_uppercase[0].value()}};
      ASSERT_EQ(result.columns(), expected_columns);
    }

    {
      PinnableWideColumns result;
      ASSERT_OK(db_->GetEntity(ReadOptions(), db_->DefaultColumnFamily(),
                               second_key, &result));

      WideColumns expected_columns{
          {kDefaultWideColumnName, second_columns_uppercase[0].value()}};
      ASSERT_EQ(result.columns(), expected_columns);
    }

    {
      PinnableWideColumns result;
      ASSERT_OK(db_->GetEntity(ReadOptions(), db_->DefaultColumnFamily(),
                               last_key, &result));

      WideColumns expected_columns{
          {kDefaultWideColumnName, last_value_uppercase}};
      ASSERT_EQ(result.columns(), expected_columns);
    }
  }

  // Test a compaction filter that changes the column values of entries to
  // uppercase. The new entry is always a wide-column entity; if the existing
  // entry is a plain key-value, it is converted to a wide-column entity with a
  // single default column.
  {
    class ChangeEntityFilter : public CompactionFilter {
     public:
      Decision FilterV3(
          int /* level */, const Slice& /* key */, ValueType value_type,
          const Slice* existing_value, const WideColumns* existing_columns,
          std::string* /* new_value */,
          std::vector<std::pair<std::string, std::string>>* new_columns,
          std::string* /* skip_until */) const override {
        assert(new_columns);

        auto upper = [](const std::string& str) {
          std::string result(str);

          for (char& c : result) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
          }

          return result;
        };

        if (value_type == ValueType::kWideColumnEntity) {
          assert(existing_columns);

          for (const auto& column : *existing_columns) {
            new_columns->emplace_back(column.name().ToString(),
                                      upper(column.value().ToString()));
          }
        } else {
          assert(existing_value);

          new_columns->emplace_back(kDefaultWideColumnName.ToString(),
                                    upper(existing_value->ToString()));
        }

        return Decision::kChangeWideColumnEntity;
      }

      const char* Name() const override { return "ChangeEntityFilter"; }
    };

    ChangeEntityFilter filter;
    options.compaction_filter = &filter;

    DestroyAndReopen(options);

    write();

    {
      PinnableWideColumns result;
      ASSERT_OK(db_->GetEntity(ReadOptions(), db_->DefaultColumnFamily(),
                               first_key, &result));
      ASSERT_EQ(result.columns(), first_columns_uppercase);
    }

    {
      PinnableWideColumns result;
      ASSERT_OK(db_->GetEntity(ReadOptions(), db_->DefaultColumnFamily(),
                               second_key, &result));
      ASSERT_EQ(result.columns(), second_columns_uppercase);
    }

    {
      PinnableWideColumns result;
      ASSERT_OK(db_->GetEntity(ReadOptions(), db_->DefaultColumnFamily(),
                               last_key, &result));

      WideColumns expected_columns{
          {kDefaultWideColumnName, last_value_uppercase}};
      ASSERT_EQ(result.columns(), expected_columns);
    }
  }
}

TEST_F(DBWideBasicTest, PutEntityTimestampError) {
  // Note: timestamps are currently not supported

  Options options = GetDefaultOptions();
  options.comparator = test::BytewiseComparatorWithU64TsWrapper();

  ColumnFamilyHandle* handle = nullptr;
  ASSERT_OK(db_->CreateColumnFamily(options, "corinthian", &handle));
  std::unique_ptr<ColumnFamilyHandle> handle_guard(handle);

  // Use the DB::PutEntity API
  constexpr char first_key[] = "first";
  WideColumns first_columns{{"attr_name1", "foo"}, {"attr_name2", "bar"}};

  ASSERT_TRUE(db_->PutEntity(WriteOptions(), handle, first_key, first_columns)
                  .IsInvalidArgument());

  // Use WriteBatch
  constexpr char second_key[] = "second";
  WideColumns second_columns{{"doric", "column"}, {"ionic", "column"}};

  WriteBatch batch;
  ASSERT_TRUE(
      batch.PutEntity(handle, second_key, second_columns).IsInvalidArgument());
  ASSERT_OK(db_->Write(WriteOptions(), &batch));
}

TEST_F(DBWideBasicTest, PutEntitySerializationError) {
  // Make sure duplicate columns are caught

  Options options = GetDefaultOptions();

  // Use the DB::PutEntity API
  constexpr char first_key[] = "first";
  WideColumns first_columns{{"foo", "bar"}, {"foo", "baz"}};

  ASSERT_TRUE(db_->PutEntity(WriteOptions(), db_->DefaultColumnFamily(),
                             first_key, first_columns)
                  .IsCorruption());

  // Use WriteBatch
  constexpr char second_key[] = "second";
  WideColumns second_columns{{"column", "doric"}, {"column", "ionic"}};

  WriteBatch batch;
  ASSERT_TRUE(
      batch.PutEntity(db_->DefaultColumnFamily(), second_key, second_columns)
          .IsCorruption());
  ASSERT_OK(db_->Write(WriteOptions(), &batch));
}

}  // namespace ROCKSDB_NAMESPACE

int main(int argc, char** argv) {
  ROCKSDB_NAMESPACE::port::InstallStackTraceHandler();
  ::testing::InitGoogleTest(&argc, argv);
  RegisterCustomObjects(argc, argv);
  return RUN_ALL_TESTS();
}
