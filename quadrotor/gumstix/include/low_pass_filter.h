/*
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

#ifndef LOW_PASS_FILTER
#define LOW_PASS_FILTER

/* Structure for representing low-pass-filter parameters */
typedef struct
{
    double gain;
    double value;

} low_pass_filter_t;

/* Initializes the low-pass filter.
   Returns 0 if successful, -1 otherwise.
*/
int    low_pass_filter_init( low_pass_filter_t *filter, double gain );

/* Resets the low-pass filter.
   Returns 0 if successful, -1 otherwise.
*/
int    low_pass_filter_reset( low_pass_filter_t *filter );

/* Updates the low-pass filter with the given value.
   Returns the filtered value.
*/
double low_pass_filter_update( low_pass_filter_t *filter, double update );

#endif /* !LOW_PASS_FILTER */

/* End of file */
