#pragma once

#include "commandDispatcher.h"
#include "database.h"
#include "asio.hpp"


#include <thread>
#include <vector>
#include <memory>
#include <chrono>


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

	std::chrono::steady_clock::time_point m_coarseNow{ std::chrono::steady_clock::now() }; // Coarse monotonic clock, refreshed once per maintenance tick (~maintenanceIntervalMs).

	std::vector<std::unique_ptr<Shard>>& m_shardPool;

public:

	Shard(int id, Config& config, uint64_t routingHashSeed, std::vector<std::unique_ptr<Shard>>& shardPool);

	~Shard();

	void Start();

	void AcceptConnection(tcp::socket socket);

	void Stop();

	template <typename Callback>
	void ExecuteRemote(CommandRequest request, LinearBuffer& responseBuffer, asio::io_context& callerContext, Callback completionCallback)
	{
		asio::post(m_ioContext,
			[this, request = std::move(request), &responseBuffer, &callerContext, completionCallback = std::forward<Callback>(completionCallback)]() mutable {

				responseBuffer.reset();
				m_dispatcher.dispatch(request, m_database, responseBuffer);

				asio::post(callerContext, std::move(completionCallback));
			}

		);
	}

	asio::io_context& GetIOContext() { return m_ioContext; }

	int GetIdleTimeoutSec() const { return m_config.idleTimeoutSec; }

	std::chrono::steady_clock::time_point CoarseNow() const { return m_coarseNow; }

	Database& GetDatabase() { return m_database; }

	CommandDispatcher& GetDispatcher() { return m_dispatcher; }

private:

	void Run();

	void RunMaintenanceLoop();

};