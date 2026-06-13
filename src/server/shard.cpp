#include "shard.h"
#include "commands.h"
#include "connection.h"

#include <memory>


Shard::Shard(int id, Config& config, uint64_t routingHashSeed, std::vector<std::unique_ptr<Shard>>& shardPool) : m_id(id), m_config(config),
										m_database(config.maxMemoryUsage/config.shardCount),	m_maintenanceTimer(m_ioContext), m_workGuard(asio::make_work_guard(m_ioContext)),
										m_routingHashSeed(routingHashSeed), m_shardPool(shardPool)						
{
	registerCommands(m_dispatcher);
}


void Shard::Start()
{
	std::cout << "Shard " << m_id << " started." << std::endl;
	m_thread = std::jthread([this]() { Run(); });

	PinThreadToCore(m_thread, m_id % static_cast<int>(std::thread::hardware_concurrency()));
}

Shard::~Shard()
{
	Stop();
}

void Shard::AcceptConnection(tcp::socket socket)
{
	auto protocol = socket.local_endpoint().protocol();
	auto nativeHandle = socket.release();

	asio::post(m_ioContext, [nativeHandle, protocol, this]() mutable {
		tcp::socket shardSocket(m_ioContext);
		shardSocket.assign(protocol, nativeHandle);
		asio::ip::tcp::no_delay option(true);
		shardSocket.set_option(option);
		std::make_shared<Connection>(std::move(shardSocket), m_routingHashSeed, m_shardPool, m_id)->Start();
	});
}



void Shard::Run()
{
	RunMaintenanceLoop();
	m_ioContext.run();

	// Clear database on the shard's own thread before thread-local SlabAllocator is destroyed.
	// CompactStrings must be deallocated by the same thread's allocator that allocated them.
	m_database.clear();
}


void Shard::Stop()
{
	m_workGuard.reset();
	m_ioContext.stop();
	if (m_thread.joinable())
		m_thread.join();
}


void Shard::RunMaintenanceLoop()
{
	m_maintenanceTimer.expires_after(std::chrono::milliseconds(m_config.maintenanceIntervalMs));
	m_maintenanceTimer.async_wait([this](const asio::error_code& ec)
		{
			if (!ec)
			{
				m_coarseNow = std::chrono::steady_clock::now();
				m_database.runMaintenance();
				RunMaintenanceLoop();
			}
		}
	);
}