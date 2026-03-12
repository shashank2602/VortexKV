#include <gtest/gtest.h>
#include "flatMap.h"
#include "compactString.h"

#include <string>
#include <vector>

class FlatMapTest : public ::testing::Test {
protected:
    using TestMap = FlatMap<CompactString, int>;
    TestMap map;

    void SetUp() override {}
    void TearDown() override {}
};

// Basic Insert and Find
TEST_F(FlatMapTest, InsertAndFind) {
    map.insert(CompactString("key1", 4), 100);
    auto* value = map.find("key1");
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(*value, 100);
}

TEST_F(FlatMapTest, FindNonExistent) {
    auto* value = map.find("nonexistent");
    EXPECT_EQ(value, nullptr);
}

TEST_F(FlatMapTest, InsertMultiple) {
    map.insert(CompactString("key1", 4), 1);
    map.insert(CompactString("key2", 4), 2);
    map.insert(CompactString("key3", 4), 3);

    EXPECT_EQ(*map.find("key1"), 1);
    EXPECT_EQ(*map.find("key2"), 2);
    EXPECT_EQ(*map.find("key3"), 3);
}

TEST_F(FlatMapTest, UpdateExistingKey) {
    map.insert(CompactString("key", 3), 100);
    bool inserted = map.insert(CompactString("key", 3), 200);
    
    EXPECT_FALSE(inserted); // Should return false for update
    EXPECT_EQ(*map.find("key"), 200);
}

// Remove Tests
TEST_F(FlatMapTest, RemoveExisting) {
    map.insert(CompactString("key", 3), 100);
    bool removed = map.remove("key");
    
    EXPECT_TRUE(removed);
    EXPECT_EQ(map.find("key"), nullptr);
}

TEST_F(FlatMapTest, RemoveNonExistent) {
    bool removed = map.remove("nonexistent");
    EXPECT_FALSE(removed);
}

TEST_F(FlatMapTest, RemoveAndReinsert) {
    map.insert(CompactString("key", 3), 100);
    map.remove("key");
    map.insert(CompactString("key", 3), 200);
    
    EXPECT_EQ(*map.find("key"), 200);
}

// Resize Tests
TEST_F(FlatMapTest, GrowthTriggersResize) {
    map.setAllowGrowth(true);
    
    // Insert enough keys to trigger resize (> 80% of initial 256)
    for (int i = 0; i < 300; ++i) {
        std::string key = "key" + std::to_string(i);
        map.insert(CompactString(key), i);
    }

    // Verify all keys are still accessible
    for (int i = 0; i < 300; ++i) {
        std::string key = "key" + std::to_string(i);
        auto* value = map.find(key);
        ASSERT_NE(value, nullptr) << "Key not found: " << key;
        EXPECT_EQ(*value, i);
    }
}

TEST_F(FlatMapTest, NoGrowthWhenDisabled) {
    map.setAllowGrowth(false);
    uint64_t initialMemory = map.getTableMemoryUsage();
    
    // Insert some keys
    for (int i = 0; i < 100; ++i) {
        std::string key = "key" + std::to_string(i);
        map.insert(CompactString(key), i);
    }
    
    EXPECT_EQ(map.getTableMemoryUsage(), initialMemory);
}

// Long Key Tests
TEST_F(FlatMapTest, LongKeys) {
    std::string longKey(100, 'a');
    map.insert(CompactString(longKey), 42);
    
    auto* value = map.find(longKey);
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(*value, 42);
}

// Collision Handling
TEST_F(FlatMapTest, HandleCollisions) {
    // Insert many keys to create collisions
    for (int i = 0; i < 200; ++i) {
        std::string key = "collision_test_" + std::to_string(i);
        map.insert(CompactString(key), i);
    }

    // Verify all keys are found correctly
    for (int i = 0; i < 200; ++i) {
        std::string key = "collision_test_" + std::to_string(i);
        auto* value = map.find(key);
        ASSERT_NE(value, nullptr);
        EXPECT_EQ(*value, i);
    }
}

// Random Entry
TEST_F(FlatMapTest, GetRandomEntryEmpty) {
    auto [key, value] = map.getRandomEntry(map.getInternalTableSize());
    EXPECT_TRUE(key.empty());
    EXPECT_EQ(value, nullptr);
}

TEST_F(FlatMapTest, GetRandomEntryNonEmpty) {///////////////////////////////
    map.insert(CompactString("key1", 4), 1);
    map.insert(CompactString("key2", 4), 2);
    auto [key, value] = map.getRandomEntry(map.getInternalTableSize());
    EXPECT_FALSE(key.empty());
    ASSERT_NE(value, nullptr);
}

// Eviction Candidate
TEST_F(FlatMapTest, GetEvictionCandidateEmpty) {
    auto ret = map.evictOneEntry(false, map.getInternalTableSize());
    EXPECT_FALSE(ret);
}

TEST_F(FlatMapTest, GetEvictionCandidateNonEmpty) {
    map.insert(CompactString("key1", 4), 1);
    
    auto ret = map.evictOneEntry(true, map.getInternalTableSize());
    EXPECT_TRUE(ret);
}

// Visited Flag
TEST_F(FlatMapTest, MarkVisitedAffectsEviction) {
    // Insert and mark as visited
    map.insert(CompactString("key1", 4), 1);
    map.find("key1", std::nullopt, true);

    // First eviction check should reset visited flag
    auto ret1 = map.evictOneEntry(false, map.getInternalTableSize());
    
    // After reset, should be evictable
    auto ret2 = map.evictOneEntry(true, map.getInternalTableSize());
    EXPECT_FALSE(ret1);
    EXPECT_TRUE(ret2);
}

// Edge Cases
TEST_F(FlatMapTest, EmptyKey) {
    map.insert(CompactString("", 0), 123);
    auto* value = map.find("");
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(*value, 123);
}

TEST_F(FlatMapTest, BinaryKeyWithNullBytes) {
    const char binaryKey[] = "key\0with\0nulls";
    std::string_view keyView(binaryKey, 14);
    
    map.insert(CompactString(binaryKey, 14), 999);
    auto* value = map.find(keyView);
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(*value, 999);
}

// Memory Usage
TEST_F(FlatMapTest, MemoryUsageIncreases) {
    uint64_t initialMemory = map.getTableMemoryUsage();
    EXPECT_GT(initialMemory, 0);
    
    map.setAllowGrowth(true);
    for (int i = 0; i < 300; ++i) {
        std::string key = "memtest" + std::to_string(i);
        map.insert(CompactString(key), i);
    }
    
    EXPECT_GT(map.getTableMemoryUsage(), initialMemory);
}