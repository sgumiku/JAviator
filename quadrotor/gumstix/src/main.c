/*
 * Copyright (c) Harald Roeck hroeck@cs.uni-salzburg.at
 * Copyright (c) Rainer Trummer rtrummer@cs.uni-salzburg.at
 *
 * University Salzburg, www.uni-salzburg.at
 * Department of Computer Science, cs.uni-salzburg.at
 */

/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

#include "protocol.h"
#include "transfer.h"
#include "comm_channel.h"
#include "serial_channel.h"
#include "socket_channel.h"
#include "javiator_port.h"
#include "terminal_port.h"
#include "ubisense_port.h"
#include "control_loop.h"

#define PERIOD_MULTIPLIER   1 /* communicate with terminal every period */
#define Z_AXIS_CONTROLLER   1 /* enable z-axis controller */
#define EXEC_CONTROL_LOOP   1 /* execute control loop */
#define ENABLE_UBISENSE     0 /* setup Ubisense socket */
#define SPI_DEVICE          "/dev/mem"
#define SPI_BAUDRATE        115200
#define SERIAL_DEVICE       "/dev/ttyS2"
#define SERIAL_BAUDRATE     115200
#define TERMINAL_PORT       7000
#define UBISENSE_ADDR       "192.168.1.3"
#define UBISENSE_PORT       9001
#define UBISENSE_TAG        20235 /* 21098 */

static comm_channel_t       javiator_channel;
static comm_channel_t       terminal_channel;
static comm_channel_t       ubisense_channel;


static int setup_javiator_port( char *device, int baudrate )
{
    if( serial_channel_create( &javiator_channel ) )
    {
        fprintf( stderr, "ERROR: unable to create serial channel\n" );
        return( -1 );
    }

    if( serial_channel_init( &javiator_channel, device, baudrate ) )
    {
        fprintf( stderr, "ERROR: cannot initialize serial channel\n" );
        return( -1 );
    }

    if( javiator_port_init( &javiator_channel ) )
    {
        fprintf( stderr, "ERROR: JAviator port not correctly initialized\n" );
        return( -1 );
    }

    return( 0 );
}

static int setup_terminal_port( int listen_port, int type, int multiplier )
{
    memset( &terminal_channel, 0, sizeof( terminal_channel ) );

    if( socket_channel_create( &terminal_channel, type ) )
    {
        fprintf( stderr, "ERROR: unable to create socket channel\n" );
        return( -1 );
    }

    if( socket_channel_init( &terminal_channel, type, NULL, listen_port ) )
    {
        fprintf( stderr, "ERROR: unable to initialize socket channel\n" );
        return( -1 );
    }

    if( terminal_port_init( &terminal_channel ) )
    {
        fprintf( stderr, "ERROR: terminal port not correctly initialized\n" );
        return( -1 );
    }

    terminal_port_set_multiplier( multiplier );
    return( 0 );
}

static int setup_ubisense_port( int type, char *addr, int port, int tag )
{
    memset( &ubisense_channel, 0, sizeof( ubisense_channel ) );

    if( socket_channel_create( &ubisense_channel, type ) )
    {
        fprintf( stderr, "ERROR: unable to create socket channel\n" );
        return( -1 );
    }

    if( socket_channel_init( &ubisense_channel, type, addr, port ) )
    {
        fprintf( stderr, "ERROR: unable to initialize socket channel\n" );
        return( -1 );
    }

    if( ubisense_port_init( &ubisense_channel, tag ) )
    {
        fprintf( stderr, "ERROR: Ubisense port not correctly initialized\n" );
        return( -1 );
    }

    return( 0 );
}

static void usage( char *binary )
{
    printf( "usage: %s [OPTIONS]\n"
            "OPTIONS are:\n"
            "\t -c      ... disable control loop\n"
            "\t -s      ... setup Ubisense port\n"
            "\t -h      ... print this message\n"
            "\t -m mult ... send data every <mult> period to terminal\n"
            "\t -t time ... controller period in milliseconds\n"
            "\t -u      ... use TCP socket instead of UDP socket\n"
            "\t -z      ... disable z-controller\n"
            , binary );
}

int main( int argc, char **argv )
{
    int period     = CONTROLLER_PERIOD;
    int multiplier = PERIOD_MULTIPLIER;
    int control_z  = Z_AXIS_CONTROLLER;
    int exec_loop  = EXEC_CONTROL_LOOP;
    int ubisense   = ENABLE_UBISENSE;
	int conn_type  = SOCK_UDP;
	int opt;

	while( (opt = getopt( argc, argv, "chm:st:uz" )) != -1 )
    {
		switch( opt )
		{
			case 'c':
				exec_loop = 0;
				break;

			case 'm':
				if( (multiplier = atoi( optarg )) < 1 )
                {
					fprintf( stderr, "ERROR: option '-m' requires a value > 0\n" );
					usage( argv[0] );
					exit( 1 );
				}
				break;

			case 's':
				ubisense = 1;
				break;

			case 't':
				if( (period = atoi( optarg )) < 1 )
                {
					fprintf( stderr, "ERROR: option '-t' requires a value > 0\n" );
					usage( argv[0] );
					exit( 1 );
				}
				break;

			case 'u':
				conn_type = SOCK_SERVER;
				break;

			case 'z':
				control_z = 0;
				break;

			case 'h':
			default:
				usage( argv[0] );
				exit( 1 );
		}
	}

    printf( "setting up JAviator port ... " );
    if( setup_javiator_port( SERIAL_DEVICE, SERIAL_BAUDRATE ) )
    {
        printf( "failed\n" );
        fprintf( stderr, "ERROR: could not setup the JAviator port\n" );
        exit( 1 );
    }
    printf( "ok\n" );

	printf( "setting up Terminal port ... " );
    if( setup_terminal_port( TERMINAL_PORT, conn_type, multiplier ) )
    {
        printf( "failed\n" );
        fprintf( stderr, "ERROR: could not setup the Terminal port\n" );
        exit( 1 );
    }
    printf( "ok\n" );

    if( ubisense )
    {
	    printf( "setting up Ubisense port ... " );
        if( setup_ubisense_port( SOCK_CLIENT,
            UBISENSE_ADDR, UBISENSE_PORT, UBISENSE_TAG ) )
        {
            printf( "failed\n" );
            fprintf( stderr, "ERROR: could not setup the Ubisense port\n" );
            exit( 1 );
        }
        printf( "ok\n" );
    }

    if( exec_loop )
    {
        printf( "setting up control loop\n" );
        control_loop_setup( period, control_z, ubisense );
        printf( "starting control loop\n" );
        control_loop_run( );
    }

    return( 0 );
}

/* End of file */
