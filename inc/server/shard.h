#pragma once

#include "commandDispatcher.h"
#include "database.h"
#include "asio.hpp"


#include <thread>
#include <vector>
#include <memory>


using asio::ip::tcp;


class alignas(64) Shard {

private:

	int m_id;

	Config m_config;
	CommandDispatcher m_dispatcher;
	Database m_database;

	asio::io_context m_ioContext;
	asio::executor_work_guard<asio::io_context::executor_type> m_workGuard;
	asio::steady_timer m_maintenanceTimer;

	std::jthread m_thread;

	uint64_t m_routingHashSeed = 0;

	std::vector<std::unique_ptr<Shard>>& m_shardPool;

public:

	Shard(int id, Config& config, uint64_t routingHashSeed, std::vector<std::unique_ptr<Shard>>& shardPool);

	~Shard();

	void Start();

	void AcceptConnection(tcp::socket socket);

	void Stop();

	using CompletionCallback = std::function<void()>;

	void ExecuteRemote(CommandRequest request, LinearBuffer& responseBuffer, asio::io_context& callerContext, CompletionCallback completionCallback);

	asio::io_context& GetIOContext() { return m_ioContext; }

	Database& GetDatabase() { return m_database; }

	CommandDispatcher& GetDispatcher() { return m_dispatcher; }

private:

	void Run();

	void RunMaintenanceLoop();

};