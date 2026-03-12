#pragma once


#include "commandDispatcher.h"


#include <string_view>
#include <vector>



void registerCommands(CommandDispatcher& dispatcher);

void handler_PING(CommandRequest& command, Database& db, LinearBuffer& responseBuffer);
void handler_ECHO(CommandRequest& command, Database& db, LinearBuffer& responseBuffer);
void handler_SET(CommandRequest& command, Database& db, LinearBuffer& responseBuffer);
void handler_GET(CommandRequest& command, Database& db, LinearBuffer& responseBuffer);
void handler_DEL(CommandRequest& command, Database& db, LinearBuffer& responseBuffer);
void handler_EXISTS(CommandRequest& command, Database& db, LinearBuffer& responseBuffer);
void handler_INCR(CommandRequest& command, Database& db, LinearBuffer& responseBuffer);
void handler_DECR(CommandRequest& command, Database& db, LinearBuffer& responseBuffer);
void handler_INCRBY(CommandRequest& command, Database& db, LinearBuffer& responseBuffer);
void handler_DECRBY(CommandRequest& command, Database& db, LinearBuffer& responseBuffer);
void handler_EXPIRE(CommandRequest& command, Database& db, LinearBuffer& responseBuffer);
void handler_PERSIST(CommandRequest& command, Database& db, LinearBuffer& responseBuffer);
void handler_TTL(CommandRequest& command, Database& db, LinearBuffer& responseBuffer);