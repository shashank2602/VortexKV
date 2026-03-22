#include "connection.h"
#include "shard.h"
#include "rapidhash.h"

#include <iostream>

Connection::Connection(tcp::socket socket, uint64_t routingHashSeed, std::vector<std::unique_ptr<Shard>>& shardPool, int shardId) :
																	m_socket(std::move(socket)), m_ownerShard(*shardPool[shardId]),
																	m_dispatcher(m_ownerShard.GetDispatcher()), m_database(m_ownerShard.GetDatabase()),
																	m_pPrimaryResponseBuffer(&m_primaryResponseBuffer),
																	m_pSecondaryResponseBuffer(&m_secondaryResponseBuffer),
																	m_writingInProgress(false), m_routingHashSeed(routingHashSeed), m_shardPool(shardPool),
																	m_shardId(shardId), m_pipelinedRequests(16), m_fastModDivisor(shardPool.size())
{
	m_pipelinedResponses.reserve(16);
	for (int i = 0 ; i < m_pipelinedResponses.capacity() ; i++)
		m_pipelinedResponses.emplace_back(128);
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
		asio::bind_allocator(asio::recycling_allocator<void>(),
		[this, self](asio::error_code errorCode, std::size_t bytesRead){
			
			if (errorCode)
			{
				CloseConnection();
				return;
			}

			m_requestBuffer.seekWrite(bytesRead);

			while (true)
			{
				if (m_pipelinedRequestCount >= m_pipelinedRequests.size())
					m_pipelinedRequests.resize(m_pipelinedRequests.size() * 2);

				m_pipelinedRequests[m_pipelinedRequestCount].reset();
				
				std::string_view currentDataView = m_requestBuffer.getView();

				auto [parseStatus, bytesConsumed] = RESPParser::parse(currentDataView.substr(m_bytesConsumedInPipeline), m_pipelinedRequests[m_pipelinedRequestCount]);

				if (parseStatus == ParseStatus::Success)
				{
					/*std::cout<<command.type<< " ";
					for (auto str : command.arguments)
						std::cout << str << " ";
					std::cout << "\n";*/
				}
				else if (parseStatus == ParseStatus::Error)
				{
					m_invalidProtocolError = true;
					break;
				}
				else if (parseStatus == ParseStatus::IncompleteData)
				{
					m_pipelineReadingComplete = true;
					break;
				}

				m_bytesConsumedInPipeline += bytesConsumed;
				m_pipelinedRequestCount++;
			}

			if(m_pipelinedRequestCount > m_pipelinedResponses.size())
			{
				m_pipelinedResponses.reserve(m_pipelinedRequestCount);
				while (m_pipelinedResponses.size() < m_pipelinedRequestCount)
					m_pipelinedResponses.emplace_back(128);
			}


			for (int i = 0; i < m_pipelinedRequestCount; ++i)
				RouteRequest(i, m_pipelinedRequests[i]);

			m_pipelineReadingComplete = true;
			 if(m_pipelinedRequestCount == m_pipelinedResponseCount)
			 	FlushPipelinedResponses();
		})
	);
}


void Connection::RouteRequest(int requestIndex, CommandRequest& request)
{
	int targetShardId;

	if (request.arguments.empty())
	{
		targetShardId = m_shardId;
	}
	else
	{
		uint64_t hash = rapidhash_withSeed(request.arguments[0].data(), request.arguments[0].size(), m_routingHashSeed);
		uint64_t quotient = hash / m_fastModDivisor;
		targetShardId = hash - (quotient * m_shardPool.size());
	}

	//std::cout<<"Owner shard: " << m_shardId << ", Target shard: " << targetShardId << std::endl;

	if (targetShardId == m_shardId)
	{
		m_pipelinedResponses[requestIndex].reset();
		m_dispatcher.dispatch(request, m_database, m_pipelinedResponses[requestIndex]);
		m_pipelinedResponseCount++;
	}
	else
	{
		auto self = shared_from_this();
		m_shardPool[targetShardId]->ExecuteRemote(std::move(request), m_pipelinedResponses[requestIndex], m_ownerShard.GetIOContext(),
			[self, this]() {

				m_pipelinedResponseCount++;
				if(m_pipelineReadingComplete && m_pipelinedRequestCount == m_pipelinedResponseCount)
					FlushPipelinedResponses();
			}
			
		);
	}
}


void Connection::FlushPipelinedResponses()
{
	for(int i = 0; i < m_pipelinedRequestCount; ++i)
		m_pSecondaryResponseBuffer->append(m_pipelinedResponses[i].readPtr(), m_pipelinedResponses[i].size());
			
	m_pipelinedRequestCount = 0;
	m_pipelinedResponseCount = 0;

	m_requestBuffer.seekRead(m_bytesConsumedInPipeline);
	m_bytesConsumedInPipeline = 0;

	m_pipelineReadingComplete = false;

	TryWrite();

	if (m_invalidProtocolError) [[unlikely]]
	{
		CloseConnection();
		return;
	}

	if (CheckResponseBufferLimits())
		DoRead();
}


void Connection::TryWrite()
{
	if (m_writingInProgress || m_pSecondaryResponseBuffer->empty())
		return;


	m_writingInProgress = true;

	std::swap(m_pPrimaryResponseBuffer, m_pSecondaryResponseBuffer);

	auto self = shared_from_this();

	asio::async_write(
		m_socket, asio::buffer(m_pPrimaryResponseBuffer->readPtr(), m_pPrimaryResponseBuffer->size()),
		asio::bind_allocator(asio::recycling_allocator<void>(),
		
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
		
		})
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