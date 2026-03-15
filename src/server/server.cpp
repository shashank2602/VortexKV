#include "server.h"
#include "connection.h"

#include <memory>
#include <iostream>


Server::Server(asio::io_context& io, Config& config) : m_config(config), 
														m_acceptor(io, tcp::endpoint(asio::ip::make_address(config.bind), config.port))
{
	m_config = config;

	RandomGenerator randomGen;
	m_routingHashSeed = randomGen.getRandomInteger(1, std::numeric_limits<long long>::max());

	m_Shards = std::thread::hardware_concurrency();
	m_shardPool.reserve(m_Shards);

	for (int i = 0; i < m_Shards; ++i)
		m_shardPool.emplace_back(std::make_unique<Shard>(i, m_config, m_routingHashSeed, m_shardPool));
}

Server::~Server()
{
	Stop();
}


void Server::Start()
{
	std::cout << "Server started, waiting for connections..." << std::endl;

	for(int i = 0; i < m_Shards; ++i)
		m_shardPool[i]->Start();

	DoAccept();
}


void Server::Stop()
{
	for (int i = 0; i < m_Shards; ++i)
		m_shardPool[i]->Stop();
}


void Server::DoAccept()
{
	m_acceptor.async_accept(
		[this](asio::error_code errorCode, tcp::socket socket) {
			if (!errorCode)
			{
				int shardId = GetNextShard();
				m_shardPool[shardId]->AcceptConnection(std::move(socket));
			}
			else
			{
				std::cerr << "Accept error: " << errorCode.message() << std::endl;
			}
			DoAccept();
		}
	);
}


int Server::GetNextShard() {
	int shardId = m_nextShard;
	m_nextShard = (m_nextShard + 1) % m_Shards;
	return shardId;
}