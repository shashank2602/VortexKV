#include "commands.h"
#include "respWriter.h"
#include "databaseErrors.h"

#include <limits>
#include <format>

void handler_PING(CommandRequest& command, Database& db, LinearBuffer& responseBuffer)
{
	if (command.arguments.empty())
		RESPWriter::writeSimpleString("PONG", responseBuffer);
	else
		RESPWriter::writeBulkString(command.arguments[0], responseBuffer);
}

void handler_ECHO(CommandRequest& command, Database& db, LinearBuffer& responseBuffer)
{
	RESPWriter::writeBulkString(command.arguments[0], responseBuffer);
}

void handler_SET(CommandRequest& command, Database& db, LinearBuffer& responseBuffer)
{
	DBResult result(DatabaseError::SUCCESS);
	char buffer[64];

	if (command.arguments.size() == 2)
	{
		result = db.SET(command.arguments[0], command.arguments[1]);
	}
	else if(command.arguments.size() == 4)
	{
		const auto& option = command.arguments[2];
		if (option.size() == 2 &&
			(option[0] == 'P' || option[0] == 'p') &&
			(option[1] == 'X' || option[1] == 'x'))
		{
			result = db.SET(command.arguments[0], command.arguments[1], command.arguments[3]);
		}
		else
		{
			RESPWriter::writeError("ERR syntax error", responseBuffer);
			return;
		}
	}
	else
	{
		auto [out, size] = std::format_to_n(buffer, sizeof(buffer), "ERR wrong number of arguments for '{}' command", command.type);
		RESPWriter::writeError(std::string_view(buffer, out - buffer), responseBuffer);
		return;
	}
		
	
	if (result.error != DatabaseError::SUCCESS)
		RESPWriter::writeError(databaseErrorToString(result.error), responseBuffer);
	else
		RESPWriter::writeSimpleString("OK", responseBuffer);
}

void handler_GET(CommandRequest& command, Database& db, LinearBuffer& responseBuffer)
{
	auto value = db.GET(command.arguments[0]);
	if (value.error == DatabaseError::SUCCESS)
		RESPWriter::writeBulkString(value.str(), responseBuffer);
	else
		RESPWriter::writeBulkString(std::nullopt, responseBuffer);
}

void handler_DEL(CommandRequest& command, Database& db, LinearBuffer& responseBuffer)
{
	auto deletedCount = db.DEL(command.arguments);
	if(deletedCount.error == DatabaseError::SUCCESS)
		RESPWriter::writeInteger(deletedCount.str(), responseBuffer);
	else
		RESPWriter::writeInteger("0", responseBuffer);
}

void handler_EXISTS(CommandRequest& command, Database& db, LinearBuffer& responseBuffer)
{
	auto existsCount = db.EXISTS(command.arguments);
	if (existsCount.error == DatabaseError::SUCCESS)
		RESPWriter::writeInteger(existsCount.str(), responseBuffer);
	else
		RESPWriter::writeInteger("0", responseBuffer);
}

void handler_INCR(CommandRequest& command, Database& db, LinearBuffer& responseBuffer)
{
	auto result = db.INCR(command.arguments[0]);
	if(result.error == DatabaseError::SUCCESS)
		RESPWriter::writeInteger(result.str(), responseBuffer);
	else
		RESPWriter::writeError(databaseErrorToString(result.error), responseBuffer);
}

void handler_DECR(CommandRequest& command, Database& db, LinearBuffer& responseBuffer)
{
	auto result = db.DECR(command.arguments[0]);
	if (result.error == DatabaseError::SUCCESS)
		RESPWriter::writeInteger(result.str(), responseBuffer);
	else
		RESPWriter::writeError(databaseErrorToString(result.error), responseBuffer);
}

void handler_INCRBY(CommandRequest& command, Database& db, LinearBuffer& responseBuffer)
{
	auto result = db.INCRBY(command.arguments[0], command.arguments[1]);
	if (result.error == DatabaseError::SUCCESS)
		RESPWriter::writeInteger(result.str(), responseBuffer);
	else
		RESPWriter::writeError(databaseErrorToString(result.error), responseBuffer);
}

void handler_DECRBY(CommandRequest& command, Database& db, LinearBuffer& responseBuffer)
{
	auto result = db.DECRBY(command.arguments[0], command.arguments[1]);
	if (result.error == DatabaseError::SUCCESS)
		RESPWriter::writeInteger(result.str(), responseBuffer);
	else
		RESPWriter::writeError(databaseErrorToString(result.error), responseBuffer);
}

void handler_EXPIRE(CommandRequest& command, Database& db, LinearBuffer& responseBuffer)
{
	auto result = db.EXPIRE(command.arguments[0], command.arguments[1]);
	if (result.error == DatabaseError::NOT_AN_INTEGER_OR_OUT_OF_RANGE)
		RESPWriter::writeError(databaseErrorToString(result.error), responseBuffer);
	else
		RESPWriter::writeInteger(result.str(), responseBuffer);
}

void handler_PERSIST(CommandRequest& command, Database& db, LinearBuffer& responseBuffer)
{
	auto result = db.PERSIST(command.arguments[0]);
	RESPWriter::writeInteger(result.str(), responseBuffer);
}

void handler_TTL(CommandRequest& command, Database& db, LinearBuffer& responseBuffer)
{
	auto result = db.TTL(command.arguments[0]);
	RESPWriter::writeInteger(result.str(), responseBuffer);
}

void registerCommands(CommandDispatcher& dispatcher)
{
	// Hottest commands first for better cache performance in the dispatcher
	dispatcher.registerCommand("GET", 1, 1, handler_GET);
	dispatcher.registerCommand("SET", 2, 4, handler_SET);
	
	dispatcher.registerCommand("PING", 0, 1, handler_PING);
	dispatcher.registerCommand("ECHO", 1, 1, handler_ECHO);
	dispatcher.registerCommand("DEL", 1, std::numeric_limits<int>::max(), handler_DEL);
	dispatcher.registerCommand("EXISTS", 1, std::numeric_limits<int>::max(), handler_EXISTS);
	dispatcher.registerCommand("INCR", 1, 1, handler_INCR);
	dispatcher.registerCommand("DECR", 1, 1, handler_DECR);
	dispatcher.registerCommand("INCRBY", 2, 2, handler_INCRBY);
	dispatcher.registerCommand("DECRBY", 2, 2, handler_DECRBY);
	dispatcher.registerCommand("EXPIRE", 2, 2, handler_EXPIRE);
	dispatcher.registerCommand("PERSIST", 1, 1, handler_PERSIST);
	dispatcher.registerCommand("TTL", 1, 1, handler_TTL);
}




