#pragma once

#include "utility.h"

#include <utility>
#include <optional>
#include <charconv>
#include <string_view>
#include <string>



class RESPParser {

public:

	static std::pair<ParseStatus, int> parse(const std::string_view data, CommandRequest& outCommand);


private:

	static ParseResult getLine(std::string_view data);

	static ParseResult getLineOfLength(std::string_view data, int length);

	static ParseResult parseLength(const std::string_view data);

	static ParseResult getBulkString(std::string_view data);

};
