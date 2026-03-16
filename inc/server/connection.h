#pragma once

#include "asio.hpp"
#include "utility.h"
#include "respParser.h"
#include "commands.h"
#include "database.h"

#include <vector>
#include <memory>

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

	bool CheckResponseBufferLimits();

	void CloseConnection();

	tcp::socket m_socket;
	LinearBuffer m_requestBuffer;
	std::unique_ptr<LinearBuffer> m_pPrimaryResponseBuffer, m_pSecondaryResponseBuffer;
	bool m_writingInProgress;

	uint64_t m_routingHashSeed = 0;
	std::vector<std::unique_ptr<Shard>>& m_shardPool;
	int m_shardId = -1;
	Shard& m_ownerShard;
	CommandDispatcher& m_dispatcher;
	Database& m_database;

	//struct PipelinedResponse
	//{
	//	InlineResponseBuffer responseBuffer;
	//	bool isReadyToWrite = false;
	//};

	std::vector<InlineResponseBuffer> m_pipelinedResponses;

	int m_pipelinedRequestCount = 0;
	int m_pipelinedResponseCount = 0;
	uint64_t m_bytesConsumedInPipeline = 0;
	bool m_invalidProtocolError = false;
	bool m_pipelineReadingComplete = false;


	static const std::size_t kHardOutputLimit = 8 * 1024 * 1024;   // 8 MB
};