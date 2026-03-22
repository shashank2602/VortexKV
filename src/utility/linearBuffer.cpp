#include "linearBuffer.h"

#include <cstring>

LinearBuffer::LinearBuffer(size_t initialSize)
{
    m_buffer.resize(initialSize);
	m_head = 0;
	m_tail = 0;
}


void LinearBuffer::append(const char* data, size_t size)
{
	if (remainingWriteSize() < size)
	{
		size_t currentDataSize = this->size();
		if (currentDataSize + size > capacity())
		{
			m_buffer.resize((currentDataSize + size) * 2);
		}

		compaction();
	}

	memcpy(m_buffer.data() + m_head, data, size);
	m_head += size;
}


char* LinearBuffer::reserveContiguous(size_t size) {

	if (remainingWriteSize() < size) 
	{
		size_t currentDataSize = this->size();
		if (currentDataSize + size > capacity())
		{
			m_buffer.resize((currentDataSize + size) * 2);
		}

		compaction();
	}
	char* ptr = m_buffer.data() + m_head;
	m_head += size;
	return ptr;
}


void LinearBuffer::seekRead(size_t size)
{
	m_tail = std::min(m_tail + size, m_head);
	if (m_tail == m_head)
		reset();
}

void LinearBuffer::seekWrite(size_t size)
{
	m_head = std::min(m_head + size, capacity());

    if (capacity() - m_head < static_cast<size_t>(minBufferSpaceRequired))
	{
		compaction();

		if (capacity() - m_head < static_cast<size_t>(minBufferSpaceRequired))
		{
			m_buffer.resize(capacity() * 2);
		}
	}
}

void LinearBuffer::compaction()
{
	size_t currentDataSize = this->size();
	if (m_tail > 0)
	{
		memmove(m_buffer.data(), m_buffer.data() + m_tail, currentDataSize);
		m_head = currentDataSize;
		m_tail = 0;
	}
}