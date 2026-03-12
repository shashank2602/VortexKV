#include "server.h"

#include "asio.hpp"



int main(int argc, char* argv[])
{
	Config config;

	if(argc > 1)
		config = Config::loadFromFile(argv[1]);

	asio::io_context ioContext;
	asio::signal_set signals(ioContext, SIGINT, SIGTERM);
	signals.async_wait([&](auto, auto) { ioContext.stop(); });

	Server server(ioContext, config);

	server.Start();


	ioContext.run();


	return 0;
}