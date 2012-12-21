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

#pragma once

#include <sys/types.h>
#include <vector>
#include <string>
#include <deque>
#include <memory>
#include <sys/un.h>


////////////////////////////////////////


class SocketServer
{
public:
	SocketServer( const std::vector<std::string> &cmdargs, uint16_t port );
	~SocketServer( void );

	/// Meant to be called from a signal handler or other thread, cancels
	/// any internal waiting happening
	/// terminate is async signal safe (SIGINT, SIGTERM, et al.)
	void terminate( void );

	/// Meant to be called from a signal handler or other thread, interrupts
	/// any internal waiting happening
	/// respawn is async signal safe (SIGHUP)
	void respawn( void );

	/// Meant to be called from a signal handler, interrupts
	/// any internal waiting happening (SIGCHLD). we only
	/// really handle this so we can syslog appropriately
	void childEvent( void );

	/// Runs forever (until terminate is called async then returns
	/// shortly after)
	///
	/// @param retryCount Number of attempts to retry starting the app before
	///                   failing ourselves. This count resets once a forwarding
	///                   connection is made
	/// @param retryPauseSec Number of seconds to pause between retry attempts
	/// @param backlogSize Maximum number of connections to accept and hold
	///                    in reserve while waiting for background to initialize
	///                    -1 (default) indicates unbounded
	void run( int retryCount = 3, int retryPauseSec = 60, int backlogSize = -1 );

private:
	void drainSockets( void );
	void acceptChild( void );
	bool sendSocket( int fd );
	void closeHandles( void );

	int getNextSocket( void );
	bool waitForEvent( void );

	void respawnChild( void );
	void handleChildEvent( void );

	void restartUnixSocket( void );
	void prepareTCPSocket( int backlog );

	int myTCPSocket;
	int myTriggerPipe[2];
	int myUnixSocket;

	std::vector<std::string> myCmdLine;
	std::unique_ptr<char *[]> myArgv;
	int myConnectedDaemon;

	pid_t myLastPID;
	std::vector<pid_t> myChildList;
	std::deque<int> mySendFDs;
	struct timeval myLastStartTime;
	int myRespawnCount;

	struct sockaddr_un myUnixSockAddr;
	uint16_t myTCPPort;
	bool myTerminated;
};


////////////////////////////////////////


