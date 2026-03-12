#include "server.h"
#include "connection.h"

#include <memory>
#include <iostream>


Server::Server(asio::io_context& io, Config& config) : m_config(config), 
														m_acceptor(io, tcp::endpoint(asio::ip::make_address(config.bind), config.port)), 
														m_maintenanceTimer(io),
														m_database(config.maxMemoryUsage)		
{
	m_config = config;
	registerCommands(m_dispatcher);
}

void Server::Start()
{
	std::cout << "Server started, waiting for connections..." << std::endl;
	DoAccept();
	RunMaintenanceLoop();
}


void Server::DoAccept()
{
	m_acceptor.async_accept(
		[this](asio::error_code errorCode, tcp::socket socket) {
			if (!errorCode)
			{
				asio::ip::tcp::no_delay option(true);
				socket.set_option(option);
				std::make_shared<Connection>(std::move(socket), m_dispatcher, m_database)->Start();
			}
			else
			{
				std::cerr << "Accept error: " << errorCode.message() << std::endl;
			}
			DoAccept();
		}
	);
}

void Server::RunMaintenanceLoop()
{
	m_maintenanceTimer.expires_after(std::chrono::milliseconds(m_config.maintenanceIntervalMs));
	m_maintenanceTimer.async_wait([this](const asio::error_code& ec)
		{
			if (!ec)
			{
				m_database.runMaintenance();
				RunMaintenanceLoop();
			}
		}
	
	);
}