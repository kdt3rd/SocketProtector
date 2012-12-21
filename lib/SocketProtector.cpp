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

#include "SocketProtector.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <syslog.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdexcept>
#include <memory>
#include <string>


////////////////////////////////////////


namespace
{

struct SocketProtectorImpl
{
	int myServerConnection;
	int myTermPipe[2];
	bool myTerminated;
	char __padding[3];

	SocketProtectorImpl( uint16_t port )
			: myServerConnection( -1 ), myTerminated( false )
	{
		myTermPipe[0] = -1;
		myTermPipe[1] = -1;

		if ( pipe( myTermPipe ) < 0 )
		{
			myTermPipe[0] = -1;
			myTermPipe[1] = -1;
			throw std::runtime_error( strerror( errno ) );
		}

		myServerConnection = socket( PF_LOCAL, SOCK_STREAM, 0 );
		if ( myServerConnection < 0 )
			throw std::runtime_error( strerror( errno ) );

		struct sockaddr_un local;
		memset( &local, 0, sizeof(local) );
		local.sun_family = PF_UNIX;

#ifdef __linux__
		// linux has abstract sockets that don't need unlinking
		std::string pName = "sock_srv_" + std::to_string( port );
		strncpy( local.sun_path + 1, pName.c_str(), std::min( pName.size(), sizeof(local.sun_path) - 2 ) );
#else
		std::string pName = "/var/run/sock_srv_" + std::to_string( port );
		strncpy( local.sun_path, pName.c_str(), std::min( pName.size(), sizeof(local.sun_path) - 1 ) );
#endif
#ifdef __APPLE__
		local.sun_len = static_cast<decltype(local.sun_len)>( SUN_LEN( &local ) );
#endif

		do
		{
			if ( connect( myServerConnection, (struct sockaddr *)&local, sizeof(local) ) == -1 )
			{
				if ( errno == EINTR )
					continue;

				throw std::runtime_error( strerror( errno ) );
			}
		} while ( false );
	}

	~SocketProtectorImpl( void )
	{
		if ( myTermPipe[0] != -1 )
			close( myTermPipe[0] );
		if ( myTermPipe[1] != -1 )
			close( myTermPipe[1] );
		if ( myServerConnection != -1 )
			close( myServerConnection );
	}
	
	void terminate( void )
	{
		if ( myTermPipe[1] != -1 )
		{
			myTerminated = true;
			char b = 'x';
			if ( write( myTermPipe[1], &b, sizeof(char) ) != 1 )
			{
				syslog( LOG_ERR, "Unable to signal accept function to terminate" );
			}
		}
	}

	bool isTerminated( void ) const { return myTerminated; }

	int accept( void )
	{
		if ( myServerConnection == -1 )
			return -1;

		fd_set fds;
		FD_ZERO( &fds );
		FD_SET( myTermPipe[0], &fds );
		FD_SET( myServerConnection, &fds );

		int fdmax = std::max( myTermPipe[0], myServerConnection );

		do
		{
			int rv = ::select( fdmax + 1, &fds, NULL, NULL, NULL );

			if ( rv == -1 )
			{
				if ( errno == EINTR )
					continue;

				myTerminated = true;
				syslog( LOG_NOTICE, "waiting on connection failed, terminate requested" );
				return -1;
			}

			if ( FD_ISSET( myTermPipe[0], &fds ) )
			{
				char b = '\0';
				if ( read( myTermPipe[0], &b, sizeof(char) ) == 1 )
				{
					// don't actually care what's in the pipe, we're done
					break;
				}
				else if ( errno == EINTR )
				{
					continue;
				}
				else
				{
					syslog( LOG_ERR, "error communicating on internal terminate channel: %s", strerror( errno ) );
					break;
				}
			}

			if ( FD_ISSET( myServerConnection, &fds ) )
				return getSocket();

			if ( myTerminated )
				break;
		}
		while ( true );
		return -1;
	}

	int
	getSocket( void )
	{
		struct msghdr msg;
		char ccmsg[CMSG_SPACE(sizeof(int))];

		struct iovec iov;
		char buf[1];
		iov.iov_base = buf;
		iov.iov_len = 1;

		msg.msg_name = 0;
		msg.msg_namelen = 0;
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;
		msg.msg_control = ccmsg;
		msg.msg_controllen = static_cast<socklen_t>( sizeof(ccmsg) );

		do
		{
			ssize_t retval = recvmsg( myServerConnection, &msg, 0 );
			if ( retval == -1 )
			{
				if ( errno == EINTR )
					continue;

				if ( errno == ECONNRESET || errno == ENOTCONN )
				{
					syslog( LOG_NOTICE, "remote server disconnected, terminating" );
					return -1;
				}

				syslog( LOG_ERR, "unhandled error attempting to receive a socket: %s", strerror( errno ) );
				return -1;
			}
		} while ( false );
			
		struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
		if ( cmsg == NULL )
		{
			syslog( LOG_NOTICE, "empty message from server, terminating" );
			return -1;
		}
		if ( cmsg->cmsg_type != SCM_RIGHTS )
		{
			syslog( LOG_ERR, "Unknown control message type %d", cmsg->cmsg_type );
			return -1;
		}

		return *( reinterpret_cast<int *>( CMSG_DATA( cmsg ) ) );
	}
};

} // empty namespace


////////////////////////////////////////


PrivSocketProtector *
socket_protector_create( uint16_t serverport )
{
	try
	{
		std::unique_ptr<SocketProtectorImpl> tmp( new SocketProtectorImpl( serverport ) );
		return reinterpret_cast<PrivSocketProtector *>( tmp.release() );
	}
	catch ( const std::exception &e )
	{
		syslog( LOG_ERR, "error creating socket protector: %s", e.what() );
	}
	catch ( ... )
	{
		syslog( LOG_ERR, "UNNKNOWN error creating socket protector" );
	}

	return NULL;
}


////////////////////////////////////////


void
socket_protector_destroy( PrivSocketProtector *ptr )
{
	SocketProtectorImpl *rptr = reinterpret_cast<SocketProtectorImpl *>( ptr );
	delete rptr;
}


////////////////////////////////////////


void
socket_protector_terminate( PrivSocketProtector *ptr )
{
	if ( ptr )
	{
		SocketProtectorImpl *rptr = reinterpret_cast<SocketProtectorImpl *>( ptr );
		rptr->terminate();
	}
}


////////////////////////////////////////


bool
socket_protector_is_terminated( PrivSocketProtector *ptr )
{
	if ( ptr )
	{
		SocketProtectorImpl *rptr = reinterpret_cast<SocketProtectorImpl *>( ptr );
		return rptr->isTerminated();
	}

	return true;
}


////////////////////////////////////////


int
socket_protector_accept( PrivSocketProtector *ptr )
{
	if ( ptr )
	{
		SocketProtectorImpl *rptr = reinterpret_cast<SocketProtectorImpl *>( ptr );
		if ( rptr->isTerminated() )
		{
			syslog( LOG_ERR, "attempt to accept on a terminated socket listener" );
			return -1;
		}

		return rptr->accept();
	}

	return -1;
}


////////////////////////////////////////


