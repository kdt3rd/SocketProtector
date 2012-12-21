//
// Copyright (c) 2012 Kimball Thurston
//
// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
// CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//

#include <iostream>
#include <SocketProtector.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#include <syslog.h>

static SocketProtector *globPtr = NULL;

static void
handleTerm( int )
{
	printf( "SIGTERM\n" );
	if ( globPtr )
		globPtr->terminate();
}

static void
handleInt( int )
{
	printf( "SIGINT\n" );
	if ( globPtr )
		globPtr->terminate();
}

int
main( int argc, char *argv[] )
{
	if ( argc < 2 )
	{
		std::cerr << "Usage: sample_client <port>" << std::endl;
		return -1;
	}
	long port = strtol( argv[1], NULL, 10 );
	if ( port <= 0 || port > 65535 )
	{
		std::cerr << "invalid port specification: " << argv[1] << std::endl;
		return -1;
	}
	std::cout << "Starting client up on port " << port << std::endl;

	signal( SIGTERM, &handleTerm );
	signal( SIGINT, &handleInt );

	openlog( "sockprot_client", LOG_PID | LOG_NOWAIT | LOG_CONS | LOG_PERROR, LOG_DAEMON );
	( void )setlogmask( LOG_UPTO( LOG_DEBUG ) );

	std::cout << "Sleeping for 10 seconds..." << std::endl;
	sleep( 10 );
	std::cout << "And we're back..." << std::endl;
	
	SocketProtector pt( static_cast<uint16_t>( port ) );
	globPtr = &pt;

	do
	{
		if ( pt.is_terminated() )
			break;

		int sock = pt.accept();

		if ( sock == -1 )
			break;

		std::cout << "\nnew connection: " << sock << std::endl;
		do
		{
			char b;
			if ( read( sock, &b, 1 ) == 1 )
			{
				std::cout << b;
			}
			else
			{
				std::cout << std::endl;
				break;
			}
		} while ( true );
		close( sock );
	} while ( true );
	globPtr = NULL;

	return 0;
}

