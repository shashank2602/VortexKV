#include <gtest/gtest.h>
#include "respParser.h"
#include "respWriter.h"
#include "commandDispatcher.h"
#include "commands.h"
#include "database.h"
#include "linearBuffer.h"

#include <string>
#include <optional>
#include <vector>

// Helper: build a RESP command into a string
static std::string buildRespCommand(const std::vector<std::string>& parts)
{
    std::string result = "*" + std::to_string(parts.size()) + "\r\n";
    for (const auto& p : parts)
    {
        result += "$" + std::to_string(p.size()) + "\r\n";
        result += p;
        result += "\r\n";
    }
    return result;
}

// Helper: parse a RESP response from the buffer and return type + data
struct RespResponse {
    char type;             // '+', '-', ':', '$'
    std::string data;
    bool isNull = false;
};

static RespResponse parseResponse(std::string_view view, int& bytesConsumed)
{
    RespResponse resp;
    resp.type = view[0];

    if (resp.type == '$')
    {
        // Bulk string
        size_t crlfPos = view.find("\r\n", 1);
        std::string lenStr(view.substr(1, crlfPos - 1));
        int len = std::stoi(lenStr);
        bytesConsumed = static_cast<int>(crlfPos) + 2;

        if (len == -1)
        {
            resp.isNull = true;
            return resp;
        }

        resp.data = std::string(view.substr(bytesConsumed, len));
        bytesConsumed += len + 2;
    }
    else
    {
        // Simple string, error, integer (+, -, :)
        size_t crlfPos = view.find("\r\n", 1);
        resp.data = std::string(view.substr(1, crlfPos - 1));
        bytesConsumed = static_cast<int>(crlfPos) + 2;
    }

    return resp;
}

class IntegrationTest : public ::testing::Test {
protected:
    Database db;
    CommandDispatcher dispatcher;
    LinearBuffer responseBuffer;

    void SetUp() override {
        registerCommands(dispatcher);
    }

    // Sends a RESP command through the full pipeline and returns parsed response
    RespResponse executeCommand(const std::vector<std::string>& parts)
    {
        std::string rawResp = buildRespCommand(parts);

        CommandRequest command;
        auto [status, bytesRead] = RESPParser::parse(rawResp, command);
        EXPECT_EQ(status, ParseStatus::Success);

        responseBuffer.reset();
        dispatcher.dispatch(command, db, responseBuffer);

        int consumed = 0;
        return parseResponse(responseBuffer.getView(), consumed);
    }
};

// ============================================================================
// PING
// ============================================================================

TEST_F(IntegrationTest, Ping) {
    auto resp = executeCommand({"PING"});
    EXPECT_EQ(resp.type, '+');
    EXPECT_EQ(resp.data, "PONG");
}

TEST_F(IntegrationTest, PingWithMessage) {
    auto resp = executeCommand({"PING", "hello"});
    EXPECT_EQ(resp.type, '$');
    EXPECT_EQ(resp.data, "hello");
}

// ============================================================================
// ECHO
// ============================================================================

TEST_F(IntegrationTest, Echo) {
    auto resp = executeCommand({"ECHO", "test message"});
    EXPECT_EQ(resp.type, '$');
    EXPECT_EQ(resp.data, "test message");
}

// ============================================================================
// SET / GET
// ============================================================================

TEST_F(IntegrationTest, SetAndGet) {
    auto setResp = executeCommand({"SET", "mykey", "myvalue"});
    EXPECT_EQ(setResp.type, '+');
    EXPECT_EQ(setResp.data, "OK");

    auto getResp = executeCommand({"GET", "mykey"});
    EXPECT_EQ(getResp.type, '$');
    EXPECT_EQ(getResp.data, "myvalue");
}

TEST_F(IntegrationTest, GetNonExistentKey) {
    auto resp = executeCommand({"GET", "nosuchkey"});
    EXPECT_EQ(resp.type, '$');
    EXPECT_TRUE(resp.isNull);
}

TEST_F(IntegrationTest, SetOverwrite) {
    executeCommand({"SET", "key", "val1"});
    executeCommand({"SET", "key", "val2"});

    auto resp = executeCommand({"GET", "key"});
    EXPECT_EQ(resp.data, "val2");
}

TEST_F(IntegrationTest, SetWithTTL) {
    auto resp = executeCommand({"SET", "key", "val", "PX", "10000"});
    EXPECT_EQ(resp.type, '+');
    EXPECT_EQ(resp.data, "OK");

    auto getResp = executeCommand({"GET", "key"});
    EXPECT_EQ(getResp.data, "val");
}

TEST_F(IntegrationTest, SetWrongArgCount) {
    auto resp = executeCommand({"SET", "key"});
    EXPECT_EQ(resp.type, '-');
}

TEST_F(IntegrationTest, SetInvalidTTLOption) {
    auto resp = executeCommand({"SET", "key", "val", "EX", "1000"});
    EXPECT_EQ(resp.type, '-');
}

// ============================================================================
// DEL
// ============================================================================

TEST_F(IntegrationTest, Del) {
    executeCommand({"SET", "k1", "v1"});
    executeCommand({"SET", "k2", "v2"});

    auto resp = executeCommand({"DEL", "k1", "k2", "k3"});
    EXPECT_EQ(resp.type, ':');
    EXPECT_EQ(resp.data, "2");

    auto getResp = executeCommand({"GET", "k1"});
    EXPECT_TRUE(getResp.isNull);
}

// ============================================================================
// EXISTS
// ============================================================================

TEST_F(IntegrationTest, Exists) {
    executeCommand({"SET", "k1", "v1"});

    auto resp = executeCommand({"EXISTS", "k1", "k2"});
    EXPECT_EQ(resp.type, ':');
    EXPECT_EQ(resp.data, "1");
}

// ============================================================================
// INCR / DECR
// ============================================================================

TEST_F(IntegrationTest, Incr) {
    executeCommand({"SET", "counter", "10"});
    auto resp = executeCommand({"INCR", "counter"});
    EXPECT_EQ(resp.type, ':');
    EXPECT_EQ(resp.data, "11");
}

TEST_F(IntegrationTest, IncrNonExistent) {
    auto resp = executeCommand({"INCR", "newctr"});
    EXPECT_EQ(resp.type, ':');
    EXPECT_EQ(resp.data, "1");
}

TEST_F(IntegrationTest, IncrNotANumber) {
    executeCommand({"SET", "key", "abc"});
    auto resp = executeCommand({"INCR", "key"});
    EXPECT_EQ(resp.type, '-');
}

TEST_F(IntegrationTest, Decr) {
    executeCommand({"SET", "counter", "10"});
    auto resp = executeCommand({"DECR", "counter"});
    EXPECT_EQ(resp.type, ':');
    EXPECT_EQ(resp.data, "9");
}

TEST_F(IntegrationTest, IncrBy) {
    executeCommand({"SET", "counter", "10"});
    auto resp = executeCommand({"INCRBY", "counter", "5"});
    EXPECT_EQ(resp.type, ':');
    EXPECT_EQ(resp.data, "15");
}

TEST_F(IntegrationTest, DecrBy) {
    executeCommand({"SET", "counter", "10"});
    auto resp = executeCommand({"DECRBY", "counter", "3"});
    EXPECT_EQ(resp.type, ':');
    EXPECT_EQ(resp.data, "7");
}

// ============================================================================
// EXPIRE / PERSIST / TTL
// ============================================================================

TEST_F(IntegrationTest, ExpireAndTTL) {
    executeCommand({"SET", "key", "val"});
    auto expResp = executeCommand({"EXPIRE", "key", "10000"});
    EXPECT_EQ(expResp.type, ':');
    EXPECT_EQ(expResp.data, "1");

    auto ttlResp = executeCommand({"TTL", "key"});
    EXPECT_EQ(ttlResp.type, ':');
    // TTL should be a positive number
    EXPECT_NE(ttlResp.data, "-1");
    EXPECT_NE(ttlResp.data, "-2");
}

TEST_F(IntegrationTest, PersistRemovesTTL) {
    executeCommand({"SET", "key", "val", "PX", "10000"});
    auto resp = executeCommand({"PERSIST", "key"});
    EXPECT_EQ(resp.type, ':');
    EXPECT_EQ(resp.data, "1");

    auto ttlResp = executeCommand({"TTL", "key"});
    EXPECT_EQ(ttlResp.data, "-1");
}

TEST_F(IntegrationTest, TTLNonExistent) {
    auto resp = executeCommand({"TTL", "nosuchkey"});
    EXPECT_EQ(resp.type, ':');
    EXPECT_EQ(resp.data, "-2");
}

// ============================================================================
// Unknown Command
// ============================================================================

TEST_F(IntegrationTest, UnknownCommand) {
    auto resp = executeCommand({"FOOBAR"});
    EXPECT_EQ(resp.type, '-');
}

// ============================================================================
// Case Insensitivity
// ============================================================================

TEST_F(IntegrationTest, CaseInsensitiveCommands) {
    auto resp1 = executeCommand({"ping"});
    EXPECT_EQ(resp1.type, '+');
    EXPECT_EQ(resp1.data, "PONG");

    executeCommand({"set", "key", "val"});
    auto resp2 = executeCommand({"get", "key"});
    EXPECT_EQ(resp2.data, "val");
}

// ============================================================================
// Pipelining Simulation (multiple commands processed sequentially)
// ============================================================================

TEST_F(IntegrationTest, PipelinedCommands) {
    std::string pipeline;
    pipeline += buildRespCommand({"SET", "k1", "v1"});
    pipeline += buildRespCommand({"SET", "k2", "v2"});
    pipeline += buildRespCommand({"GET", "k1"});
    pipeline += buildRespCommand({"GET", "k2"});
    pipeline += buildRespCommand({"DEL", "k1"});
    pipeline += buildRespCommand({"GET", "k1"});

    std::string_view remaining = pipeline;
    std::vector<RespResponse> responses;

    while (!remaining.empty())
    {
        CommandRequest cmd;
        auto [status, bytesRead] = RESPParser::parse(remaining, cmd);
        if (status != ParseStatus::Success) break;

        responseBuffer.reset();
        dispatcher.dispatch(cmd, db, responseBuffer);

        int consumed = 0;
        responses.push_back(parseResponse(responseBuffer.getView(), consumed));
        remaining = remaining.substr(bytesRead);
    }

    ASSERT_EQ(responses.size(), 6);
    EXPECT_EQ(responses[0].data, "OK");       // SET k1 v1
    EXPECT_EQ(responses[1].data, "OK");       // SET k2 v2
    EXPECT_EQ(responses[2].data, "v1");       // GET k1
    EXPECT_EQ(responses[3].data, "v2");       // GET k2
    EXPECT_EQ(responses[4].data, "1");        // DEL k1
    EXPECT_TRUE(responses[5].isNull);         // GET k1 (deleted)
}

// ============================================================================
// Large Value
// ============================================================================

TEST_F(IntegrationTest, LargeValue) {
    std::string largeVal(10000, 'z');
    auto setResp = executeCommand({"SET", "bigkey", largeVal});
    EXPECT_EQ(setResp.data, "OK");

    auto getResp = executeCommand({"GET", "bigkey"});
    EXPECT_EQ(getResp.data, largeVal);
}