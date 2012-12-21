//
// Copyright (c) 2012 Reliance MediaWorks. All Rights Reserved.
// This software is proprietary to Reliance MediaWorks, and is not to
// be reproduced, transmitted, or disclosed in any way without written
// permission.
//


#include "PID.h"

#include "Semaphore.h"
#include "Daemon.h"
#include "SocketServer.h"

#include <syslog.h>
#include <iostream>
#include <vector>
#include <stdlib.h>
#include <sys/types.h>
#include <pwd.h>


////////////////////////////////////////


namespace {

std::string
fixPath( const std::string &cmd )
{
	std::string retval;

	if ( cmd[0] == '~' )
	{
		long bufV = sysconf( _SC_GETPW_R_SIZE_MAX );
		if ( bufV == -1 )
			bufV = 4096;

		size_t bufSz = static_cast<size_t>( bufV );
		char tmpBuf[bufSz];
		struct passwd pwd, *result = NULL;
		if ( getpwuid_r( getuid(), &pwd, tmpBuf, bufSz, &result ) != 0 || result == NULL )
		{
			const char *h = getenv( "HOME" );
			if ( ! h )
				return cmd;

			retval = std::string( h ) + '/' + cmd;
		}
		else
			retval = std::string( pwd.pw_dir ) + '/' + cmd;
	}
	else
	{
		char *cd = getcwd( NULL, 0 );
		if ( cd )
		{
			retval = std::string( cd ) + '/' + cmd;
			free( cd );
		}
		else
			retval = cmd;
	}

	if ( access( retval.c_str(), X_OK ) == -1 )
	{
		syslog( LOG_DEBUG, "Assuming command is in path" );
		return cmd;
	}
	else
	{
		syslog( LOG_DEBUG, "Transformed relative path to '%s'. Specify full path if not correct", retval.c_str() );
	}
	return retval;
}


SocketServer *theSocketServer = NULL;


////////////////////////////////////////


void
handleTerminateSignal( int )
{
	if ( theSocketServer )
		theSocketServer->terminate();
}


////////////////////////////////////////


void
handleRespawnSignal( int )
{
	if ( theSocketServer )
		theSocketServer->respawn();
}


////////////////////////////////////////


void
handleChildEvent( int )
{
	if ( theSocketServer )
		theSocketServer->childEvent();
}
	

////////////////////////////////////////


void
setSignalHandlers( void )
{
	signal( SIGINT, &handleTerminateSignal );
	signal( SIGQUIT, &handleTerminateSignal );
	signal( SIGTERM, &handleTerminateSignal );
	signal( SIGHUP, &handleRespawnSignal );
	signal( SIGCHLD, &handleChildEvent );
}


////////////////////////////////////////


void
usageAndExit( const char *argv0, const char *errorMsg, int exitStatus )
#ifdef __clang__
	[[noreturn]]
#endif
{
	if ( errorMsg != NULL )
		syslog( LOG_ERR, "ERROR: %s", errorMsg );

	std::cerr << "Usage: " << argv0
			  <<
		" [-h|--help] [-f|--foreground] [-v|--verbose] [--pid-file filename] portnum -- <daemon command> [daemon arguments...]\n"
		"\n  --help:       This message"
		"\n  --foreground: Run the daemon in foreground (default: false)"
		"\n  --verbose:     Enables more verbose syslog messages (default: false)"
			  << std::endl;

	exit( exitStatus );
}


////////////////////////////////////////


} // empty namespace


////////////////////////////////////////


int
main( int argc, char *argv[] )
{
	signal( SIGPIPE, SIG_IGN );

	std::string pipeFile = "/var/run/SomeUniqueName.sock";
	std::string pidFile;// = "/var/run/SocketForwarder.pid";
	int port = -1;
	bool isVerbose = false;
	bool foregroundDaemon = false;

	openlog( "socket_protector", LOG_PID | LOG_NOWAIT | LOG_CONS | LOG_PERROR, LOG_DAEMON );

	std::vector<std::string> subCommand;
	for ( int a = 1; a < argc; ++a )
	{
		std::string curarg = argv[a];
		if ( curarg == "-v" || curarg == "-verbose" || curarg == "--verbose" )
			isVerbose = true;
		else if ( curarg == "-f" || curarg == "-foreground" || curarg == "--foreground" )
			foregroundDaemon = true;
		else if ( curarg == "-?" || curarg == "-h" || curarg == "-help" || curarg == "--help" )
		{
			usageAndExit( argv[0], NULL, 0 );
		}
		else if ( curarg == "-pid-file" || curarg == "--pid-file" )
		{
			++a;
			if ( a == argc )
				usageAndExit( argv[0], "Invalid arguments", -1 );

			pidFile = argv[a];
		}
		else if ( curarg == "--" )
		{
			for ( ++a; a < argc; ++a )
				subCommand.push_back( std::string( argv[a] ) );

			if ( subCommand.empty() )
				usageAndExit( argv[0], "Missing sub-daemon arguments", -1 );

			if ( subCommand[0][0] != '/' )
			{
				// if stuff isn't in the execution path, like a relative
				// path, execvp will fail. Expand it out to a full path
				subCommand[0] = fixPath( subCommand[0] );
			}
			break;
		}
		else if ( curarg[0] == '-' )
		{
			usageAndExit( argv[0], "Unknown command line argument", -1 );
		}
		else if ( port == -1 )
		{
			long tmp = strtol( curarg.data(), NULL, 10 );
			if ( tmp <= 0 || tmp > 65535 )
				usageAndExit( argv[0], "Invalid port specification", -1 );
			port = static_cast<int>( tmp );
		}
		else
			usageAndExit( argv[0], "Invalid arguments", -1 );
	}

	if ( port == -1 )
	{
		usageAndExit( argv[0], "Missing port argument", -1 );
	}

	if ( port < 1024 && geteuid() != 0 )
	{
		syslog( LOG_ERR, "Attempt to use privileged port can only be done running as root" );
		return -1;
	}

	// We need to wait for our child process from the backgrounding
	// process to be up and running or it's hard to see what is
	// wrong. so set up a named semaphore to wait on that in the
	// main thread, although we only need to do that in the case where
	// we are going to detach
	std::unique_ptr<Semaphore> startup;
	std::unique_ptr<SocketServer> servPtr;

	try
	{
		if ( ! foregroundDaemon )
		{
			std::stringstream name;
			name << "SocketProtector" << getpid();
			startup.reset( new Semaphore( name.str() ) );

			Daemon::background( std::bind( &Semaphore::wait, startup.get() ) );
			Daemon::closeFileDescriptors();
		}
		else
		{
			Daemon::closeFileDescriptors( 3 );
		}

		// Daemonization causes all open files to be closed, so we have to
		// restart syslogging afterwards
		openlog( "safe_daemon", LOG_PID | LOG_NOWAIT | LOG_CONS | LOG_PERROR, LOG_DAEMON );

		int p = LOG_INFO;
		if ( isVerbose )
			p = LOG_DEBUG;
		( void )setlogmask( LOG_UPTO( p ) );

		PID pf( pidFile );

		syslog( LOG_NOTICE, "pid %d starting...", getpid() );

		setSignalHandlers();

		servPtr.reset( new SocketServer( subCommand, static_cast<uint16_t>( port ) ) );
		theSocketServer = servPtr.get();

		// ok, we're at a point where we are going to run, so
		// let the parent process know so it can continue allowing us
		// to detach...
		if ( startup )
		{
			startup->post();
			startup.reset();
		}

		syslog( LOG_DEBUG, "socket server starting..." );

		theSocketServer->run();

		syslog( LOG_DEBUG, "shutdown requested..." );

		servPtr.reset();
		theSocketServer = NULL;
	}
	catch ( std::exception &e )
	{
		syslog( LOG_CRIT, "Unhandled exception: %s", e.what() );
	}
	catch ( ... )
	{
		syslog( LOG_CRIT, "Unhandled unknown exception" );
	}

	// failsafe in case we got an exception...
	if ( startup )
		startup->post();

	return 0;
}



