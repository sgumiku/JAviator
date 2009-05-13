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
#include <stdio.h>

#include "ctrl_params.h"


int ctrl_params_to_stream( const ctrl_params_t *params, char *buf, int len )
{
    if( len == CTRL_PARAMS_SIZE )
    {
        buf[0] = (char)( params->kp >> 8 );
        buf[1] = (char)( params->kp );
        buf[2] = (char)( params->ki >> 8 );
        buf[3] = (char)( params->ki );
        buf[4] = (char)( params->kd >> 8 );
        buf[5] = (char)( params->kd );
        buf[6] = (char)( params->kdd >> 8 );
        buf[7] = (char)( params->kdd );
        return( 0 );
    }

    fprintf( stderr, "ERROR in %s %d: invalid length (%d) of control params to stream\n",
        __FILE__, __LINE__, len );
    return( -1 );
}

int ctrl_params_from_stream( ctrl_params_t *params, const char *buf, int len )
{
    if( len == CTRL_PARAMS_SIZE )
    {
        params->kp  = (int16_t)( (buf[0] << 8) | (buf[1] & 0xFF) );
        params->ki  = (int16_t)( (buf[2] << 8) | (buf[3] & 0xFF) );
        params->kd  = (int16_t)( (buf[4] << 8) | (buf[5] & 0xFF) );
        params->kdd = (int16_t)( (buf[6] << 8) | (buf[7] & 0xFF) );
        return( 0 );
    }

    fprintf( stderr, "ERROR in %s %d: invalid length (%d) of control params from stream\n",
        __FILE__, __LINE__, len );
    return( -1 );
}

/* End of file */