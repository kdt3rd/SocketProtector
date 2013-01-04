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
#include <sys/stat.h>
#include <fcntl.h>

#include <semaphore.h>
#include <string>
#include <stdexcept>
#include <errno.h>


////////////////////////////////////////


class Semaphore
{
public:
	Semaphore( const std::string &name )
			: mySem( NULL )
	{
		mySem = sem_open( name.c_str(), O_CREAT | O_RDWR, S_IRWXU, 0 );
		if ( mySem == SEM_FAILED )
			throw std::runtime_error( "Unable to create semaphore" );
		if ( sem_unlink( name.c_str() ) != 0 )
			throw std::runtime_error( "Unable to unlink semaphore" );
	}

	~Semaphore( void )
	{
		sem_close( mySem );
	}

	void post( void )
	{
		if ( sem_post( mySem ) != 0 )
			throw std::runtime_error( "Unable to post semaphore" );
	}

	void wait( void )
	{
		while ( sem_wait( mySem ) != 0 )
		{
			if ( errno == EINTR )
				continue;
			throw std::runtime_error( "Error waiting for semaphore" );
		}
	}

private:
	sem_t *mySem;
};


