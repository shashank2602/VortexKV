#pragma once

#include "asio.hpp"
#include "commandDispatcher.h"
#include "utility.h"


using asio::ip::tcp;

class Server {

public:

	Server(asio::io_context& io, Config& config);
	
	void Start();

private:

	void DoAccept();

	void RunMaintenanceLoop();

	Config m_config;
	asio::ip::tcp::acceptor m_acceptor;
	CommandDispatcher m_dispatcher;
	Database m_database;
	asio::steady_timer m_maintenanceTimer;
};