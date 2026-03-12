#include "respWriter.h"

#include <charconv>
#include <optional>
#include <cstring>


void RESPWriter::writeSimpleString(std::string_view data, LinearBuffer& responseBuffer)
{
	int totalSize = 1 + static_cast<int>(data.size()) + 2;
	char* bufferPtr = responseBuffer.reserveContiguous(totalSize);
	*bufferPtr++ = '+';
	memcpy(bufferPtr, data.data(), data.size());
	bufferPtr += data.size();
	*bufferPtr++ = '\r';
	*bufferPtr = '\n';
}

void RESPWriter::writeInteger(std::string_view data, LinearBuffer& responseBuffer)
{
	int totalSize = 1 + static_cast<int>(data.size()) + 2;
	char* bufferPtr = responseBuffer.reserveContiguous(totalSize);
	*bufferPtr++ = ':';
	memcpy(bufferPtr, data.data(), data.size());
	bufferPtr += data.size();
	*bufferPtr++ = '\r';
	*bufferPtr = '\n';
}

void RESPWriter::writeError(std::string_view data, LinearBuffer& responseBuffer)
{
	int totalSize = 1 + static_cast<int>(data.size()) + 2;
	char* bufferPtr = responseBuffer.reserveContiguous(totalSize);
	*bufferPtr++ = '-';
	memcpy(bufferPtr, data.data(), data.size());
	bufferPtr += data.size();
	*bufferPtr++ = '\r';
	*bufferPtr = '\n';
}

void RESPWriter::writeBulkString(std::optional<std::string_view> data, LinearBuffer& responseBuffer)
{
	if(!data.has_value())
	{
		responseBuffer.append("$-1\r\n", 5);
		return;
	}
	char stackBuffer[24];
	stackBuffer[0] = '$';
	auto [stackBufferEnd, ec] = std::to_chars(stackBuffer + 1, stackBuffer + sizeof(stackBuffer) - 1, data.value().size());
	*stackBufferEnd++ = '\r';
	*stackBufferEnd++ = '\n';

	int totalSize = stackBufferEnd - stackBuffer + static_cast<int>(data.value().size()) + 2;
	char* bufferPtr = responseBuffer.reserveContiguous(totalSize);
	memcpy(bufferPtr, stackBuffer, stackBufferEnd - stackBuffer);
	bufferPtr += (stackBufferEnd - stackBuffer);
	memcpy(bufferPtr, data.value().data(), data.value().size());
	bufferPtr += data.value().size();
	*bufferPtr++ = '\r';
	*bufferPtr = '\n';

}

void RESPWriter::writeArrayHeader(int size, LinearBuffer& responseBuffer)
{
	char buffer[24];
	buffer[0] = '*';
	auto [ptr, ec] = std::to_chars(buffer + 1, buffer + sizeof(buffer) - 1, size);
	*ptr = '\r';
	*(ptr + 1) = '\n';
	responseBuffer.append(buffer, (ptr + 2) - buffer);
}