#include "respParser.h"



static constexpr int kMaxBulkLen = 512 * 1024 * 1024; // 500 mb , Redis default max bulk string length
static constexpr int kMaxMultibulkLen = 1024 * 1024;  // 1M elements per command, matches Redis' hard cap


std::pair<ParseStatus, int> RESPParser::parse(const std::string_view data, CommandRequest& outCommand)
{
	outCommand.reset();

	if (data.empty())
		return { ParseStatus::IncompleteData, 0 };

	if (data[0] != '*')
		return { ParseStatus::Error, 1 };

	int totalDataRead = 1;
	
	auto [parseLengthStatus, parseLengthDataRead, arrLength, _] = parseLength(data.substr(totalDataRead));


	if (parseLengthStatus != ParseStatus::Success)
	{
		if (parseLengthStatus == ParseStatus::IncompleteData)
			return { ParseStatus::IncompleteData, 0 };

		return { parseLengthStatus, totalDataRead + parseLengthDataRead };
	}

	if (arrLength < 1 || arrLength > kMaxMultibulkLen)
		return { ParseStatus::Error, totalDataRead + parseLengthDataRead };

	totalDataRead += parseLengthDataRead;

	for (int i = 0; i < arrLength; i++)
	{
		auto [status, dataRead, _, bulkStr] = getBulkString(data.substr(totalDataRead));

		totalDataRead += dataRead;

		if (status == ParseStatus::Success)
		{
			if (i == 0)
				outCommand.type = bulkStr;
			else
				outCommand.arguments.push_back(bulkStr);
		}
		else if(status == ParseStatus::Error)
		{
			return { status , totalDataRead };
		}
		else if (status == ParseStatus::IncompleteData)
		{
			return { status, 0};
		}
	}

	return { ParseStatus::Success , totalDataRead };
}


ParseResult RESPParser::getBulkString(std::string_view data)
{
	if (data.empty())
		return { ParseStatus::IncompleteData, 0, 0, {} };

	if (data[0] != '$')
		return { ParseStatus::Error, 1, 0, {} };

	auto [parseLengthStatus, parseLengthDataRead, strLength, _] = parseLength(data.substr(1));

	if (parseLengthStatus == ParseStatus::Success)
	{
		if (strLength == -1)
			return { parseLengthStatus, 1 + parseLengthDataRead, {}, {} };

		if (strLength < -1 || strLength > kMaxBulkLen)
			return { ParseStatus::Error, 1 + parseLengthDataRead, {}, {} };

		auto [status, dataRead, _, str] = getLineOfLength(data.substr(1 + parseLengthDataRead), strLength);
		if (status == ParseStatus::Success)
		{
			return { ParseStatus::Success, 1 + parseLengthDataRead + dataRead, {}, str};
		}
		else if (status == ParseStatus::Error)
		{
			return { ParseStatus::Error, 1 + parseLengthDataRead + dataRead, {}, {} };
		}
		return { status, 0, {}, {} };
	}
	else if (parseLengthStatus == ParseStatus::Error)
	{
		return { parseLengthStatus, parseLengthDataRead + 1, {}, {} };
	}

	return { parseLengthStatus, 0, {}, {} };
}


ParseResult RESPParser::parseLength(const std::string_view data)
{
	int value = 0;
	auto [status, dataRead, _, numStr] = getLine(data);
	if (status == ParseStatus::Success)
	{
		auto [ptr, ec] = std::from_chars(numStr.data(), numStr.data() + numStr.size(), value);
		if (ec == std::errc() && ptr == numStr.data() + numStr.size())
			return { status, dataRead, value, {} };
		else
			return { ParseStatus::Error, dataRead, {}, {} };
	}

	return { status, 0, {}, {} };
}

ParseResult RESPParser::getLine(std::string_view data)
{
	size_t pos = data.find("\r\n");
	if (pos == std::string_view::npos)
		return { ParseStatus::IncompleteData, 0, {}, {} };
	return { ParseStatus::Success, (int)pos + 2, {}, data.substr(0, pos)};
}

ParseResult RESPParser::getLineOfLength(std::string_view data, int length)
{
	if(length < 0)
		return { ParseStatus::Error, 0, {}, {} };
	if(data.size() < length + 2)
		return { ParseStatus::IncompleteData, 0, {}, {} };
	if(data[length] != '\r' || data[length + 1] != '\n')
		return { ParseStatus::Error, length + 2, {}, {} };
	return { ParseStatus::Success, length + 2, {}, data.substr(0, length) };
}