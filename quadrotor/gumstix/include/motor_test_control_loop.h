/*
 *  Copyright (c) 2006-2013 Harald Roeck <harald.roeck@gmail.com>
 *                      and Rainer Trummer <rainer.trummer@gmail.com>
 *
 *  Department of Computer Sciences, www.cs.uni-salzburg.at
 *  University of Salzburg, www.uni-salzburg.at
 *
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 *
 */

#ifndef CONTROL_LOOP_H
#define CONTROL_LOOP_H

int control_loop_setup( void );

int control_loop_run( int motor, int speed );

int control_loop_stop( void );

#endif /* !CONTROL_LOOP_H */

/* End of file */
