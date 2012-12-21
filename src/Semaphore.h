//

#pragma once

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


