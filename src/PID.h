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
#include <string>
#include <sstream>
#include <fstream>
#include <signal.h>
#include <unistd.h>
#include <syslog.h>
#include <stdexcept>
#include <errno.h>


////////////////////////////////////////


/// RAII cleanup of the PID file
class PID
{
public:
	PID( const std::string &path )
	{
		if ( ! path.empty() )
		{
			std::ifstream curpidf( path.c_str() );
			pid_t curpid = 0;
			if ( curpidf )
			{
				curpidf >> curpid;
				if ( curpid != 0 )
				{
					if ( kill( curpid, 0 ) == 0 || errno != ESRCH )
					{
						std::stringstream msg;
						msg << "Application already running for pid file '" << path << "', PID " << curpid;
						syslog( LOG_ERR, "%s", msg.str().c_str() );
						throw std::runtime_error( msg.str() );
					}
				}
			}
			curpidf.close();

			std::ofstream pidf( path.c_str(), std::ofstream::trunc );
			pidf << getpid() << std::endl;
			myPath = path;
		}
	}

	~PID( void )
	{
		close();
	}

	void close( void )
	{
		if ( ! myPath.empty() )
			::unlink( myPath.c_str() );
		myPath.clear();
	}

private:
	std::string myPath;
};


