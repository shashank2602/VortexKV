#include <gtest/gtest.h>
#include "compactString.h"

// Basic Tests - No fixture needed
TEST(CompactStringTest, DefaultConstructor) {
    CompactString cs;
    EXPECT_EQ(cs.length(), 0);
    EXPECT_EQ(cs.capacity(), SSO_CAPACITY);
}

TEST(CompactStringTest, SSOConstruction) {
    const char* shortStr = "Hello"; 
    CompactString cs(shortStr, 5);
    EXPECT_EQ(cs.length(), 5);
    EXPECT_EQ(std::string_view(cs.data(), cs.length()), "Hello");
}

// SSO vs Heap pointer verification
TEST(CompactStringTest, SSODataPointerIsInternal) {
    CompactString cs("Hello", 5);
    const char* dataPtr = cs.data();
    const char* objectStart = reinterpret_cast<const char*>(&cs);
    const char* objectEnd = objectStart + sizeof(CompactString);
    
    // SSO: data pointer should be within the object's memory
    EXPECT_GE(dataPtr, objectStart);
    EXPECT_LT(dataPtr, objectEnd);
    EXPECT_EQ(cs.heapSize(), 0);
}

TEST(CompactStringTest, HeapDataPointerIsExternal) {
    std::string longStr(100, 'a');
    CompactString cs(longStr.data(), static_cast<uint32_t>(longStr.length()));
    const char* dataPtr = cs.data();
    const char* objectStart = reinterpret_cast<const char*>(&cs);
    const char* objectEnd = objectStart + sizeof(CompactString);
    
    // Heap: data pointer should be outside the object's memory
    EXPECT_TRUE(dataPtr < objectStart || dataPtr >= objectEnd);
    EXPECT_GT(cs.heapSize(), 0);
}

TEST(CompactStringTest, SSOMaxLengthIsInternal) {
    std::string maxSSO(SSO_CAPACITY, 'x');
    CompactString cs(maxSSO.data(), SSO_CAPACITY);
    const char* dataPtr = cs.data();
    const char* objectStart = reinterpret_cast<const char*>(&cs);
    const char* objectEnd = objectStart + sizeof(CompactString);
    
    EXPECT_GE(dataPtr, objectStart);
    EXPECT_LT(dataPtr, objectEnd);
    EXPECT_EQ(cs.heapSize(), 0);
}

TEST(CompactStringTest, JustOverSSOIsHeap) {
    std::string str(SSO_CAPACITY + 1, 'x');
    CompactString cs(str.data(), SSO_CAPACITY + 1);
    const char* dataPtr = cs.data();
    const char* objectStart = reinterpret_cast<const char*>(&cs);
    const char* objectEnd = objectStart + sizeof(CompactString);
    
    EXPECT_TRUE(dataPtr < objectStart || dataPtr >= objectEnd);
    EXPECT_GT(cs.heapSize(), 0);
}

// Move semantics
TEST(CompactStringTest, MoveConstructorSSO) {
    CompactString cs1("Hello", 5);
    CompactString cs2(std::move(cs1));
    EXPECT_EQ(cs2.length(), 5);
    EXPECT_EQ(std::string_view(cs2.data(), cs2.length()), "Hello");
    EXPECT_EQ(cs1.length(), 0);
}

TEST(CompactStringTest, MoveConstructorHeap) {
    std::string longStr(100, 'b');
    CompactString cs1(longStr.data(), static_cast<uint32_t>(longStr.length()));
    CompactString cs2(std::move(cs1));
    EXPECT_EQ(cs2.length(), 100);
    EXPECT_EQ(std::string_view(cs2.data(), cs2.length()), longStr);
    EXPECT_EQ(cs1.length(), 0);
}

TEST(CompactStringTest, MoveAssignmentSSO) {
    CompactString cs1("Hello", 5);
    CompactString cs2;
    cs2 = std::move(cs1);
    EXPECT_EQ(cs2.length(), 5);
    EXPECT_EQ(std::string_view(cs2.data(), cs2.length()), "Hello");
}

TEST(CompactStringTest, MoveAssignmentHeap) {
    std::string longStr(100, 'c');
    CompactString cs1(longStr.data(), static_cast<uint32_t>(longStr.length()));
    CompactString cs2;
    cs2 = std::move(cs1);
    EXPECT_EQ(cs2.length(), 100);
    EXPECT_EQ(std::string_view(cs2.data(), cs2.length()), longStr);
}

// Equality operators
TEST(CompactStringTest, EqualityWithCompactString) {
    CompactString cs1("test", 4);
    CompactString cs2("test", 4);
    CompactString cs3("diff", 4);
    EXPECT_TRUE(cs1 == cs2);
    EXPECT_FALSE(cs1 == cs3);
}

TEST(CompactStringTest, EqualityWithStringView) {
    CompactString cs("test", 4);
    EXPECT_TRUE(cs == std::string_view("test"));
    EXPECT_FALSE(cs == std::string_view("other"));
}

// Swap
TEST(CompactStringTest, SwapSSOStrings) {
    CompactString cs1("hello", 5);
    CompactString cs2("world", 5);
    cs1.swap(cs2);
    EXPECT_EQ(std::string_view(cs1.data(), cs1.length()), "world");
    EXPECT_EQ(std::string_view(cs2.data(), cs2.length()), "hello");
}

TEST(CompactStringTest, SwapHeapStrings) {
    std::string str1(50, 'a');
    std::string str2(60, 'b');
    CompactString cs1(str1);
    CompactString cs2(str2);
    cs1.swap(cs2);
    EXPECT_EQ(std::string_view(cs1.data(), cs1.length()), str2);
    EXPECT_EQ(std::string_view(cs2.data(), cs2.length()), str1);
}

// Binary safety
TEST(CompactStringTest, BinarySafetyWithNullBytes) {
    const char binaryData[] = "hello\0world";
    CompactString cs(binaryData, 11);
    EXPECT_EQ(cs.length(), 11);
    EXPECT_EQ(std::memcmp(cs.data(), binaryData, 11), 0);
}

// Edge cases
TEST(CompactStringTest, EmptyString) {
    CompactString cs("", 0);
    EXPECT_EQ(cs.length(), 0);
}