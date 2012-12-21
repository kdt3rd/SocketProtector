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

#include "SocketServer.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>
#include <errno.h>
#include <stdexcept>

#include "Daemon.h"

#include <iostream>


////////////////////////////////////////


SocketServer::SocketServer( const std::vector<std::string> &subDaemonCommands, uint16_t port )
		: myTCPSocket( -1 ), myUnixSocket( -1 ), myCmdLine( subDaemonCommands ), myConnectedDaemon( -1 ), myLastPID( -1 ), myRespawnCount( 0 ), myTCPPort( port ), myTerminated( false )
{
	myTriggerPipe[0] = -1;
	myTriggerPipe[1] = -1;

	if ( ::pipe( myTriggerPipe ) < 0 )
	{
		myTriggerPipe[0] = -1;
		myTriggerPipe[1] = -1;
		throw std::runtime_error( "Unable to initialize pipe for controlling run loop" );
	}
}


////////////////////////////////////////


SocketServer::~SocketServer( void )
{
	if ( myTCPSocket >= 0 )
		::close( myTCPSocket );
	myTCPSocket = -1;

	if ( myTriggerPipe[0] >= 0 )
		::close( myTriggerPipe[0] );
	if ( myTriggerPipe[1] >= 0 )
		::close( myTriggerPipe[1] );
	myTriggerPipe[0] = -1;
	myTriggerPipe[1] = -1;
}


////////////////////////////////////////


void
SocketServer::terminate( void )
{
	if ( ! myTerminated )
	{
		char b = 'x';
		if ( write( myTriggerPipe[1], &b, sizeof(char) ) != 1 )
		{
			syslog( LOG_ERR, "Unable to write to internal communication pipe" );
		}
	}
}


////////////////////////////////////////


void
SocketServer::respawn( void )
{
	if ( ! myTerminated )
	{
		char b = 'r';
		if ( write( myTriggerPipe[1], &b, sizeof(char) ) != 1 )
		{
			syslog( LOG_ERR, "Unable to write to internal communication pipe" );
		}
	}
}


////////////////////////////////////////


void
SocketServer::childEvent( void )
{
	if ( ! myTerminated )
	{
		char b = 'c';
		if ( write( myTriggerPipe[1], &b, sizeof(char) ) != 1 )
		{
			syslog( LOG_ERR, "Unable to write to internal communication pipe" );
		}
	}
}


////////////////////////////////////////


void
SocketServer::run( int retryCount, int retryPauseSec, int backlogSize )
{
	if ( myTCPSocket != -1 )
		throw std::runtime_error( "TCP Socket server already appears to be running" );

	try
	{
		prepareTCPSocket( backlogSize );
	
		myRespawnCount = 0;
		respawnChild();

		do
		{
			drainSockets();

			int fd = getNextSocket();

			if ( fd >= 0 )
			{
				if ( sendSocket( fd ) )
					continue;

				mySendFDs.push_back( fd );
				if ( myConnectedDaemon == -1 )
				{
					struct timeval curwaittime;
					if ( gettimeofday( &curwaittime, NULL ) != 0 )
					{
						syslog( LOG_CRIT, "Unable to retrieve time of day: %s", strerror( errno ) );
						continue;
					}

					if ( ( curwaittime.tv_sec - myLastStartTime.tv_sec ) > retryPauseSec )
					{
						syslog( LOG_NOTICE, "Child didn't respond after %d seconds, restarting", retryPauseSec );
						if ( myRespawnCount > retryCount )
						{
							syslog( LOG_CRIT, "Child process didn't connect after %d retries, terminating", myRespawnCount );
							myTerminated = true;
							break;
						}
						respawnChild();
					}
				}
				else
				{
					myRespawnCount = 0;
				}
			}
			else
			{
				// we were terminated, break out
				syslog( LOG_INFO, "Terminate request received, forwarder stopping" );
				break;
			}
		} while ( true );
	}
	catch ( const std::exception &e )
	{
		syslog( LOG_CRIT, "Unhandled exception, terminating: %s", e.what() );
	}
	catch ( ... )
	{
		syslog( LOG_CRIT, "Unknown exception, terminating" );
	}

	closeHandles();

	myTerminated = true;
	myLastPID = -1;
	if ( ! myChildList.empty() )
	{
		for ( auto i = myChildList.begin(); i != myChildList.end(); ++i )
			kill( (*i), SIGTERM );
	}

}


////////////////////////////////////////


void
SocketServer::drainSockets( void )
{
	if ( myConnectedDaemon == -1 )
		return;

	while ( ! mySendFDs.empty() )
	{
		int fd = mySendFDs.front();
		if ( sendSocket( fd ) )
		{
			mySendFDs.pop_front();
		}
		else
		{
			syslog( LOG_ERR, "Lost child process or couldn't send socket, respawning: %s", strerror( errno ) );
			respawnChild();
			return;
		}
	}
}


////////////////////////////////////////


void
SocketServer::acceptChild( void )
{
	do
	{
		myConnectedDaemon = accept( myUnixSocket, NULL, NULL );
		if ( myConnectedDaemon == -1 )
		{
			if ( errno == EINTR )
				continue;
			syslog( LOG_ERR, "Error accepting child socket, restarting child" );
			respawnChild();
			return;
		}
	} while ( false );

	close( myUnixSocket );
	myUnixSocket = -1;
#ifndef __linux__
	if ( unlink( myUnixSockAddr.sun_path ) == -1 )
	{
		if ( errno != ENOENT )
			throw std::runtime_error( strerror( errno ) );
	}
#endif

	while ( ! mySendFDs.empty() )
	{
		int fd = mySendFDs.front();
		if ( sendSocket( fd ) )
		{
			mySendFDs.pop_front();
			continue;
		}
		
		syslog( LOG_INFO, "Failed to send after initial child connection" );
		break;
	}
}


////////////////////////////////////////


bool
SocketServer::sendSocket( int fd )
{
	if ( myConnectedDaemon == -1 )
		return false;

	struct msghdr msg;
	char ccmsg[CMSG_SPACE(sizeof(fd))];
	struct cmsghdr *cmsg;

	// Apparently you have to at least send 1 byte...
	char buf[1];
	buf[0] = 'x';
	struct iovec vec;
	vec.iov_base = buf;
	vec.iov_len = 1;

	msg.msg_name = (struct sockaddr*)&myUnixSockAddr;
	msg.msg_namelen = sizeof(myUnixSockAddr);
	msg.msg_iov = &vec;
	msg.msg_iovlen = 1;
	msg.msg_control = ccmsg;
	msg.msg_controllen = static_cast<socklen_t>( sizeof(ccmsg) );

	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	cmsg->cmsg_len = CMSG_LEN(sizeof(fd));
	memcpy( CMSG_DATA(cmsg), &fd, sizeof(fd) );

	msg.msg_controllen = cmsg->cmsg_len;
	msg.msg_flags = 0;

	if ( sendmsg( myConnectedDaemon, &msg, 0 ) != -1 )
	{
		close( fd );
		return true;
	}

	syslog( LOG_DEBUG, "Failed to send fd %d to child %d", fd, myLastPID );
	return false;
}


////////////////////////////////////////


void
SocketServer::closeHandles( void )
{
	if ( myConnectedDaemon != -1 )
	{
		close( myConnectedDaemon );
		myConnectedDaemon = -1;
	}

	if ( myUnixSocket != -1 )
	{
		close( myUnixSocket );
		myUnixSocket = -1;
	}

	if ( myTCPSocket != -1 )
	{
		close( myTCPSocket );
		myTCPSocket = -1;
	}

	while ( ! mySendFDs.empty() )
	{
		int fd = mySendFDs.front();
		close( fd );
		mySendFDs.pop_front();
	}
}


////////////////////////////////////////


int
SocketServer::getNextSocket( void )
{
	if ( waitForEvent() )
	{
		do
		{
			int fd = accept( myTCPSocket, NULL, NULL );

			if ( fd == -1 )
			{
				syslog( LOG_DEBUG, "Socket accept returned an error: (%d) '%s'", errno, strerror( errno ) );
				// Reading accept (2), linux passes already-pending network errors on the new socket
				// via accept. for reliability, treat these as EAGAIN and retry...
				switch ( errno )
				{
					case ENETDOWN:
					case EPROTO:
					case ENOPROTOOPT:
					case EHOSTDOWN:
#ifdef ENONET
					case ENONET:
#endif
					case EHOSTUNREACH:
					case EOPNOTSUPP:
					case ENETUNREACH:
					case EAGAIN:
						continue;

					case EINTR:
						continue;

					default:
						syslog( LOG_CRIT, "Received unknown / unhandled error accepting connection on TCP socket: (%d) %s", errno, strerror(errno) );
				}
			}

			return fd;
		} while ( true );
	}

	return -1;
}


////////////////////////////////////////


bool
SocketServer::waitForEvent( void )
{

	do
	{
		if ( myTerminated )
			break;

		drainSockets();

		fd_set fds;
		FD_ZERO( &fds );
		FD_SET( myTriggerPipe[0], &fds );
		FD_SET( myTCPSocket, &fds );
		int fdmax = std::max( myTriggerPipe[0], myTCPSocket );
		if ( myUnixSocket != -1 )
		{
			FD_SET( myUnixSocket, &fds );
			fdmax = std::max( std::max( myTriggerPipe[0], myTCPSocket ), myUnixSocket );
		}

		int rv = select( fdmax + 1, &fds, NULL, NULL, NULL );
		if ( rv == -1 )
		{
			if ( errno == EINTR )
				continue;

			syslog( LOG_ERR, "Error trying to wait for activity: %s", strerror( errno ) );
			throw std::runtime_error( "select error" );
		}

		if ( myUnixSocket != -1 && FD_ISSET( myUnixSocket, &fds ) )
		{
			try
			{
				acceptChild();
			}
			catch ( const std::exception &e )
			{
				if ( myConnectedDaemon != -1 )
				{
					close( myConnectedDaemon );
					myConnectedDaemon = -1;
				}
				syslog( LOG_ERR, "error accepting child process: %s", e.what() );
			}
		}

		if ( FD_ISSET( myTriggerPipe[0], &fds ) )
		{
			char b = '\0';
			if ( read( myTriggerPipe[0], &b, sizeof(char) ) < 1 )
			{
				syslog( LOG_CRIT, "Attempting to read from internal communication pipe, but failed, terminating" );
				myTerminated = true;
			}

			syslog( LOG_DEBUG, "Got notification byte '%c' on communication pipe", b );
			switch ( b )
			{
				case 'x':
					myTerminated = true;
					break;
				case 'r':
					respawnChild();
					break;
				case 'c':
					handleChildEvent();
					break;

				default:
					syslog( LOG_DEBUG, "Got weird byte on communication pipe" );
					break;
			}
		}

		if ( myTerminated )
		{
			syslog( LOG_DEBUG, "terminate flag has been set..." );
			break;
		}

		// NB: EXIT POINT
		if ( FD_ISSET( myTCPSocket, &fds ) )
			return true;

	} while ( true );

	return false;
}


////////////////////////////////////////


void
SocketServer::respawnChild( void )
{
	syslog( LOG_NOTICE, "Respawning child process..." );
	restartUnixSocket();

	if ( ! myArgv )
	{
		size_t N = myCmdLine.size();
		myArgv.reset( new char *[N + 1] );
		char **data = myArgv.get();
		for ( size_t i = 0; i != N; ++i )
			data[i] = const_cast<char *>( myCmdLine[i].c_str() );
		myArgv[N] = nullptr;
	}

	myLastPID = -1;
	pid_t pid = fork();

	if ( pid < 0 )
	{
		syslog( LOG_CRIT, "Unable to fork child process" );
		throw std::runtime_error( "Unable to fork child process" );
	}

	if ( pid == 0 )
	{
		//child process, exec off the command
		// first close any extra open descriptors
		Daemon::closeFileDescriptors( 3 );
		execvp( myArgv[0], myArgv.get() );
		_exit( -1 );
	}

	myLastPID = pid;
	myChildList.push_back( pid );
	if ( gettimeofday( &myLastStartTime, NULL ) != 0 )
	{
		myLastStartTime.tv_sec = 0;
		myLastStartTime.tv_usec = 0;
		syslog( LOG_CRIT, "Unable to retrieve time of day: %s", strerror( errno ) );
	}

	++myRespawnCount;
}


////////////////////////////////////////


void
SocketServer::handleChildEvent( void )
{
	int status = 0;
	pid_t cpid = waitpid( -1, &status, WNOHANG );

	if ( cpid <= 0 )
		return;

	if ( WIFEXITED( status ) )
		syslog( LOG_INFO, "child process %d exited with status %d", cpid, WEXITSTATUS( status ) );
	else if ( WIFSIGNALED( status ) )
		syslog( LOG_INFO, "child process %d terminated due to signal %d", cpid, WTERMSIG( status ) );
	else if ( WIFSTOPPED( status ) )
	{
		syslog( LOG_DEBUG, "child process %d stopped due to signal %d", cpid, WSTOPSIG( status ) );
		return;
	}

	for ( auto i = myChildList.begin(); i != myChildList.end(); ++i )
	{
		if ( (*i) == cpid )
		{
			myChildList.erase( i );
			break;
		}
	}

	if ( cpid == myLastPID )
	{
		if ( myConnectedDaemon >= 0 )
		{
			close( myConnectedDaemon );
			myConnectedDaemon = -1;
		}
		myLastPID = -1;
	}
}


////////////////////////////////////////


void
SocketServer::restartUnixSocket( void )
{
	if ( myConnectedDaemon >= 0 )
	{
		close( myConnectedDaemon );
		myConnectedDaemon = -1;
	}

	if ( myUnixSocket != -1 )
	{
		close( myUnixSocket );
		myUnixSocket = -1;
	}

	myUnixSocket = socket( PF_LOCAL, SOCK_STREAM, 0 );
	if ( myUnixSocket < 0 )
		throw std::runtime_error( "Unable to create UNIX socket" );

	memset( &myUnixSockAddr, 0, sizeof(myUnixSockAddr) );
	myUnixSockAddr.sun_family = PF_UNIX;

#ifdef __linux__
	// linux has abstract sockets that don't need unlinking
	std::string pName = "sock_srv_" + std::to_string( myTCPPort );
	strncpy( myUnixSockAddr.sun_path + 1, pName.c_str(), std::min( pName.size(), sizeof(myUnixSockAddr.sun_path) - 2 ) );
#else
	std::string pName = "/tmp/sock_srv_" + std::to_string( myTCPPort );
	strncpy( myUnixSockAddr.sun_path, pName.c_str(), std::min( pName.size(), sizeof(myUnixSockAddr.sun_path) - 1 ) );
	if ( unlink( myUnixSockAddr.sun_path ) == -1 )
	{
		if ( errno != ENOENT )
			throw std::runtime_error( "unable to remove socket path" );
	}
#endif

#ifdef __APPLE__
	myUnixSockAddr.sun_len = static_cast<decltype(myUnixSockAddr.sun_len)>( SUN_LEN( &myUnixSockAddr ) );
#endif

	if ( bind( myUnixSocket, (struct sockaddr *)&myUnixSockAddr, sizeof(myUnixSockAddr) ) == -1 )
		throw std::runtime_error( std::string( "Unable to bind local unix socket: " ) + strerror( errno ) );

	if ( listen( myUnixSocket, 1 ) == -1 )
		throw std::runtime_error( "Unable to start listening on a socket" );
}


////////////////////////////////////////


void
SocketServer::prepareTCPSocket( int backlog )
{
	myTCPSocket = socket( AF_INET, SOCK_STREAM, 0 );
	if ( myTCPSocket < 0 )
	{
		syslog( LOG_ERR, "Unable to create AF_INET socket: %s", strerror( errno ) );
		throw std::runtime_error( "error creating socket" );
	}

	int on = 1;
	if ( setsockopt( myTCPSocket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on) ) < 0 )
	{
		syslog( LOG_ERR, "Unable to set SO_REUSEADDR on TCP socket: %s", strerror( errno ) );
		throw std::runtime_error( "error setting socket option" );
	}

	if ( setsockopt( myTCPSocket, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on) ) < 0 )
	{
		syslog( LOG_ERR, "Unable to set SO_KEEPALIVE: %s", strerror( errno ) );
		throw std::runtime_error( "error setting socket option" );
	}

#ifndef __APPLE__
	if ( setsockopt( myTCPSocket, IPPROTO_TCP, TCP_CORK, &on, sizeof(on) ) < 0 )
	{
		syslog( LOG_ERR, "Unable to set TCP_CORK: %s", strerror( errno ) );
		throw std::runtime_error( "error setting socket option" );
	}
#endif

	if ( setsockopt( myTCPSocket, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on) ) < 0 )
	{
		syslog( LOG_ERR, "Unable to set TCP_NODELAY: %s", strerror( errno ) );
		throw std::runtime_error( "error setting socket option" );
	}

	int low_delay = IPTOS_LOWDELAY;
	if ( setsockopt( myTCPSocket, IPPROTO_IP, IP_TOS, &low_delay, sizeof(low_delay) ) < 0 )
	{
		syslog( LOG_ERR, "Unable to set IP_TOS IPTOS_LOWDELAY: %s", strerror( errno ) );
		throw std::runtime_error( "error setting socket option" );
	}

	struct sockaddr_in local;
	memset( &local, 0, sizeof(local) );
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = htonl( INADDR_ANY );
	local.sin_port = htons( myTCPPort );
	
	if ( bind( myTCPSocket, (struct sockaddr *)&local, sizeof(local) ) == -1 )
	{
		syslog( LOG_ERR, "Unable to bind to the socket: %s", strerror( errno ) );
		throw std::runtime_error( "error binding to socket" );
	}

	if ( listen( myTCPSocket, backlog ) )
	{
		syslog( LOG_ERR, "Unable to listen the socket: %s", strerror( errno ) );
		throw std::runtime_error( "error listening on socket" );
	}
}


////////////////////////////////////////


