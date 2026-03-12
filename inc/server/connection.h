#pragma once

#include "asio.hpp"
#include "utility.h"
#include "respParser.h"
#include "commands.h"
#include "database.h"

#include <memory>

using asio::ip::tcp;

class Connection: public std::enable_shared_from_this<Connection>{

public:

	Connection(tcp::socket socket, CommandDispatcher& dispatcher, Database& database);

	~Connection();

	void Start();

private:

	void DoRead();

	void TryWrite();

	bool CheckResponseBufferLimits();

	void CloseConnection();

	tcp::socket m_socket;
	//std::array<char, 4096> m_socketBuffer;
	LinearBuffer m_requestBuffer;
	std::unique_ptr<LinearBuffer> m_pPrimaryResponseBuffer, m_pSecondaryResponseBuffer;
	CommandDispatcher& m_dispatcher;
	Database& m_database;
	bool m_writingInProgress;

	static const std::size_t kHardOutputLimit = 8 * 1024 * 1024;   // 8 MB
};