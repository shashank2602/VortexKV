#include <gtest/gtest.h>
#include "database.h"

#include <string>
#include <vector>
#include <thread>
#include <chrono>

class DatabaseTest : public ::testing::Test {
protected:
    Database db;

    void SetUp() override {}
    void TearDown() override {}
};

// ============================================================================
// SET and GET Tests
// ============================================================================
TEST_F(DatabaseTest, SetAndGetString) {
    auto setResult = db.SET("key1", "value1");
    EXPECT_EQ(setResult.error, DatabaseError::SUCCESS);

    auto getResult = db.GET("key1");
    EXPECT_EQ(getResult.error, DatabaseError::SUCCESS);
    EXPECT_EQ(getResult.str(), "value1");
}

TEST_F(DatabaseTest, SetAndGetInteger) {
    auto setResult = db.SET("counter", "42");
    EXPECT_EQ(setResult.error, DatabaseError::SUCCESS);

    auto getResult = db.GET("counter");
    EXPECT_EQ(getResult.error, DatabaseError::SUCCESS);
    EXPECT_EQ(getResult.str(), "42");
}

TEST_F(DatabaseTest, SetAndGetNegativeInteger) {
    auto setResult = db.SET("negative", "-100");
    EXPECT_EQ(setResult.error, DatabaseError::SUCCESS);

    auto getResult = db.GET("negative");
    EXPECT_EQ(getResult.error, DatabaseError::SUCCESS);
    EXPECT_EQ(getResult.str(), "-100");
}

TEST_F(DatabaseTest, GetNonExistentKey) {
    auto result = db.GET("nonexistent");
    EXPECT_EQ(result.error, DatabaseError::KEY_NOT_FOUND);
}

TEST_F(DatabaseTest, OverwriteExistingKey) {
    db.SET("key", "value1");
    db.SET("key", "value2");

    auto result = db.GET("key");
    EXPECT_EQ(result.error, DatabaseError::SUCCESS);
    EXPECT_EQ(result.str(), "value2");
}

TEST_F(DatabaseTest, SetMultipleKeys) {
    db.SET("key1", "value1");
    db.SET("key2", "value2");
    db.SET("key3", "value3");

    EXPECT_EQ(db.GET("key1").str(), "value1");
    EXPECT_EQ(db.GET("key2").str(), "value2");
    EXPECT_EQ(db.GET("key3").str(), "value3");
}

// ============================================================================
// DEL Tests
// ============================================================================
TEST_F(DatabaseTest, DeleteExistingKey) {
    db.SET("key", "value");
    auto result = db.DEL(std::vector<std::string_view>{"key"});

    EXPECT_EQ(result.error, DatabaseError::SUCCESS);
    EXPECT_EQ(result.str(), "1");
    EXPECT_EQ(db.GET("key").error, DatabaseError::KEY_NOT_FOUND);
}

TEST_F(DatabaseTest, DeleteNonExistentKey) {
    auto result = db.DEL(std::vector<std::string_view>{"nonexistent"});
    EXPECT_EQ(result.error, DatabaseError::SUCCESS);
    EXPECT_EQ(result.str(), "0");
}

TEST_F(DatabaseTest, DeleteMultipleKeys) {
    db.SET("key1", "value1");
    db.SET("key2", "value2");
    db.SET("key3", "value3");

    auto result = db.DEL(std::vector<std::string_view>{"key1", "key2", "nonexistent"});
    EXPECT_EQ(result.error, DatabaseError::SUCCESS);
    EXPECT_EQ(result.str(), "2");

    EXPECT_EQ(db.GET("key1").error, DatabaseError::KEY_NOT_FOUND);
    EXPECT_EQ(db.GET("key2").error, DatabaseError::KEY_NOT_FOUND);
    EXPECT_EQ(db.GET("key3").error, DatabaseError::SUCCESS);
}

// ============================================================================
// EXISTS Tests
// ============================================================================
TEST_F(DatabaseTest, ExistsForExistingKey) {
    db.SET("key", "value");
    auto result = db.EXISTS(std::vector<std::string_view>{"key"});

    EXPECT_EQ(result.error, DatabaseError::SUCCESS);
    EXPECT_EQ(result.str(), "1");
}

TEST_F(DatabaseTest, ExistsForNonExistentKey) {
    auto result = db.EXISTS(std::vector<std::string_view>{"nonexistent"});
    EXPECT_EQ(result.error, DatabaseError::SUCCESS);
    EXPECT_EQ(result.str(), "0");
}

TEST_F(DatabaseTest, ExistsForMultipleKeys) {
    db.SET("key1", "value1");
    db.SET("key2", "value2");

    auto result = db.EXISTS(std::vector<std::string_view>{"key1", "key2", "nonexistent"});
    EXPECT_EQ(result.error, DatabaseError::SUCCESS);
    EXPECT_EQ(result.str(), "2");
}

// ============================================================================
// INCR/DECR Tests
// ============================================================================
TEST_F(DatabaseTest, IncrExistingInteger) {
    db.SET("counter", "10");
    auto result = db.INCR("counter");

    EXPECT_EQ(result.error, DatabaseError::SUCCESS);
    EXPECT_EQ(result.str(), "11");
}

TEST_F(DatabaseTest, IncrNonExistentKey) {
    auto result = db.INCR("newcounter");

    EXPECT_EQ(result.error, DatabaseError::SUCCESS);
    EXPECT_EQ(result.str(), "1");
}

TEST_F(DatabaseTest, DecrExistingInteger) {
    db.SET("counter", "10");
    auto result = db.DECR("counter");

    EXPECT_EQ(result.error, DatabaseError::SUCCESS);
    EXPECT_EQ(result.str(), "9");
}

TEST_F(DatabaseTest, DecrNonExistentKey) {
    auto result = db.DECR("newcounter");

    EXPECT_EQ(result.error, DatabaseError::SUCCESS);
    EXPECT_EQ(result.str(), "-1");
}

TEST_F(DatabaseTest, IncrNonIntegerValue) {
    db.SET("key", "notanumber");
    auto result = db.INCR("key");

    EXPECT_EQ(result.error, DatabaseError::NOT_AN_INTEGER_OR_OUT_OF_RANGE);
}

// ============================================================================
// INCRBY/DECRBY Tests
// ============================================================================
TEST_F(DatabaseTest, IncrByPositive) {
    db.SET("counter", "10");
    auto result = db.INCRBY("counter", "5");

    EXPECT_EQ(result.error, DatabaseError::SUCCESS);
    EXPECT_EQ(result.str(), "15");
}

TEST_F(DatabaseTest, IncrByNegative) {
    db.SET("counter", "10");
    auto result = db.INCRBY("counter", "-3");

    EXPECT_EQ(result.error, DatabaseError::SUCCESS);
    EXPECT_EQ(result.str(), "7");
}

TEST_F(DatabaseTest, DecrByPositive) {
    db.SET("counter", "10");
    auto result = db.DECRBY("counter", "5");

    EXPECT_EQ(result.error, DatabaseError::SUCCESS);
    EXPECT_EQ(result.str(), "5");
}

TEST_F(DatabaseTest, IncrByInvalidDelta) {
    db.SET("counter", "10");
    auto result = db.INCRBY("counter", "notanumber");

    EXPECT_EQ(result.error, DatabaseError::NOT_AN_INTEGER_OR_OUT_OF_RANGE);
}

// ============================================================================
// TTL and Expiration Tests
// ============================================================================
TEST_F(DatabaseTest, SetWithTTL) {
    auto result = db.SET("key", "value", "1000");
    EXPECT_EQ(result.error, DatabaseError::SUCCESS);

    auto ttlResult = db.TTL("key");
    EXPECT_EQ(ttlResult.error, DatabaseError::SUCCESS);
}

TEST_F(DatabaseTest, SetWithInvalidTTL) {
    auto result = db.SET("key", "value", "notanumber");
    EXPECT_EQ(result.error, DatabaseError::NOT_AN_INTEGER_OR_OUT_OF_RANGE);
}

TEST_F(DatabaseTest, SetWithZeroTTL) {
    auto result = db.SET("key", "value", "0");
    EXPECT_EQ(result.error, DatabaseError::NOT_AN_INTEGER_OR_OUT_OF_RANGE);
}

TEST_F(DatabaseTest, SetWithNegativeTTL) {
    auto result = db.SET("key", "value", "-1");
    EXPECT_EQ(result.error, DatabaseError::NOT_AN_INTEGER_OR_OUT_OF_RANGE);
}

TEST_F(DatabaseTest, ExpireExistingKey) {
    db.SET("key", "value");
    auto result = db.EXPIRE("key", "1000");

    EXPECT_EQ(result.error, DatabaseError::SUCCESS);
    EXPECT_EQ(result.str(), "1");
}

TEST_F(DatabaseTest, ExpireNonExistentKey) {
    auto result = db.EXPIRE("nonexistent", "1000");
    EXPECT_EQ(result.error, DatabaseError::KEY_NOT_FOUND);
}

TEST_F(DatabaseTest, PersistExistingExpiration) {
    db.SET("key", "value", "10000");
    auto result = db.PERSIST("key");

    EXPECT_EQ(result.error, DatabaseError::SUCCESS);
    EXPECT_EQ(result.str(), "1");

    auto ttlResult = db.TTL("key");
    EXPECT_EQ(ttlResult.error, DatabaseError::NO_EXPIRATION_SET);
}

TEST_F(DatabaseTest, PersistNoExpiration) {
    db.SET("key", "value");
    auto result = db.PERSIST("key");

    EXPECT_EQ(result.error, DatabaseError::NO_EXPIRATION_SET);
}

TEST_F(DatabaseTest, TTLExistingExpiration) {
    db.SET("key", "value", "10000");
    auto result = db.TTL("key");

    EXPECT_EQ(result.error, DatabaseError::SUCCESS);
}

TEST_F(DatabaseTest, TTLNoExpiration) {
    db.SET("key", "value");
    auto result = db.TTL("key");

    EXPECT_EQ(result.error, DatabaseError::NO_EXPIRATION_SET);
    EXPECT_EQ(result.str(), "-1");
}

TEST_F(DatabaseTest, TTLNonExistentKey) {
    auto result = db.TTL("nonexistent");

    EXPECT_EQ(result.error, DatabaseError::KEY_NOT_FOUND);
    EXPECT_EQ(result.str(), "-2");
}

// ============================================================================
// Edge Cases
// ============================================================================
TEST_F(DatabaseTest, EmptyKeyValue) {
    auto setResult = db.SET("", "");
    EXPECT_EQ(setResult.error, DatabaseError::SUCCESS);

    auto getResult = db.GET("");
    EXPECT_EQ(getResult.error, DatabaseError::SUCCESS);
}

TEST_F(DatabaseTest, LongKeyValue) {
    std::string longKey(1000, 'k');
    std::string longValue(10000, 'v');

    auto setResult = db.SET(longKey, longValue);
    EXPECT_EQ(setResult.error, DatabaseError::SUCCESS);

    auto getResult = db.GET(longKey);
    EXPECT_EQ(getResult.error, DatabaseError::SUCCESS);
    EXPECT_EQ(getResult.str(), longValue);
}

TEST_F(DatabaseTest, SpecialCharactersInKeyValue) {
    std::string key = "key\0with\nnull\tand\rtabs";
    std::string value = "value\0with\nnull\tand\rtabs";

    auto setResult = db.SET(key, value);
    EXPECT_EQ(setResult.error, DatabaseError::SUCCESS);

    auto getResult = db.GET(key);
    EXPECT_EQ(getResult.error, DatabaseError::SUCCESS);
    EXPECT_EQ(getResult.str(), value);
}

TEST_F(DatabaseTest, OverwriteWithDifferentType) {
    db.SET("key", "42");
    db.SET("key", "notanumber");

    auto getResult = db.GET("key");
    EXPECT_EQ(getResult.error, DatabaseError::SUCCESS);
    EXPECT_EQ(getResult.str(), "notanumber");
}

TEST_F(DatabaseTest, SetRemovesTTLIfNotProvided) {
    db.SET("key", "value", "10000");
    db.SET("key", "newvalue");

    auto ttlResult = db.TTL("key");
    EXPECT_EQ(ttlResult.error, DatabaseError::NO_EXPIRATION_SET);
}
