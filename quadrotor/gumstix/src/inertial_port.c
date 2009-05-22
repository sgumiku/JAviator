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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "shared/protocol.h"
#include "controller.h"
#include "serial_channel.h"
#include "inertial_port.h"
#include "inertial_data.h"
#include "us_timer.h"

#define CMD_CONFIRMATION    0x00    /* null-byte confirmation */
#define CMD_CONTINUOUSLY    0x10    /* command for continuous mode */
#define CMD_DESIRED_DATA    0x31    /* command for desired data */

static int                  inertial_local = 0;
static int                  _started;
static int                  _automatic;
static inertial_data_t      inertial_data;
static comm_channel_t *     comm_channel;
static char                 comm_buf[ DM3_GX1_DATA_SIZE ];
static volatile int         new_data;

typedef enum
{
    st_TYPE = 1,
    st_PAYLOAD,
    st_COMPLETE,

} comm_state_t;


static inline int parse_inertial_data( void )
{
    int res = inertial_data_from_stream( &inertial_data,
        comm_buf + 1, DM3_GX1_DATA_SIZE - 1 ); /* skip header byte */

    //new_data = 1;

    return( res );
}

static inline int is_valid_data( const uint8_t *data, int size )
{
    uint16_t checksum = ( (data[ size-2 ] << 8) | (data[ size-1 ] & 0xFF) ) - (data[0] & 0xFF);

    size -= 3;

    while( size )
    {
        checksum -= (data[ size-1 ] << 8) | (data[ size ] & 0xFF);
        size -= 2;
    }

    return( checksum == 0 );
}

static int imu_recv_packet( void )
{
    static comm_state_t state = st_TYPE;
    static int items = 0;
    int retval = EAGAIN;

redo:
    switch( state )
    {
        case st_TYPE:
            retval = EAGAIN;
            items  = comm_channel->receive( comm_channel, comm_buf, 1 );
            if( items == 1 && comm_buf[0] == CMD_DESIRED_DATA )
            {
                state = st_PAYLOAD;
                goto redo;
            }
            break;

        case st_PAYLOAD:
            retval = EAGAIN;
            items += comm_channel->receive( comm_channel, comm_buf + items, DM3_GX1_DATA_SIZE - items );
            if( items == DM3_GX1_DATA_SIZE )
            {
                state = st_COMPLETE;
                goto redo;
            }
            break;

        case st_COMPLETE:
            if( is_valid_data( (uint8_t *) comm_buf, DM3_GX1_DATA_SIZE ) )
            {
                new_data = 1;
                retval = 0;
            }
            else
            {
                retval = -1;
            }
            state = st_TYPE;
            break;

        default:
            state = st_TYPE;
    }

    return( retval );
}

int inertial_port_init( comm_channel_t *channel, int automatic)
{
    memset( &inertial_data, 0, sizeof( inertial_data ) );

    comm_channel = channel;
    new_data     = 0;

	_automatic = automatic;
	_started = 0;
	inertial_local = 1;

    return( 0 );
}

int inertial_is_local()
{
	return inertial_local;
}

int inertial_port_tick( void )
{
    int res = 0;

    if( !new_data )
    {
        res = imu_recv_packet( );

        if( res == 0 )
        {
            parse_inertial_data( );
        }
        else
        if( res == EAGAIN )
        {
            return( res );
        }
        else
        if( res == -1 )
        {
            fprintf( stderr, "ERROR: invalid data from IMU channel\n" );
        }
        else
        {
            fprintf( stderr, "ERROR: cannot receive from IMU channel\n" );
        }
    }

    return( res );
}

int inertial_port_send_request( void )
{
	if (!_automatic) {
		char buf[1] = { (char) CMD_DESIRED_DATA };

		return comm_channel->transmit( comm_channel, buf, 1 );
	}
	return 0;
}

int inertial_port_send_start( void )
{
	if (_automatic) {
		int ret;
		char buf[64] = { (char) CMD_CONTINUOUSLY,
			(char) CMD_CONFIRMATION,
			(char) CMD_DESIRED_DATA };

		_started = 1;
		comm_channel->transmit( comm_channel, buf, 3 );

		while (comm_channel->poll(comm_channel, 0) > 0) {
			ret = comm_channel->receive(comm_channel, buf, 64);
			printf("empty recv buffer: %d\n", ret);
		}
	}

	return 0;
}

int inertial_port_send_stop( void )
{
	if (_automatic) {
		char buf[3] = { (char) CMD_CONTINUOUSLY,
			(char) CMD_CONFIRMATION,
			(char) CMD_CONFIRMATION };

		_started = 0;
		return comm_channel->transmit( comm_channel, buf, 3 );
	}

	return 0;
}

int inertial_port_get_data( inertial_data_t *data )
{
    int res;

	if (_automatic && !_started)
		return 0;

    while (!new_data) {
        res = inertial_port_tick( );

        if( res == EAGAIN )
        {
            sleep_for( 1000 );
        }
        else
        if( res == -1 )
        {
            return( res );
        }
        else
        if( res != 0 )
        {
            return( 1 );
        }
    }
    memcpy( data, &inertial_data, sizeof( *data ) );
    new_data = 0;

    return( 0 );
}

/* End of file */

