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

#include "Daemon.h"

#include <fcntl.h>
#include <stdlib.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/resource.h>

#include <stdexcept>

////////////////////////////////////////


namespace Daemon
{


////////////////////////////////////////


void
closeFileDescriptors( int startFD )
{
	// set folder to root so we aren't on any random mount points if
	// someone wants to remount
	int err = chdir( "/" );
	if ( err < 0 )
		syslog( LOG_CRIT, "Unable to switch to root directory" );

	// set umask so people have control
	umask( 0 );

	struct rlimit rlim;
	err = getrlimit( RLIMIT_NOFILE, &rlim );
	if ( err < 0 )
	{
		syslog( LOG_CRIT, "Unable to get resource limit describing max number of files" );
		throw std::runtime_error( "Unable to retrieve resource limits" );
	}
	else
	{
		for ( int i = startFD, N = ( int )rlim.rlim_cur; i < N; ++i )
			close( i );
	}

	// re-open standard file pointers
	if ( startFD == 0 )
	{
		int fd0 = open( "/dev/null", O_RDWR );
		int fd1 = dup( fd0 );
		int fd2 = dup( fd0 );

		if ( fd0 != STDIN_FILENO or fd1 != STDOUT_FILENO or fd2 != STDERR_FILENO )
			_exit( -1 );
	}
}


////////////////////////////////////////


void
background( const std::function<void (void)> &waitStart )
{
	pid_t pid = fork();

	if ( pid < 0 )
	{
		syslog( LOG_CRIT, "Unable to fork process" );
	}
	else if ( pid != 0 )
	{
		// wait for child to beome active
		if ( waitStart )
			waitStart();

		// we're done and child is detached in theory
		exit( 0 );
	}

	// Become the session leader
	pid_t sid = setsid();
	if ( sid < 0 )
	{
		syslog( LOG_CRIT, "Unable to become session leader" );
	}
}


////////////////////////////////////////


} // namespace Daemon


////////////////////////////////////////



