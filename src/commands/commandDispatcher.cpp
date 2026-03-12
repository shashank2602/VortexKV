#include "commandDispatcher.h"
#include "respWriter.h"

#include <format>
#include <cstring>
#include <cassert>


CommandDispatcher::CommandDispatcher() = default;


void CommandDispatcher::registerCommand(std::string_view name, int minArgs, int maxArgs, commandHandler handler)
{
	assert(m_commandCount < MAX_COMMANDS && "Too many commands registered, increase MAX_COMMANDS value");
	assert(name.size() < 9 && "Command name too long, only less than equal to 8 characters are supported");

	CommandEntry& entry = m_commands[m_commandCount++];

	entry.nameKey = transformToUpperAndPackInInteger(name.data(), name.size());
	entry.handler  = handler;
	entry.minArgs  = minArgs;
	entry.maxArgs  = maxArgs;
}


void CommandDispatcher::dispatch(CommandRequest& command, Database& database, LinearBuffer& responseBuffer)
{
	uint64_t nameKey = transformToUpperAndPackInInteger(command.type.data(), command.type.size());

	for (uint8_t i = 0; i < m_commandCount; ++i) {
		if (m_commands[i].nameKey != nameKey)
			continue;

		const int argCount = static_cast<int>(command.arguments.size());
		if (argCount < m_commands[i].minArgs || argCount > m_commands[i].maxArgs) {
			auto [out, size] = std::format_to_n(m_errorBuffer.data(), m_errorBuffer.size(),
			    "ERR wrong number of arguments for '{}' command", command.type);
			RESPWriter::writeError(std::string_view(m_errorBuffer.data(), out - m_errorBuffer.data()), responseBuffer);
			return;
		}

		m_commands[i].handler(command, database, responseBuffer);
		return;
	}

	auto [out, size] = std::format_to_n(m_errorBuffer.data(), m_errorBuffer.size(),
	    "ERR unknown command '{}'", command.type);
	RESPWriter::writeError(std::string_view(m_errorBuffer.data(), out - m_errorBuffer.data()), responseBuffer);
}

