#pragma once

#include "asio.hpp"
#include "commandDispatcher.h"
#include "utility.h"
#include "shard.h"

#include <vector>

using asio::ip::tcp;

class Server {

private:

	Config m_config;
	asio::ip::tcp::acceptor m_acceptor;
	int m_Shards = 0;
	int m_nextShard = 0;
	std::vector<std::unique_ptr<Shard>> m_shardPool;
	uint64_t m_routingHashSeed = 0;

public:

	Server(asio::io_context& io, Config& config);
	
	~Server();

	void Start();

	void Stop();

private:

	void DoAccept();

	int GetNextShard();
	
};