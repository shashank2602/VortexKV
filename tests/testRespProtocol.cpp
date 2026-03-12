#include <gtest/gtest.h>
#include "respParser.h"
#include "respWriter.h"
#include "linearBuffer.h"

#include <string>
#include <optional>

// ============================================================================
// RESPParser Tests
// ============================================================================

class RESPParserTest : public ::testing::Test {
protected:
    CommandRequest command;
};

// --- Basic Parsing ---

TEST_F(RESPParserTest, ParseSimpleCommand) {
    std::string data = "*1\r\n$4\r\nPING\r\n";
    auto [status, bytesRead] = RESPParser::parse(data, command);

    EXPECT_EQ(status, ParseStatus::Success);
    EXPECT_EQ(bytesRead, static_cast<int>(data.size()));
    EXPECT_EQ(command.type, "PING");
    EXPECT_TRUE(command.arguments.empty());
}

TEST_F(RESPParserTest, ParseCommandWithOneArgument) {
    std::string data = "*2\r\n$4\r\nECHO\r\n$5\r\nhello\r\n";
    auto [status, bytesRead] = RESPParser::parse(data, command);

    EXPECT_EQ(status, ParseStatus::Success);
    EXPECT_EQ(bytesRead, static_cast<int>(data.size()));
    EXPECT_EQ(command.type, "ECHO");
    ASSERT_EQ(command.arguments.size(), 1);
    EXPECT_EQ(command.arguments[0], "hello");
}

TEST_F(RESPParserTest, ParseSetCommand) {
    std::string data = "*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n";
    auto [status, bytesRead] = RESPParser::parse(data, command);

    EXPECT_EQ(status, ParseStatus::Success);
    EXPECT_EQ(command.type, "SET");
    ASSERT_EQ(command.arguments.size(), 2);
    EXPECT_EQ(command.arguments[0], "foo");
    EXPECT_EQ(command.arguments[1], "bar");
}

TEST_F(RESPParserTest, ParseGetCommand) {
    std::string data = "*2\r\n$3\r\nGET\r\n$3\r\nfoo\r\n";
    auto [status, bytesRead] = RESPParser::parse(data, command);

    EXPECT_EQ(status, ParseStatus::Success);
    EXPECT_EQ(command.type, "GET");
    ASSERT_EQ(command.arguments.size(), 1);
    EXPECT_EQ(command.arguments[0], "foo");
}

TEST_F(RESPParserTest, ParseSetWithTTL) {
    std::string data = "*5\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n$2\r\nEX\r\n$4\r\n1000\r\n";
    auto [status, bytesRead] = RESPParser::parse(data, command);

    EXPECT_EQ(status, ParseStatus::Success);
    EXPECT_EQ(command.type, "SET");
    ASSERT_EQ(command.arguments.size(), 4);
    EXPECT_EQ(command.arguments[0], "foo");
    EXPECT_EQ(command.arguments[1], "bar");
    EXPECT_EQ(command.arguments[2], "EX");
    EXPECT_EQ(command.arguments[3], "1000");
}

TEST_F(RESPParserTest, ParseDelMultipleKeys) {
    std::string data = "*4\r\n$3\r\nDEL\r\n$2\r\nk1\r\n$2\r\nk2\r\n$2\r\nk3\r\n";
    auto [status, bytesRead] = RESPParser::parse(data, command);

    EXPECT_EQ(status, ParseStatus::Success);
    EXPECT_EQ(command.type, "DEL");
    ASSERT_EQ(command.arguments.size(), 3);
    EXPECT_EQ(command.arguments[0], "k1");
    EXPECT_EQ(command.arguments[1], "k2");
    EXPECT_EQ(command.arguments[2], "k3");
}

// --- Incomplete Data ---

TEST_F(RESPParserTest, IncompleteEmpty) {
    auto [status, bytesRead] = RESPParser::parse("", command);
    EXPECT_EQ(status, ParseStatus::IncompleteData);
    EXPECT_EQ(bytesRead, 0);
}

TEST_F(RESPParserTest, IncompleteArrayHeader) {
    auto [status, bytesRead] = RESPParser::parse("*2\r\n$3\r\nSET\r\n", command);
    EXPECT_EQ(status, ParseStatus::IncompleteData);
    EXPECT_EQ(bytesRead, 0);
}

TEST_F(RESPParserTest, IncompleteBulkStringData) {
    auto [status, bytesRead] = RESPParser::parse("*1\r\n$4\r\nPI", command);
    EXPECT_EQ(status, ParseStatus::IncompleteData);
    EXPECT_EQ(bytesRead, 0);
}

TEST_F(RESPParserTest, IncompleteNoNewlineAfterArrayCount) {
    auto [status, bytesRead] = RESPParser::parse("*2", command);
    EXPECT_EQ(status, ParseStatus::IncompleteData);
    EXPECT_EQ(bytesRead, 0);
}

TEST_F(RESPParserTest, IncompleteBulkStringLengthOnly) {
    auto [status, bytesRead] = RESPParser::parse("*1\r\n$4\r\n", command);
    EXPECT_EQ(status, ParseStatus::IncompleteData);
    EXPECT_EQ(bytesRead, 0);
}

// --- Error Cases ---

TEST_F(RESPParserTest, ErrorMissingAsterisk) {
    auto [status, bytesRead] = RESPParser::parse("+PING\r\n", command);
    EXPECT_EQ(status, ParseStatus::Error);
}

TEST_F(RESPParserTest, ErrorMissingDollarSign) {
    auto [status, bytesRead] = RESPParser::parse("*1\r\n+PING\r\n", command);
    EXPECT_EQ(status, ParseStatus::Error);
}

TEST_F(RESPParserTest, ErrorNonNumericArrayLength) {
    auto [status, bytesRead] = RESPParser::parse("*abc\r\n", command);
    EXPECT_EQ(status, ParseStatus::Error);
}

TEST_F(RESPParserTest, ErrorZeroArrayLength) {
    auto [status, bytesRead] = RESPParser::parse("*0\r\n", command);
    EXPECT_EQ(status, ParseStatus::Error);
}

TEST_F(RESPParserTest, ErrorNegativeArrayLength) {
    auto [status, bytesRead] = RESPParser::parse("*-1\r\n", command);
    EXPECT_EQ(status, ParseStatus::Error);
}

TEST_F(RESPParserTest, ErrorNonNumericBulkLength) {
    auto [status, bytesRead] = RESPParser::parse("*1\r\n$xyz\r\n", command);
    EXPECT_EQ(status, ParseStatus::Error);
}

TEST_F(RESPParserTest, ErrorBulkStringLengthMismatch) {
    auto [status, bytesRead] = RESPParser::parse("*1\r\n$3\r\nPING\r\n", command);
    EXPECT_EQ(status, ParseStatus::Error);
}

// --- Multiple Commands in Buffer (Pipelining) ---

TEST_F(RESPParserTest, ParseFirstCommandFromPipeline) {
    std::string data = "*1\r\n$4\r\nPING\r\n*2\r\n$3\r\nGET\r\n$3\r\nfoo\r\n";
    auto [status, bytesRead] = RESPParser::parse(data, command);

    EXPECT_EQ(status, ParseStatus::Success);
    EXPECT_EQ(command.type, "PING");

    command.reset();
	std::string_view remainingData = data.substr(bytesRead);
    auto [status2, bytesRead2] = RESPParser::parse(remainingData, command);
    EXPECT_EQ(status2, ParseStatus::Success);
    EXPECT_EQ(command.type, "GET");
    ASSERT_EQ(command.arguments.size(), 1);
    EXPECT_EQ(command.arguments[0], "foo");
}

// --- Edge Cases ---

TEST_F(RESPParserTest, EmptyBulkStringArgument) {
    std::string data = "*2\r\n$3\r\nSET\r\n$0\r\n\r\n";
    auto [status, bytesRead] = RESPParser::parse(data, command);

    EXPECT_EQ(status, ParseStatus::Success);
    EXPECT_EQ(command.type, "SET");
    ASSERT_EQ(command.arguments.size(), 1);
    EXPECT_EQ(command.arguments[0], "");
}

TEST_F(RESPParserTest, LargeBulkString) {
    std::string largeVal(5000, 'x');
    std::string data = "*2\r\n$3\r\nSET\r\n$5000\r\n" + largeVal + "\r\n";
    auto [status, bytesRead] = RESPParser::parse(data, command);

    EXPECT_EQ(status, ParseStatus::Success);
    EXPECT_EQ(command.type, "SET");
    ASSERT_EQ(command.arguments.size(), 1);
    EXPECT_EQ(command.arguments[0], largeVal);
}

TEST_F(RESPParserTest, BinaryDataInBulkString) {
    std::string binaryVal("he\0llo", 6);
    std::string data = "*2\r\n$3\r\nSET\r\n$6\r\n" + binaryVal + "\r\n";
    auto [status, bytesRead] = RESPParser::parse(data, command);

    EXPECT_EQ(status, ParseStatus::Success);
    ASSERT_EQ(command.arguments.size(), 1);
    EXPECT_EQ(command.arguments[0].size(), 6);
}

TEST_F(RESPParserTest, CommandResetBetweenParses) {
    std::string data1 = "*3\r\n$3\r\nSET\r\n$1\r\na\r\n$1\r\nb\r\n";
    RESPParser::parse(data1, command);
    EXPECT_EQ(command.arguments.size(), 2);

    std::string data2 = "*1\r\n$4\r\nPING\r\n";
    RESPParser::parse(data2, command);
    EXPECT_EQ(command.type, "PING");
    EXPECT_TRUE(command.arguments.empty());
}

// ============================================================================
// RESPWriter Tests
// ============================================================================

class RESPWriterTest : public ::testing::Test {
protected:
    LinearBuffer buffer;

};

// --- Simple String ---

TEST_F(RESPWriterTest, WriteSimpleString) {
    RESPWriter::writeSimpleString("OK", buffer);
    EXPECT_EQ(buffer.getView(), "+OK\r\n");
}

TEST_F(RESPWriterTest, WriteSimpleStringPONG) {
    RESPWriter::writeSimpleString("PONG", buffer);
    EXPECT_EQ(buffer.getView(), "+PONG\r\n");
}

TEST_F(RESPWriterTest, WriteSimpleStringEmpty) {
    RESPWriter::writeSimpleString("", buffer);
    EXPECT_EQ(buffer.getView(), "+\r\n");
}

// --- Error ---

TEST_F(RESPWriterTest, WriteError) {
    RESPWriter::writeError("ERR unknown command", buffer);
    EXPECT_EQ(buffer.getView(), "-ERR unknown command\r\n");
}

TEST_F(RESPWriterTest, WriteErrorEmpty) {
    RESPWriter::writeError("", buffer);
    EXPECT_EQ(buffer.getView(), "-\r\n");
}

// --- Integer ---

TEST_F(RESPWriterTest, WriteInteger) {
    RESPWriter::writeInteger("42", buffer);
    EXPECT_EQ(buffer.getView(), ":42\r\n");
}

TEST_F(RESPWriterTest, WriteIntegerZero) {
    RESPWriter::writeInteger("0", buffer);
    EXPECT_EQ(buffer.getView(), ":0\r\n");
}

TEST_F(RESPWriterTest, WriteIntegerNegative) {
    RESPWriter::writeInteger("-1", buffer);
    EXPECT_EQ(buffer.getView(), ":-1\r\n");
}

// --- Bulk String ---

TEST_F(RESPWriterTest, WriteBulkString) {
    RESPWriter::writeBulkString("hello", buffer);
    EXPECT_EQ(buffer.getView(), "$5\r\nhello\r\n");
}

TEST_F(RESPWriterTest, WriteBulkStringEmpty) {
    RESPWriter::writeBulkString(std::string_view(""), buffer);
    EXPECT_EQ(buffer.getView(), "$0\r\n\r\n");
}

TEST_F(RESPWriterTest, WriteBulkStringNull) {
    RESPWriter::writeBulkString(std::nullopt, buffer);
    EXPECT_EQ(buffer.getView(), "$-1\r\n");
}

TEST_F(RESPWriterTest, WriteBulkStringLarge) {
    std::string largeStr(1000, 'a');
    RESPWriter::writeBulkString(largeStr, buffer);
    std::string expected = "$1000\r\n" + largeStr + "\r\n";
    EXPECT_EQ(buffer.getView(), expected);
}

// --- Array Header ---

TEST_F(RESPWriterTest, WriteArrayHeader) {
    RESPWriter::writeArrayHeader(3, buffer);
    EXPECT_EQ(buffer.getView(), "*3\r\n");
}

TEST_F(RESPWriterTest, WriteArrayHeaderZero) {
    RESPWriter::writeArrayHeader(0, buffer);
    EXPECT_EQ(buffer.getView(), "*0\r\n");
}

TEST_F(RESPWriterTest, WriteArrayHeaderLarge) {
    RESPWriter::writeArrayHeader(100000, buffer);
    EXPECT_EQ(buffer.getView(), "*100000\r\n");
}

// --- Combined Write Operations ---

TEST_F(RESPWriterTest, WriteMultipleResponses) {
    RESPWriter::writeSimpleString("OK", buffer);
    RESPWriter::writeBulkString("value", buffer);
    RESPWriter::writeInteger("1", buffer);

    EXPECT_EQ(buffer.getView(), "+OK\r\n$5\r\nvalue\r\n:1\r\n");
}

TEST_F(RESPWriterTest, WriteArrayOfBulkStrings) {
    RESPWriter::writeArrayHeader(2, buffer);
    RESPWriter::writeBulkString("foo", buffer);
    RESPWriter::writeBulkString("bar", buffer);

    EXPECT_EQ(buffer.getView(), "*2\r\n$3\r\nfoo\r\n$3\r\nbar\r\n");
}

// ============================================================================
// Round-trip: Writer -> Parser
// ============================================================================

TEST(RESPRoundTripTest, WriteThenParseArrayOfBulkStrings) {
    LinearBuffer buf;
    RESPWriter::writeArrayHeader(3, buf);
    RESPWriter::writeBulkString("SET", buf);
    RESPWriter::writeBulkString("mykey", buf);
    RESPWriter::writeBulkString("myvalue", buf);

    CommandRequest command;
    auto view = buf.getView();
    auto [status, bytesRead] = RESPParser::parse(view, command);

    EXPECT_EQ(status, ParseStatus::Success);
    EXPECT_EQ(bytesRead, static_cast<int>(view.size()));
    EXPECT_EQ(command.type, "SET");
    ASSERT_EQ(command.arguments.size(), 2);
    EXPECT_EQ(command.arguments[0], "mykey");
    EXPECT_EQ(command.arguments[1], "myvalue");
}

TEST(RESPRoundTripTest, WriteThenParseSingleCommand) {
    LinearBuffer buf;
    RESPWriter::writeArrayHeader(1, buf);
    RESPWriter::writeBulkString("PING", buf);

    CommandRequest command;
    auto view = buf.getView();
    auto [status, bytesRead] = RESPParser::parse(view, command);

    EXPECT_EQ(status, ParseStatus::Success);
    EXPECT_EQ(command.type, "PING");
    EXPECT_TRUE(command.arguments.empty());
}

TEST(RESPRoundTripTest, WriteThenParsePipelinedCommands) {
    LinearBuffer buf;

    // Command 1: SET foo bar
    RESPWriter::writeArrayHeader(3, buf);
    RESPWriter::writeBulkString("SET", buf);
    RESPWriter::writeBulkString("foo", buf);
    RESPWriter::writeBulkString("bar", buf);

    // Command 2: GET foo
    RESPWriter::writeArrayHeader(2, buf);
    RESPWriter::writeBulkString("GET", buf);
    RESPWriter::writeBulkString("foo", buf);

    auto view = buf.getView();
    CommandRequest cmd;

    auto [status1, bytes1] = RESPParser::parse(view, cmd);
    EXPECT_EQ(status1, ParseStatus::Success);
    EXPECT_EQ(cmd.type, "SET");

    auto [status2, bytes2] = RESPParser::parse(view.substr(bytes1), cmd);
    EXPECT_EQ(status2, ParseStatus::Success);
    EXPECT_EQ(cmd.type, "GET");
    ASSERT_EQ(cmd.arguments.size(), 1);
    EXPECT_EQ(cmd.arguments[0], "foo");
}