#pragma once

#include "utility.h"
#include "database.h"

#include <string>
#include <string_view>
#include <cstring>


using commandHandler = void(*)(CommandRequest& command, Database& db, LinearBuffer& responseBuffer);


struct CommandInfo {
	commandHandler handler;
	int minArgs;
	int maxArgs;
};

// Note - Only supports command names up to 8 characters
struct CommandEntry {
	uint64_t       nameKey;		// characters of the command name converted to uppercase then packed into an integer for fast comparison
	commandHandler handler;		
	uint32_t       minArgs;
	uint32_t       maxArgs;
};


class CommandDispatcher {

public:

	static constexpr uint8_t MAX_COMMANDS = 32;

	CommandDispatcher();
	~CommandDispatcher() = default;
	
	// Only supports command names up to 8 characters, , Longer command names would require a different approach (e.g. string comparison or a hash map).
	void registerCommand(std::string_view name, int minArgs, int maxArgs, commandHandler handler);

	void dispatch(CommandRequest& command, Database& database, LinearBuffer& responseBuffer);


private:

	CommandEntry m_commands[MAX_COMMANDS] = {};
	uint8_t      m_commandCount = 0;
	std::array<char, 256> m_errorBuffer;	// only used on the error path
};