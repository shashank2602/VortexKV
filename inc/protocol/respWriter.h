#pragma once

#include "utility.h"

#include <string>
#include <string_view>
#include <optional>




class RESPWriter {

public:

	static void writeSimpleString(std::string_view data, LinearBuffer& responseBuffer);

	static void writeInteger(std::string_view data, LinearBuffer& responseBuffer);

	static void writeError(std::string_view data, LinearBuffer& responseBuffer);

	static void writeBulkString(std::optional<std::string_view> data, LinearBuffer& responseBuffer);

	static void writeArrayHeader(int size, LinearBuffer& responseBuffer);

};