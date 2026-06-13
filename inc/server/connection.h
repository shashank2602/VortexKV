#pragma once

#include "asio.hpp"
#include "utility.h"
#include "respParser.h"
#include "commands.h"
#include "database.h"
#include "libdivide.h"

#include <vector>
#include <memory>
#include <chrono>

using asio::ip::tcp;

// Forward declaration to avoid circular dependency between Connection and Shard
class Shard;


class Connection: public std::enable_shared_from_this<Connection>{

public:

	Connection(tcp::socket socket, uint64_t routingHashSeed, std::vector<std::unique_ptr<Shard>>& shardPool, int shardId);

	~Connection();

	void Start();

private:

	void DoRead();

	void TryWrite();

	void RouteRequest(int requestIndex, CommandRequest& request);

	void FlushPipelinedResponses();

	void ArmIdleTimer();

	void CloseConnection();

	tcp::socket m_socket;
	asio::steady_timer m_idleTimer;
	std::chrono::steady_clock::time_point m_lastActivity;
	std::chrono::seconds m_idleTimeout{0};	// 0 = disabled
	LinearBuffer m_requestBuffer;
	LinearBuffer m_primaryResponseBuffer, m_secondaryResponseBuffer;
	LinearBuffer *m_pPrimaryResponseBuffer = nullptr, *m_pSecondaryResponseBuffer = nullptr;
	bool m_writingInProgress;
	bool m_readPaused = false;	// reading is paused while the client drains a large pending write

	uint64_t m_routingHashSeed = 0;
	std::vector<std::unique_ptr<Shard>>& m_shardPool;
	int m_shardId = -1;
	Shard& m_ownerShard;
	CommandDispatcher& m_dispatcher;
	Database& m_database;

	int m_pipelinedRequestCount = 0;
	int m_pipelinedResponseCount = 0;
	uint64_t m_bytesConsumedInPipeline = 0;
	bool m_invalidProtocolError = false;
	bool m_pipelineReadingComplete = false;
	std::vector<CommandRequest> m_pipelinedRequests;
	std::vector<LinearBuffer> m_pipelinedResponses;

	libdivide::divider<uint64_t> m_fastModDivisor;

	static const std::size_t kWriteHighWatermark = 8 * 1024 * 1024;   // 8 MB: pause reading
	static const std::size_t kWriteLowWatermark  = 256 * 1024;        // 256 KB: resume reading

	static const std::size_t kOutputHardLimit = 564 * 1024 * 1024;     // 564 MB
};