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
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <math.h>

#include "protocol.h"
#include "transfer.h"
#include "control_loop.h"
#include "controller.h"
#include "comm_channel.h"
#include "communication.h"
#include "javiator_port.h"
#include "terminal_port.h"
#include "ubisense_port.h"
#include "command_data.h"
#include "javiator_data.h"
#include "sensor_data.h"
#include "motor_signals.h"
#include "ctrl_params.h"
#include "trace_data.h"
#include "rotation_matrix.h"
#include "low_pass_filter.h"
#include "average_filter.h"
#include "median_filter.h"
#include "kalman_filter.h"
#include "us_timer.h"

#define ENABLE_POSITION_CONTROLLERS

/* controller modes */
#define ALT_MODE_GROUND         0x00
#define ALT_MODE_FLYING         0x01
#define ALT_MODE_SHUTDOWN       0x02

/* filter parameters */
#define FILTER_GAIN_DD          0.1
#define FILTER_GAIN_CMD         0.1
#define FILTER_SIZE_BAT         15

/* plant parameters */
#define SONAR_POS_ROLL         0//-95                  /* [mm] sonar position on roll axis */
#define SONAR_POS_PITCH        0//-95                  /* [mm] sonar position on pitch axis */
#define SONAR_POS_YAW          0// 55                  /* [mm] sonar position on yaw axis */
#define X_LASER_POS_ROLL       0//-75                  /* [mm] y-laser position on roll axis */
#define X_LASER_POS_PITCH      0// 60                  /* [mm] y-laser position on pitch axis */
#define X_LASER_POS_YAW        0//-80                  /* [mm] y-laser position on yaw axis */
#define Y_LASER_POS_ROLL       0// 80                  /* [mm] x-laser position on roll axis */
#define Y_LASER_POS_PITCH      0// 55                  /* [mm] x-laser position on pitch axis */
#define Y_LASER_POS_YAW        0//-80                  /* [mm] x-laser position on yaw axis */
#define EARTH_GRAVITY           9810                /* [mm/s^2] gravitational acceleration */

/* controller parameters */
#define COMMAND_THRESHOLD       35                  /* [iterations] max iterations to wait */
#define MAX_ROLL_PITCH          500                 /* [mrad] */

/* scaling constants */
#define FACTOR_EULER_ANGLE      2000.0*M_PI/65536.0 /* [units] --> [mrad] (2*PI*1000 mrad/2^16) */
#define FACTOR_ANGULAR_RATE     8500.0/32768.0      /* [units] --> [mrad/s] */
#define FACTOR_ANGULAR_ACCEL    8500.0/32768.0*76.3 /* [units] --> [mrad/s^2] */
#define FACTOR_LINEAR_ACCEL     9810.0/4681.0       /* [units] --> [mm/s^2] (4681=32768000/7000) */
#define FACTOR_SONAR            2000.0/1024.0       /* [0-5V]  --> [0-2000mm] */
#define FACTOR_BATTERY          18000.0/1024.0      /* [0-5V]  --> [0-18000mV] */
#define FACTOR_PARAMETER        0.001               /* [mrad]  --> [rad] */

/* control loop parameters */
static volatile int             running;
static int                      ms_period;
static int                      us_period;
static int                      compute_z;
static int                      enable_ubisense;
static int                      controller_state;
static int                      altitude_mode;
static long long                next_period;
static double                   uz_old;
#ifdef ENABLE_POSITION_CONTROLLERS
static int                      offset_x;
static int                      offset_y;
#endif

/* motor speed-up parameters */
static int                      motor_revving_add = 50;
static int                      base_motor_speed  = 8000;
static int                      revving_step      = 0;

/* controller objects */
static struct controller        ctrl_roll;
static struct controller        ctrl_pitch;
static struct controller        ctrl_yaw;
#ifdef ENABLE_POSITION_CONTROLLERS
static struct controller        ctrl_x;
static struct controller        ctrl_y;
#endif
static struct controller        ctrl_z;

/* filter objects */
static low_pass_filter_t        filter_ddy;
static low_pass_filter_t        filter_ddx;
static low_pass_filter_t        filter_ddy;
static low_pass_filter_t        filter_ddz;
static low_pass_filter_t        filter_cmd_roll;
static low_pass_filter_t        filter_cmd_pitch;
static low_pass_filter_t        filter_cmd_z;
static median_filter_t          filter_battery;
static kalman_filter_t          filter_dx;
static kalman_filter_t          filter_dy;
static kalman_filter_t          filter_dz;

/* data structures */
static command_data_t           command_data;
static javiator_ldat_t          javiator_data;
static position_data_t          position_data;
static sensor_data_t            sensor_data;
static motor_signals_t          motor_signals;
static command_data_t           motor_offsets;
static trace_data_t             trace_data;

/* controller statistics */
#define NUM_STATS               9
#define STAT_FROM_UBI           0
#define STAT_TO_JAV             1
#define STAT_FROM_JAV           2
#define STAT_FROM_TERM          3
#define STAT_TO_TERM            4
#define STAT_CONTROL            5
#define STAT_SLEEP              6
#define STAT_READ               7
#define STAT_ALL                8

static int                      loop_count = 0;
static long long                stats[ NUM_STATS ] = {0,0,0,0,0,0,0,0,0};
static long long                max_stats[ NUM_STATS ] = {0,0,0,0,0,0,0,0,0};
static char *                   stats_name[ NUM_STATS ] =
{
    "from Ubisense ",
    "to JAviator   ",
    "from JAviator ",
    "from Terminal ",
    "to Terminal   ",
    "control       ",
    "sleep time    ",
    "read time     ",
    "complete loop "
};

/* function pointers */
static void                     signal_handler( int num );
static void                     int_handler( int num );

/* forward declaration */
static void                     print_stats( void );


/****************************************
 *          Control Loop Code           *
 ****************************************/

static inline void set_ctrl_params( struct controller *ctrl,
    double p, double i, double d, double dd )
{
    ctrl->set_params( ctrl, p, i, d, dd );
}

static inline double do_control( struct controller *ctrl,
                                 double current_angle,
                                 double desired_angle,
                                 double angular_velocity,
                                 double angular_acceleration )
{
    return ctrl->control( ctrl, current_angle, desired_angle,
        angular_velocity, angular_acceleration );
}

int control_loop_setup( int period, int control_z, int ubisense )
{
    struct sigaction act;

    running          = 1;
    ms_period        = period;
    us_period        = period * 1000;
    compute_z        = control_z;
    enable_ubisense  = ubisense;
    controller_state = 0;
    altitude_mode    = ALT_MODE_SHUTDOWN;
    next_period      = 0;
    uz_old           = 0;
#ifdef ENABLE_POSITION_CONTROLLERS
    offset_x         = 0;
    offset_y         = 0;
#endif
    act.sa_handler   = signal_handler;
	act.sa_handler   = int_handler;

    if( sigaction( SIGUSR1, &act, NULL ) || sigaction( SIGINT, &act, NULL ) )
    {
        perror( "sigaction" );
    }

    controller_init( &ctrl_roll,  "Roll",  CTRL_PIDD_DEF, ms_period );
    controller_init( &ctrl_pitch, "Pitch", CTRL_PIDD_DEF, ms_period );
    controller_init( &ctrl_yaw,   "Yaw",   CTRL_PIDD_YAW, ms_period );
#ifdef ENABLE_POSITION_CONTROLLERS
    controller_init( &ctrl_x,     "X",     CTRL_PIDD_X_Y, ms_period );
    controller_init( &ctrl_y,     "Y",     CTRL_PIDD_X_Y, ms_period );
#endif
    controller_init( &ctrl_z,     "Z",     CTRL_PIDD_DEF, ms_period );

    low_pass_filter_init( &filter_ddx,       FILTER_GAIN_DD );
    low_pass_filter_init( &filter_ddy,       FILTER_GAIN_DD );
    low_pass_filter_init( &filter_ddz,       FILTER_GAIN_DD );
    low_pass_filter_init( &filter_cmd_roll,  FILTER_GAIN_CMD );
    low_pass_filter_init( &filter_cmd_pitch, FILTER_GAIN_CMD );
    low_pass_filter_init( &filter_cmd_z,     FILTER_GAIN_CMD );
    median_filter_init  ( &filter_battery,   FILTER_SIZE_BAT );
    kalman_filter_init  ( &filter_dx,        ms_period );
    kalman_filter_init  ( &filter_dy,        ms_period );
    kalman_filter_init  ( &filter_dz,        ms_period );

    memset( &command_data,  0, sizeof( command_data ) );
    memset( &javiator_data, 0, sizeof( javiator_data ) );
    memset( &position_data, 0, sizeof( position_data ) );
    memset( &sensor_data,   0, sizeof( sensor_data ) );
    memset( &motor_signals, 0, sizeof( motor_signals ) );
    memset( &motor_offsets, 0, sizeof( motor_offsets ) );
    memset( &trace_data,    0, sizeof( trace_data ) );

    return( 0 );
}

static int check_terminal_connection( void )
{
    static int command_data_delay = 0;

    if( terminal_port_is_new_command_data( ) )
    {
        command_data_delay = 0;
    }
    else
    if( ++command_data_delay > COMMAND_THRESHOLD )
    {
        return( -1 );
    }

    return( 0 );
}

static void set_control_params( ctrl_params_t *params,
    struct controller *ctrl_1, struct controller *ctrl_2 )
{
    double kp  = params->kp  * FACTOR_PARAMETER;
    double ki  = params->ki  * FACTOR_PARAMETER;
    double kd  = params->kd  * FACTOR_PARAMETER;
    double kdd = params->kdd * FACTOR_PARAMETER;

    if( ctrl_1 != NULL )
    {
        set_ctrl_params( ctrl_1, kp, ki, kd, kdd );
        fprintf( stdout, "parameter update: %s", ctrl_1->name );

        if( ctrl_2 != NULL )
        {
            set_ctrl_params( ctrl_2, kp, ki, kd, kdd );
            fprintf( stdout, "/%s", ctrl_2->name );
        }

        fprintf( stdout, "\n--> Kp: %+3.5f   Ki: %+3.5f   Kd: %+3.5f   Kdd: %+3.5f\n",
            kp, ki, kd, kdd );
        fflush( stdout );
    }
}

static void get_control_params( void )
{
    ctrl_params_t params;

    if( terminal_port_is_new_r_p_params( ) )
    {
        terminal_port_get_r_p_params( &params );
        set_control_params( &params, &ctrl_roll, &ctrl_pitch );
    }

    if( terminal_port_is_new_yaw_params( ) )
    {
        terminal_port_get_yaw_params( &params );
        set_control_params( &params, &ctrl_yaw, NULL );
    }

    if( terminal_port_is_new_x_y_params( ) )
    {
        terminal_port_get_x_y_params( &params );
#ifdef ENABLE_POSITION_CONTROLLERS
        set_control_params( &params, &ctrl_x, &ctrl_y );
#endif
    }

    if( terminal_port_is_new_alt_params( ) )
    {
        terminal_port_get_alt_params( &params );
        set_control_params( &params, &ctrl_z, NULL );
    }
}

static inline double get_corrected_x( void )
{
    return rotation_matrix_rotate_x(
        X_LASER_POS_ROLL + atoi( (const char *) javiator_data.x_pos ),
        X_LASER_POS_PITCH,
        X_LASER_POS_YAW );
}

static inline double get_corrected_y( void )
{
    return rotation_matrix_rotate_y(
        Y_LASER_POS_ROLL,
        Y_LASER_POS_PITCH + atoi( (const char *) javiator_data.y_pos ),
        Y_LASER_POS_YAW );
}

static inline double get_corrected_z( void )
{
    return rotation_matrix_rotate_z(
        SONAR_POS_ROLL,
        SONAR_POS_PITCH,
        SONAR_POS_YAW + FACTOR_SONAR * javiator_data.sonar );
}

static inline double get_filtered_ddx( void )
{
    return low_pass_filter_apply( &filter_ddx, rotation_matrix_rotate_x(
        sensor_data.ddx,
        sensor_data.ddy,
        sensor_data.ddz ) );
}

static inline double get_filtered_ddy( void )
{
    return low_pass_filter_apply( &filter_ddy, rotation_matrix_rotate_y(
        sensor_data.ddx,
        sensor_data.ddy,
        sensor_data.ddz ) );
}

static inline double get_filtered_ddz( void )
{
    return low_pass_filter_apply( &filter_ddz, rotation_matrix_rotate_z(
        sensor_data.ddx,
        sensor_data.ddy,
        sensor_data.ddz ) + EARTH_GRAVITY );
}

static void adjust_sensor_data( void )
{
    static int offset_yaw = 0;
    static int offset_z   = 0;

    if( motor_signals.front == 0 && motor_signals.right == 0 &&
        motor_signals.rear  == 0 && motor_signals.left  == 0 )
    {
        offset_yaw      = sensor_data.yaw;
        offset_z        = sensor_data.z;
        sensor_data.yaw = 0;
        sensor_data.z   = 0;
#ifdef ENABLE_POSITION_CONTROLLERS
        if( enable_ubisense )
        {
            offset_x = position_data.x;
            offset_y = position_data.y;
        }
        else
        {
            offset_x = sensor_data.x;
            offset_y = sensor_data.y;
        }
#endif
    }
    else
    {
        sensor_data.yaw -= offset_yaw;
        sensor_data.z   -= offset_z;
    }
}

static int get_javiator_data( void )
{
    static int      count = 2;
    static uint16_t old_z = 13;
    int             res   = javiator_port_get_data( &javiator_data );

    if( res )
    {
        fprintf( stderr, "ERROR: data from JAviator not available\n" );
        return( res );
    }

    /* copy and scale Euler angles */
    sensor_data.roll    = (int)( FACTOR_EULER_ANGLE * javiator_data.roll );
    sensor_data.pitch   = (int)( FACTOR_EULER_ANGLE * javiator_data.pitch );
    sensor_data.yaw     = (int)( FACTOR_EULER_ANGLE * javiator_data.yaw );

    /* save old angular rates */
    sensor_data.ddroll  = sensor_data.droll;
    sensor_data.ddpitch = sensor_data.dpitch;
    sensor_data.ddyaw   = sensor_data.dyaw;

    /* copy and scale angular rates */
    sensor_data.droll   = (int)( FACTOR_ANGULAR_RATE * javiator_data.droll );
    sensor_data.dpitch  = (int)( FACTOR_ANGULAR_RATE * javiator_data.dpitch );
    sensor_data.dyaw    = (int)( FACTOR_ANGULAR_RATE * javiator_data.dyaw );

    /* compute angular accelerations */
    sensor_data.ddroll  = (int)( FACTOR_ANGULAR_ACCEL * (sensor_data.droll  - sensor_data.ddroll) );
    sensor_data.ddpitch = (int)( FACTOR_ANGULAR_ACCEL * (sensor_data.dpitch - sensor_data.ddpitch) );
    sensor_data.ddyaw   = (int)( FACTOR_ANGULAR_ACCEL * (sensor_data.dyaw   - sensor_data.ddyaw) );

    /* copy and scale linear accelerations */
    sensor_data.ddx     = (int)( FACTOR_LINEAR_ACCEL * javiator_data.ddx );
    sensor_data.ddy     = (int)( FACTOR_LINEAR_ACCEL * javiator_data.ddy );
    sensor_data.ddz     = (int)( FACTOR_LINEAR_ACCEL * javiator_data.ddz );

    /* reject possible sonar outliers */
    if( abs( old_z - javiator_data.sonar ) > 20 && count < 2 )
    {
        javiator_data.sonar = old_z;
        ++count;
    }
    else
    {
        count = 0;
    }

    old_z = javiator_data.sonar;

    /* copy corrected positions */
    sensor_data.x = (int) get_corrected_x( );
    sensor_data.y = (int) get_corrected_y( );
    sensor_data.z = (int) get_corrected_z( );

    /* scale and filter battery level */
    sensor_data.battery = (int) median_filter_apply( &filter_battery,
        FACTOR_BATTERY * javiator_data.batt );

    return( 0 );
}

static int get_ubisense_data( void )
{
    int res = ubisense_port_get_data( &position_data );

    if( res == -1 )
    {
        return( 0 );
    }

    if( res )
    {
        fprintf( stderr, "ERROR: data from Ubisense not available\n" );
        return( res );
    }

    return( 0 );
}

static int get_command_data( void )
{
    double filtered_ddx  = 0;
    double filtered_ddy  = 0;
    double estimated_dx  = 0;
    double estimated_dy  = 0;
    double estimated_x   = 0;
    double estimated_y   = 0;
    double rotated_roll  = 0;
    double rotated_pitch = 0;

    if( terminal_port_is_shut_down( ) )
    {
        altitude_mode = ALT_MODE_SHUTDOWN;
        terminal_port_reset_shut_down( );
    }
    else
    if( terminal_port_is_mode_switch( ) )
    {
		printf( "Mode Switch ...\n" );
		print_stats( );

		loop_count = 0;
		memset( stats, 0, sizeof( stats ) );
		memset( max_stats, 0, sizeof( max_stats ) );

        switch( altitude_mode )
        {
            case ALT_MODE_GROUND:
                altitude_mode = ALT_MODE_FLYING;
                break;

            case ALT_MODE_FLYING:
                altitude_mode = ALT_MODE_GROUND;
                break;

            case ALT_MODE_SHUTDOWN:
                altitude_mode = ALT_MODE_GROUND;
                break;

            default:
                altitude_mode = ALT_MODE_SHUTDOWN;
        }
    }
    else
    if( terminal_port_is_new_command_data( ) )
    {
        terminal_port_get_command_data( &command_data );
    }

    /* apply low-pass filtering to roll, pitch, and z command */
    command_data.roll  = (int16_t) low_pass_filter_apply(
        &filter_cmd_roll, command_data.roll );

    command_data.pitch = (int16_t) low_pass_filter_apply(
        &filter_cmd_pitch, command_data.pitch );

    command_data.z     = (int16_t) low_pass_filter_apply(
        &filter_cmd_z, command_data.z );

    filtered_ddx = get_filtered_ddx( );
    filtered_ddy = get_filtered_ddy( );

    if( enable_ubisense )
    {
        estimated_dx = kalman_filter_apply( &filter_dx, position_data.x, filtered_ddx );
        estimated_dy = kalman_filter_apply( &filter_dy, position_data.y, filtered_ddy );
    }
    else
    {
        estimated_dx = kalman_filter_apply( &filter_dx, sensor_data.x, filtered_ddx );
        estimated_dy = kalman_filter_apply( &filter_dy, sensor_data.y, filtered_ddy );
    }

    estimated_x = filter_dx.s;
    estimated_y = filter_dy.s;

#ifdef ENABLE_POSITION_CONTROLLERS
    if( terminal_port_is_test_mode( ) )
    {
        command_data.roll  = (int16_t) -do_control( &ctrl_y,
            estimated_y, offset_y - command_data.roll, estimated_dy, filtered_ddy );

        command_data.pitch = (int16_t)  do_control( &ctrl_x,
            estimated_x, offset_x + command_data.pitch, estimated_dx, filtered_ddx );

        if( command_data.roll > MAX_ROLL_PITCH )
        {
            command_data.roll = MAX_ROLL_PITCH;
        }
        else
        if( command_data.roll < -MAX_ROLL_PITCH )
        {
            command_data.roll = -MAX_ROLL_PITCH;
        }

        if( command_data.pitch > MAX_ROLL_PITCH )
        {
            command_data.pitch = MAX_ROLL_PITCH;
        }
        else
        if( command_data.pitch < -MAX_ROLL_PITCH )
        {
            command_data.pitch = -MAX_ROLL_PITCH;
        }
    }
    else
    if( enable_ubisense )
    {
        offset_x = position_data.x;
        offset_y = position_data.y;
    }
    else
    {
        offset_x = sensor_data.x;
        offset_y = sensor_data.y;
    }

    /* save values for logging */
    sensor_data.x       = (int) estimated_x;
    sensor_data.y       = (int) estimated_y;
    sensor_data.dx      = (int) estimated_dx;
    sensor_data.dy      = (int) estimated_dy;
    sensor_data.ddx     = (int) filtered_ddx;
    sensor_data.ddy     = (int) filtered_ddy;
#endif

    trace_data.value_1  = (int16_t)( -command_data.roll );
    trace_data.value_2  = (int16_t)( command_data.pitch );
    trace_data.value_3  = (int16_t)( position_data.y );
    trace_data.value_4  = (int16_t)( position_data.x );
#ifdef ENABLE_POSITION_CONTROLLERS
    trace_data.value_5  = (int16_t)( controller_get_p_term( &ctrl_y ) );
    trace_data.value_6  = (int16_t)( controller_get_i_term( &ctrl_y ) );
    trace_data.value_7  = (int16_t)( controller_get_d_term( &ctrl_y ) );
    trace_data.value_8  = (int16_t)( controller_get_dd_term( &ctrl_y ) );
    trace_data.value_9  = (int16_t)( controller_get_p_term( &ctrl_x ) );
    trace_data.value_10 = (int16_t)( controller_get_i_term( &ctrl_x ) );
    trace_data.value_11 = (int16_t)( controller_get_d_term( &ctrl_x ) );
    trace_data.value_12 = (int16_t)( controller_get_dd_term( &ctrl_x ) );
#endif

    /* rotate roll and pitch command depending on yaw angle */
    rotated_roll  = rotation_matrix_rotate_x( command_data.roll,
        command_data.pitch, command_data.yaw );

    rotated_pitch = rotation_matrix_rotate_y( command_data.roll,
        command_data.pitch, command_data.yaw );

    /* replace original commands with rotated commands */
    command_data.roll  = (int16_t) rotated_roll;
    command_data.pitch = (int16_t) rotated_pitch;

    return( 0 );
}

static inline void reset_motor_signals( void )
{
    motor_signals.front = 0;
    motor_signals.right = 0;
    motor_signals.rear  = 0;
    motor_signals.left  = 0;
}

static void reset_controllers( void )
{
    ctrl_roll  .reset_zero( &ctrl_roll );
    ctrl_pitch .reset_zero( &ctrl_pitch );
    ctrl_yaw   .reset_zero( &ctrl_yaw );
#ifdef ENABLE_POSITION_CONTROLLERS
    ctrl_x     .reset_zero( &ctrl_x );
    ctrl_y     .reset_zero( &ctrl_y );
#endif
    ctrl_z     .reset_zero( &ctrl_z );

    uz_old = 0;
}

static inline void reset_filters( void )
{
    low_pass_filter_reset( &filter_ddx );
    low_pass_filter_reset( &filter_ddy );
    low_pass_filter_reset( &filter_ddz );
    low_pass_filter_reset( &filter_cmd_roll );
    low_pass_filter_reset( &filter_cmd_pitch );
    low_pass_filter_reset( &filter_cmd_z );
    kalman_filter_reset  ( &filter_dz );
}

static int perform_shut_down( void )
{
    controller_state = 0;
    revving_step     = 0;
    base_motor_speed = terminal_port_get_base_motor_speed( );

    reset_motor_signals( );
    reset_controllers( );
    reset_filters( );

    return( 1 );
}

static int perform_ground_actions( void )
{
    if( revving_step > 0 )
    {
        if( motor_signals.front > 0 )
        {
            motor_signals.front -= motor_revving_add;
        }

        if( motor_signals.right > 0 )
        {
            motor_signals.right -= motor_revving_add;
        }

        if( motor_signals.rear > 0 )
        {
            motor_signals.rear -= motor_revving_add;
        }

        if( motor_signals.left > 0 )
        {
            motor_signals.left -= motor_revving_add;
        }

        --revving_step;
    }
    else
    {
        perform_shut_down( );
    }

    return( 1 );
}

static int compute_motor_signals( void )
{
    double filtered_ddz = get_filtered_ddz( );
    double estimated_dz = kalman_filter_apply( &filter_dz, sensor_data.z, -filtered_ddz );
    double estimated_z  = filter_dz.s;
    double uroll        = 0;
    double upitch       = 0;
    double uyaw         = 0;
    double uz_new       = 0;
    int i, signals[4];

    if( revving_step < (base_motor_speed / motor_revving_add) )
    {
        uz_new = uz_old + motor_revving_add;
        ++revving_step;
    }
    else
    {
        if( command_data.z < 10 && controller_get_integral( &ctrl_z ) > 0 )
        {
            controller_set_integral( &ctrl_z, controller_get_integral( &ctrl_z ) - 10 );
        }

        uroll  = do_control( &ctrl_roll,
            sensor_data.roll, command_data.roll, sensor_data.droll, sensor_data.ddroll );

        upitch = do_control( &ctrl_pitch,
            sensor_data.pitch, command_data.pitch, sensor_data.dpitch, sensor_data.ddpitch );

        uyaw   = do_control( &ctrl_yaw,
            sensor_data.yaw, command_data.yaw, sensor_data.dyaw, sensor_data.ddyaw );

        if( compute_z )
	    {
		    uz_new = base_motor_speed + do_control( &ctrl_z,
                estimated_z, command_data.z , estimated_dz, filtered_ddz );
	    }
        else
        {
            uz_new = command_data.z * 16;
        }
    }

    signals[0] = (int)( uz_new + uyaw + upitch );
    signals[1] = (int)( uz_new - uyaw - uroll );
    signals[2] = (int)( uz_new + uyaw - upitch );
    signals[3] = (int)( uz_new - uyaw + uroll );

    for( i = 0; i < 4; ++i )
    {
        if( signals[i] < MOTOR_MIN )
        {
            signals[i] = MOTOR_MIN;
        }
        else
        if( signals[i] > MOTOR_MAX )
        {
            signals[i] = MOTOR_MAX;
        }
    }

    /* save values for logging */
    sensor_data.z       = (int) estimated_z;
    sensor_data.dz      = (int) estimated_dz;
    sensor_data.ddz     = (int) filtered_ddz;

    motor_signals.front = (int16_t)( signals[0] );
    motor_signals.right = (int16_t)( signals[1] );
    motor_signals.rear  = (int16_t)( signals[2] );
    motor_signals.left  = (int16_t)( signals[3] );

    motor_offsets.roll  = (int16_t)( uroll );
    motor_offsets.pitch = (int16_t)( upitch );
    motor_offsets.yaw   = (int16_t)( uyaw );
    motor_offsets.z     = (int16_t)( uz_new );

    trace_data.value_13 = (int16_t)( controller_get_p_term( &ctrl_z ) );
    trace_data.value_14 = (int16_t)( controller_get_i_term( &ctrl_z ) );
    trace_data.value_15 = (int16_t)( controller_get_d_term( &ctrl_z ) );
    trace_data.value_16 = (int16_t)( controller_get_dd_term( &ctrl_z ) );

    uz_old              = uz_new;

    return( 0 );
}

static int send_motor_signals( void )
{
    int res = javiator_port_send_motor_signals( &motor_signals );

    if( res == -1 )
    {
        fprintf( stderr, "ERROR: connection to JAviator broken\n" );
        res = javiator_port_send_motor_signals( &motor_signals );

        if( res == -1 )
        {
            fprintf( stderr, "ERROR: no success with reconnecting to JAviator\n" );
        }
    }

    return( res );
}

static int send_report_to_terminal( void )
{
    return terminal_port_send_report(
        &sensor_data,
        &motor_signals,
        &motor_offsets,
        controller_state,
        altitude_mode );
}

static int send_trace_data_to_terminal( void )
{
    return terminal_port_send_trace_data( &trace_data );
}

static int wait_for_next_period( void )
{
    if( sleep_until( next_period ) )
    {
        fprintf( stderr, "error in sleep_until\n" );
        exit( 1 );
    }

    next_period += us_period;
    return( 0 );
}

void calc_stats( long long time, int id )
{
    stats[id] += time;

    if( loop_count > 10 && time > max_stats[ id ] )
    { 
		/* drop the first few stats for max calculation */
        max_stats[ id ] = time;
    }
}

static void print_stats( void )
{
    int i;

    if( loop_count )
    {
        fprintf( stdout, "Loop Statistics:\n" );

        for( i = 0; i < NUM_STATS; ++i )
        {
            fprintf( stdout, "\t%s %8lld us\tmax %8lld us\n",
                stats_name[i], stats[i] / loop_count, max_stats[i] );
        }
    }
}

static void signal_handler( int num )
{
    print_stats( );
}

static void int_handler( int num )
{
	running = 0;
}

static int read_sensors( void )
{
	uint64_t start, end;

	start = get_utime( );

	if( get_javiator_data( ) )
    {
        fprintf( stderr, "ERROR: connection to JAviator broken\n" );
		return( -1 );
	}

	end = get_utime( );
	calc_stats( end - start, STAT_FROM_JAV );

	start = get_utime( );

	if( enable_ubisense && get_ubisense_data( ) )
    {
        fprintf( stderr, "ERROR: connection to Ubisense broken\n" );
		return( -1 );
	}

    end = get_utime( );
    calc_stats( end - start, STAT_FROM_UBI );

	/* IMPORTANT: yaw angle must be adjusted BEFORE
	   updating the rotation matrix */
	adjust_sensor_data( );

	/* IMPORTANT: all angles must be given in [rad] */
    rotation_matrix_update(
        sensor_data.roll  / 1000.0,
        sensor_data.pitch / 1000.0,
        sensor_data.yaw   / 1000.0 );

	return( 0 );
}

/* the control loop for our helicopter */
int control_loop_run( void )
{
    int first_time = 1;
    long long start, end;
	long long loop_start;

    altitude_mode = ALT_MODE_SHUTDOWN;
    next_period   = get_utime( ) + us_period;

    while( running )
    {
        start = get_utime( );
        loop_start = start;

        if( send_motor_signals( ) )
        {
            break;
        }

        end = get_utime( );
        calc_stats( end - start, STAT_TO_JAV );

        if( read_sensors( ) )
        {
            altitude_mode = ALT_MODE_SHUTDOWN;
            perform_shut_down( );
            break;
        }

        start = get_utime( );

        if( check_terminal_connection( ) )
        {
            altitude_mode = ALT_MODE_SHUTDOWN;

            if( first_time )
            {
                fprintf( stderr, "ERROR: connection to terminal broken\n" );
                first_time = 0;
            }
        }
        else
        {
            first_time = 1;
        }

        get_command_data( );
        get_control_params( );

        end = get_utime( );
        calc_stats( end - start, STAT_FROM_TERM );

        start = get_utime( );
 
        switch( altitude_mode )
        {
            case ALT_MODE_GROUND:
                perform_ground_actions( );
                break;

            case ALT_MODE_FLYING:
                compute_motor_signals( );
                break;

            case ALT_MODE_SHUTDOWN:
                perform_shut_down( );
                break;

            default:
                fprintf( stderr, "ERROR: invalid altitude mode %d\n", altitude_mode );
        }

        end = get_utime( );
        calc_stats( end - start, STAT_CONTROL );

        start = get_utime( );

        send_report_to_terminal( );
        send_trace_data_to_terminal( );

        end = get_utime( );
        calc_stats( end - start, STAT_TO_TERM );

        calc_stats( end - loop_start, STAT_ALL );

        start = get_utime( );

        wait_for_next_period( );

        end = get_utime( );
        calc_stats( end - start, STAT_SLEEP );

        if( ++loop_count < 0 )
        {
            fprintf( stderr, "WARNING: stats overrun\n" );
            loop_count = 0;
            memset( stats, 0, sizeof( stats ) );
        }
    }

    controller_destroy( &ctrl_roll );
    controller_destroy( &ctrl_pitch );
    controller_destroy( &ctrl_yaw );
#ifdef ENABLE_POSITION_CONTROLLERS
    controller_destroy( &ctrl_x );
    controller_destroy( &ctrl_y );
#endif
    controller_destroy( &ctrl_z );

    median_filter_destroy( &filter_battery );

    print_stats( );

    return( 0 );
}

int control_loop_stop( void )
{
    running = 0;
    return( 0 );
}

/* End of file */
