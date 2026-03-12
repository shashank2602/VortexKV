#include "connection.h"

#include <iostream>

Connection::Connection(tcp::socket socket, CommandDispatcher& dispatcher, Database& database) :m_socket(std::move(socket)), m_dispatcher(dispatcher), m_database(database),
																							m_pPrimaryResponseBuffer(std::make_unique<LinearBuffer>()),
																							m_pSecondaryResponseBuffer(std::make_unique<LinearBuffer>()),
																							m_writingInProgress(false)
{
	//std::cout << "New connection accepted." << std::endl;
}

Connection::~Connection()
{
	//std::cout << "Connection closed." << std::endl;
}

void Connection::Start()
{
	DoRead();
}


void Connection::DoRead()
{
	auto self = shared_from_this();
	m_socket.async_read_some(
		asio::buffer(m_requestBuffer.writePtr(), m_requestBuffer.remainingWriteSize()),
		[this, self](asio::error_code errorCode, std::size_t bytesRead){
			
			if (errorCode)
			{
				CloseConnection();
				return;
			}

			m_requestBuffer.seekWrite(bytesRead);

			constexpr int pipelineSize = 16;
			CommandRequest CommandRequests[pipelineSize];


			while (true)
			{
				int currentPipelineIndex = 0;
				bool errorOccurredInParsing = false;
				bool pipelineFull = false;
				uint64_t totalBytesConsumed = 0;

				while (currentPipelineIndex < pipelineSize)
				{
					CommandRequests[currentPipelineIndex].reset();
					std::string_view currentDataView = m_requestBuffer.getView();

					auto [parseStatus, bytesConsumed] = RESPParser::parse(currentDataView.substr(totalBytesConsumed), CommandRequests[currentPipelineIndex]);

					if (parseStatus == ParseStatus::Success)
					{
						/*std::cout<<command.type<< " ";
						for (auto str : command.arguments)
							std::cout << str << " ";
						std::cout << "\n";*/
					}
					else if (parseStatus == ParseStatus::Error)
					{
						errorOccurredInParsing = true;
						break;
					}
					else if (parseStatus == ParseStatus::IncompleteData)
					{
						break;
					}

					++currentPipelineIndex;
					totalBytesConsumed += bytesConsumed;
				}

				pipelineFull = (currentPipelineIndex == pipelineSize);

				if (currentPipelineIndex == 0)
				{
					if (errorOccurredInParsing) [[unlikely]]
					{
						CloseConnection();
						return;
					}
					break;
				}

				constexpr int LOOKAHEAD = 4;

				for (int i = 0; i < std::min(LOOKAHEAD, currentPipelineIndex); ++i)
				{
					if (!CommandRequests[i].arguments.empty())
						m_database.prefetchForKey(CommandRequests[i].arguments[0]);
				}

				for (int i = 0; i < currentPipelineIndex; i++)
				{
					int prefetchIdx = i + LOOKAHEAD;
					if (prefetchIdx < currentPipelineIndex && !CommandRequests[prefetchIdx].arguments.empty())
						m_database.prefetchForKey(CommandRequests[prefetchIdx].arguments[0]);

					m_dispatcher.dispatch(CommandRequests[i], m_database, *m_pSecondaryResponseBuffer);
				}

				m_requestBuffer.seekRead(totalBytesConsumed);


				if (errorOccurredInParsing) [[unlikely]]
				{
					CloseConnection();
					return;
				}

				if(!pipelineFull)
					break;
			}


			TryWrite();

			if (CheckResponseBufferLimits())
				DoRead();
					
			
		}
	);
}

void Connection::TryWrite()
{
	if (m_writingInProgress || m_pSecondaryResponseBuffer->empty())
		return;


	m_writingInProgress = true;

	std::swap(m_pPrimaryResponseBuffer, m_pSecondaryResponseBuffer);

	auto self = shared_from_this();

	asio::async_write(m_socket, asio::buffer(m_pPrimaryResponseBuffer->readPtr(), m_pPrimaryResponseBuffer->size()), 
		
		[this, self](asio::error_code errorCode, std::size_t bytesWritten){
			
			m_writingInProgress = false;
			m_pPrimaryResponseBuffer->reset();
			if (!errorCode)
			{
				TryWrite();
			}
			else
			{
				CloseConnection();
			}
		
		}
	);
}


bool Connection::CheckResponseBufferLimits()
{
	size_t pendingSize = m_pPrimaryResponseBuffer->size() + m_pSecondaryResponseBuffer->size();
	if (pendingSize > kHardOutputLimit)
	{
		CloseConnection();
		return false;
	}
	return true;
}


void Connection::CloseConnection()
{
	asio::error_code ignored;
	m_socket.shutdown(tcp::socket::shutdown_both, ignored);
	m_socket.close(ignored);
}