#include "connection.h"
#include "shard.h"
#include "rapidhash.h"

#include <iostream>

Connection::Connection(tcp::socket socket, uint64_t routingHashSeed, std::vector<std::unique_ptr<Shard>>& shardPool, int shardId) :
																	m_socket(std::move(socket)), m_ownerShard(*shardPool[shardId]),
																	m_dispatcher(m_ownerShard.GetDispatcher()), m_database(m_ownerShard.GetDatabase()),
																	m_pPrimaryResponseBuffer(std::make_unique<LinearBuffer>()),
																	m_pSecondaryResponseBuffer(std::make_unique<LinearBuffer>()),
																	m_writingInProgress(false), m_routingHashSeed(routingHashSeed), m_shardPool(shardPool),
																	m_shardId(shardId), m_pipelinedResponses(16)	
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

			//constexpr int pipelineSize = 16;
			CommandRequest commandRequest;


			while (true)
			{	
				commandRequest.reset();
				
				std::string_view currentDataView = m_requestBuffer.getView();

				auto [parseStatus, bytesConsumed] = RESPParser::parse(currentDataView.substr(m_bytesConsumedInPipeline), commandRequest);

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
				
				RouteRequest(m_pipelinedRequestCount - 1, commandRequest);
			}
			
			m_pipelineReadingComplete = true;
			if(m_pipelinedRequestCount == m_pipelinedResponseCount)
				FlushPipelinedResponses();
		}
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
		targetShardId = hash % m_shardPool.size();
	}

	//std::cout<<"Owner shard: " << m_shardId << ", Target shard: " << targetShardId << std::endl;

	if(requestIndex >= m_pipelinedResponses.size())
		m_pipelinedResponses.resize(m_pipelinedResponses.size() * 2);

	if (targetShardId == m_shardId)
	{
		thread_local LinearBuffer localResponseBuffer(256);
		localResponseBuffer.reset();
		m_dispatcher.dispatch(request, m_database, localResponseBuffer);
		m_pipelinedResponses[requestIndex].assign(localResponseBuffer.readPtr(), localResponseBuffer.size());
		m_pipelinedResponseCount++;
	}
	else
	{
		auto self = shared_from_this();
		m_shardPool[targetShardId]->ExecuteRemote(std::move(request), m_ownerShard.GetIOContext(),
			[self, this, requestIndex](InlineResponseBuffer responseBuffer) {

				m_pipelinedResponses[requestIndex] = std::move(responseBuffer);
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
		m_pSecondaryResponseBuffer->append(m_pipelinedResponses[i].data(), m_pipelinedResponses[i].size());
			
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