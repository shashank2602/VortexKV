#include "utility.h"

#include <cstring>


RandomGenerator::RandomGenerator() : m_gen(std::random_device{}()) {}

RandomGenerator::RandomGenerator(int64_t min, int64_t max) : m_gen(std::random_device{}()), m_dist(min, max) {}

int64_t RandomGenerator::getRandomInteger(int64_t min, int64_t max)
{
	if (min != m_min || max != m_max)
	{
		m_min = min;
		m_max = max;
		m_dist = std::uniform_int_distribution<int64_t>(min, max);
	}
	return m_dist(m_gen);
}


InlineResponseBuffer::InlineResponseBuffer(const char* data, uint64_t size)
{
	m_size = size;
	if (size <= kInlineCapacity)
		std::memcpy(m_inlineBuffer, data, size);
	else
		m_heapBuffer.assign(data, data + size);
	
}

void InlineResponseBuffer::assign(const char* data, uint64_t size) {
	m_size = size;
	if (size <= kInlineCapacity)
		std::memcpy(m_inlineBuffer, data, size);
	else
		m_heapBuffer.assign(data, data + size);
}


InlineResponseBuffer::InlineResponseBuffer(InlineResponseBuffer&& other) noexcept
{
	if (this == &other)
		return;

	this->m_size = other.m_size;
	if(other.m_size > kInlineCapacity)
		m_heapBuffer = std::move(other.m_heapBuffer);
	else
		std::memcpy(this->m_inlineBuffer, other.m_inlineBuffer, other.m_size);
	other.m_size = 0;
}

InlineResponseBuffer& InlineResponseBuffer::operator=(InlineResponseBuffer&& other) noexcept
{
	if(this == &other)
		return *this;

	this->m_size = other.m_size;
	if (other.m_size > kInlineCapacity)
		m_heapBuffer = std::move(other.m_heapBuffer);
	else
		std::memcpy(this->m_inlineBuffer, other.m_inlineBuffer, other.m_size);
	other.m_size = 0;

	return *this;
}



void PinThreadToCore(std::jthread& thread, int coreId)
{
#if defined(_WIN32) || defined(_WIN64)
	// Windows
	HANDLE threadHandle = reinterpret_cast<HANDLE>(thread.native_handle());
	DWORD_PTR mask = 1ULL << coreId;
	SetThreadAffinityMask(threadHandle, mask);
#else
	// Linux and other Unix-like systems
	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(coreId, &cpuset);
	pthread_setaffinity_np(thread.native_handle(), sizeof(cpu_set_t), &cpuset);
#endif
}