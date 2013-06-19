#include <math.h>
#include <stdlib.h>
//
#include "kernel.h"
#include "kernel_id.h"
#include "ecrobot_interface.h"
//
#include "Common.h"
#include "NXT_Config.h"
#include "kfkf/kfkfModel.h"
#include "Logger.h"
#include "SensorManager.h"
#include "Controller.h"

/*
===============================================================================================
	macro
===============================================================================================
*/
#define BLUETOOTH

/*
===============================================================================================
	task declaration
===============================================================================================
*/
DeclareCounter(SysTimerCnt);
DeclareTask(TaskMain);					/* Task to manage behavior of robot */
DeclareTask(TaskActuator);				/* Task to actuate */
DeclareTask(TaskSensor);				/* Task to sense */
DeclareTask(TaskLogger);				/* Task to send logging data */

/*
===============================================================================================
	task declaration
===============================================================================================
*/
typedef enum _MainTaskState
{
	INIT,
	BTCOMM,
	TARGETCALIB,
	WHITECALIB,
	BLACKCALIB,
	ACTION
}MainTaskState_e;

/*
===============================================================================================
	prototype declaration
===============================================================================================
*/
//
void InitNXT();
//
State_t EventSensor();
//
void setController(const State_t state);

//void gyro_calibration();
//void calibration(int *black,int *white,int *gray);
//void tail_run_turn2pwm(S16 _tail_run_speed ,float _turn ,S8 *_pwm_L, S8 *_pwm_R);
S16 calcAngle2Encoder(S16 ang);
//S8 calc_variance(U16 *buf,int _len);
void TailStand();



/*
===============================================================================================
	variables
===============================================================================================
*/
S8 g_pwm_L = 0;
S8 g_pwm_R = 0;
S8 g_pwm_T = 0;

//--------------------------------------------------------------------
//	For sensing
//--------------------------------------------------------------------
static Sensor_t g_Sensor;

//--------------------------------------------------------------------
//	For controlling
//--------------------------------------------------------------------
/* Controller */
static Controller_t g_Controller;
/* Event status */
static EventStatus_t g_EventStatus = {NO,0,0,NO,0,0,NO,0,0,NO,NO,0,0,NO,NO,0,0,NO};

//--------------------------------------------------------------------
//	For logging
//--------------------------------------------------------------------
static LogType_e g_LogType = LOG_NO;


/*
===============================================================================================
	ecrobot_device_initialize
===============================================================================================
*/
void ecrobot_device_initialize(){
	/* Motor Initialization */
	nxt_motor_set_speed( LEFT_MOTOR, 0, 0);
	nxt_motor_set_speed( RIGHT_MOTOR, 0, 0);
	nxt_motor_set_speed( TAIL_MOTOR, 0, 0);
	nxt_motor_set_count( RIGHT_MOTOR, 0);
	nxt_motor_set_count( LEFT_MOTOR, 0);
	nxt_motor_set_count( TAIL_MOTOR, 0);

	/* Sensor:Light Initialization */
	ecrobot_set_light_sensor_active( LIGHT_SENSOR );

	/* Sensor:Sonar Initialization */
	ecrobot_init_sonar_sensor( SONAR_SENSOR );

	/* Bluetooth device Initialization */
	ecrobot_init_bt_slave( BT_PASS_KEY );
}

/*
===============================================================================================
	ecrobot_device_terminate
===============================================================================================
*/
void ecrobot_device_terminate(){
	/* Motor Termination */
	nxt_motor_set_speed( LEFT_MOTOR, 0, 0);
	nxt_motor_set_speed( RIGHT_MOTOR, 0, 0);
	nxt_motor_set_speed( TAIL_MOTOR, 0, 0);
	nxt_motor_set_count( RIGHT_MOTOR, 0);
	nxt_motor_set_count( LEFT_MOTOR, 0);
	nxt_motor_set_count( TAIL_MOTOR, 0);

	/* Sensor:Light Termination */
	ecrobot_set_light_sensor_inactive( LIGHT_SENSOR );

	/* Sensor:Sonar Termination */
	ecrobot_term_sonar_sensor( SONAR_SENSOR );

	/* Bluetooth device Termination */
	ecrobot_term_bt_connection();
}

/*
===============================================================================================
	user_1ms_isr_type2
===============================================================================================
*/
void user_1ms_isr_type2(void){
	SignalCounter(SysTimerCnt);   
}



/****************************************************************************************************************************
	Task
****************************************************************************************************************************/
/*
===============================================================================================
	name: TaskMain
	Description: ??
===============================================================================================
*/
//--------------------------------------------------------------------
//  Variables for TaskMain
//--------------------------------------------------------------------
static U8 g_CalibCnt = 0;
static U32 g_CalibGyroSum = 0;
static U32 g_CalibLightSum = 0;
static boolean g_CalibFlag = OFF;
static MainTaskState_e g_MTState = INIT;

//--------------------------------------------------------------------
//  Task
//--------------------------------------------------------------------
TASK(TaskMain)
{

	switch(g_MTState)
	{
		//==========================================
		//	Initialization
		//==========================================
		case INIT:
			display_clear(0);
			display_goto_xy(0, 1);
			display_string("Robot Start");
			display_update();
			InitNXT();

			/* Transition:Auto */
			g_MTState = BTCOMM;
			break;

		//==========================================
		//	Bluetooth Communication
		//==========================================
		case BTCOMM:
			TailStand();
			receive_BT();

			/* Transition:Auto */
			g_MTState = TARGETCALIB;
			break;

		//==========================================
		//	Calibration of gray color & gyro offset
		//==========================================
		case TARGETCALIB:
			if( g_Sensor.touch == ON ) g_CalibFlag = ON;

			if(g_CalibFlag == ON)
			{
				g_CalibCnt++;
				g_CalibGyroSum += g_Sensor.gyro;
				g_CalibLightSum += g_Sensor.light;
			}

			if(g_CalibCnt >= 100)
			{
				g_Sensor.gyro_offset = (U16)(g_CalibGyroSum / g_CalibCnt);
				g_Sensor.target_gray = (U16)(g_CalibLightSum / g_CalibCnt);
				g_Sensor.target_gray_base = g_Sensor.target_gray;
				g_CalibFlag = OFF;
				g_CalibGyroSum = 0;
				g_CalibLightSum = 0;
				g_CalibCnt = 0;

				/* Transition:End of the calculation for gray color & gyro offset   */
				g_MTState = WHITECALIB;
			}
			break;

		//==========================================
		//	Calibration of white color
		//==========================================
		case WHITECALIB:
			if( g_Sensor.touch == ON ) g_CalibFlag = ON;

			if(g_CalibFlag == ON)
			{
				g_CalibCnt++;
				g_CalibLightSum += g_Sensor.light;
			}

			if(g_CalibCnt >= 100)
			{
				g_Sensor.white = (U16)(g_CalibLightSum / g_CalibCnt);
				g_CalibFlag = OFF;
				g_CalibLightSum = 0;
				g_CalibCnt = 0;

				/* Transition:End of the calculation for white color   */
				g_MTState =  BLACKCALIB;
			}
			break;

		//==========================================
		//	Calibration of black color
		//==========================================
		case BLACKCALIB:
			if( g_Sensor.touch == ON ) g_CalibFlag = ON;

			if( g_CalibFlag == ON )
			{
				g_CalibCnt++;
				g_CalibLightSum += g_Sensor.light;
			}

			if(g_CalibCnt >= 100)
			{
				g_Sensor.black = (U16)(g_CalibLightSum / g_CalibCnt);
				g_CalibFlag = OFF;
				g_CalibLightSum = 0;
				g_CalibCnt = 0;

				/* Transition:End of the calculation for black color  */
				g_MTState = ACTION;
			}
			break;

		//==========================================
		//	kfkf Model
		//==========================================
		case ACTION:
			setController( EventSensor() );

			if( ecrobot_is_ENTER_button_pressed() == ON )
			{
				/* Transition:Pressing ENTER button  */
				g_MTState = INIT;
			}
			break;
	}

	/* Termination of Task */
	TerminateTask();
}


/*
===============================================================================================
	name: TaskActuator
	Description: ??
===============================================================================================
*/
TASK(TaskActuator)
{
	//==========================================
	//  PWM
	//==========================================
	g_pwm_L = 0;
	g_pwm_R = 0;
	g_pwm_T = 0;

	//==========================================
	//	Calculate PWM of right & left motor
	//==========================================
	if( g_Controller.PIDmode != NO_PID_MODE )
	{
		g_Controller.pre_dif = g_Controller.dif;

		if( g_Controller.PIDmode == WB_PID )
		{
			g_Controller.dif = g_Sensor.light - g_Sensor.target_gray;
		}
		else if( g_Controller.PIDmode == WG_PID )
		{
			//g_Controller.dif = g_Sensor.light - g_Sensor.white_gray_threshold;//?
		}

		g_Controller.differential = g_Controller.dif - g_Controller.pre_dif;
		g_Controller.integral += (g_Controller.dif + g_Controller.pre_dif)/2.0 * 0.004;

		g_Controller.turn = g_Controller.P_gain * g_Controller.dif
				+ g_Controller.I_gain * g_Controller.integral
				+ g_Controller.D_gain * g_Controller.differential;
	}

	g_Controller.tail_pre_dif = g_Controller.tail_dif;
	g_Controller.tail_dif = g_Controller.target_tail - g_Sensor.count_tail;
	
	if( g_Controller.StandMode == BALANCE )
	{
		balance_control(
			(F32)g_Controller.forward,
			(F32)g_Controller.turn,
			(F32)g_Sensor.gyro,
			(F32)g_Sensor.gyro_offset,
			(F32)g_Sensor.count_left,
			(F32)g_Sensor.count_right,
			(F32)g_Sensor.battery,
			&g_pwm_L,
			&g_pwm_R
		);
	
	}
	else if(g_Controller.StandMode == TAIL)
	{
		
		balance_control(
			(F32)g_Controller.forward,
			(F32)g_Controller.turn,
			(F32)g_Sensor.gyro_offset,
			(F32)g_Sensor.gyro_offset,
			(F32)g_Sensor.count_left,
			(F32)g_Sensor.count_right,
			(F32)g_Sensor.battery,
			&g_pwm_L,
			&g_pwm_R
		);

	}

	//==========================================
	//	Calculate PWM of tail motor
	//==========================================
	//g_pwm_T = (U8)( g_Controller.TP_gain * g_Controller.tail_dif + g_Controller.TD_gain * (g_Controller.tail_pre_dif - g_Controller.tail_dif) );
	g_pwm_T = (U8)( g_Controller.TP_gain * g_Controller.tail_dif );

	if(g_pwm_T > 100)
	{
		g_pwm_T = 100;
	}
	else if(g_pwm_T < -100)
	{
		g_pwm_T = -100;
	}

	//==========================================
	//  Set PWM
	//==========================================
	nxt_motor_set_speed( TAIL_MOTOR, g_pwm_T, 1 );
	nxt_motor_set_speed( LEFT_MOTOR, g_pwm_L, 1 );
	nxt_motor_set_speed( RIGHT_MOTOR, g_pwm_R, 1 );

	//==========================================
	//  Termination of Task
	//==========================================
	TerminateTask();
}

/*
===============================================================================================
	name: TaskSensor
	Description: ??
===============================================================================================
*/
#define LIGHT_BUFFER_LENGTH_MAX 250

//--------------------------------------------------------------------
//  Variables for TaskMain
//--------------------------------------------------------------------
static U8 g_SonarCnt = 0;
static U8 g_LightCnt = 0;
static U16 g_LightBuffer[ LIGHT_BUFFER_LENGTH_MAX ] = {0};
static U32 g_LightAve = 0;

//--------------------------------------------------------------------
//  Task
//--------------------------------------------------------------------
TASK(TaskSensor)
{
	//U8 i = 0;

	//==========================================
	//	Data Update of Sensor
	//==========================================
	//--------------------------------
	//	Light Data
	//--------------------------------
	// Update Data of Light Sensor
	g_Sensor.light = ecrobot_get_light_sensor(LIGHT_SENSOR);

	g_LightBuffer[g_LightCnt] = g_Sensor.light;
	g_LightCnt++;
	if( g_LightCnt >= LIGHT_BUFFER_LENGTH_MAX )
	{
		g_LightCnt = 0;
	}

	for(i=0;i < LIGHT_BUFFER_LENGTH_MAX;i++)
	{
		g_LightAve += g_LightBuffer[i];
	}
	g_Sensor.light_ave = (U16)(g_LightAve / LIGHT_BUFFER_LENGTH_MAX);

	//--------------------------------
	//	Gyro Data
	//--------------------------------
	// Update Data of Gyro Sensor
	g_Sensor.gyro = ecrobot_get_gyro_sensor(GYRO_SENSOR);

	//--------------------------------
	//	Sonar Data
	//--------------------------------
	if(g_SonarCnt > 10)
	{
		// Update Data of Sonar Sensor
		g_Sensor.distance = ecrobot_get_sonar_sensor(SONAR_SENSOR);
		g_SonarCnt = 0;
	}
	else
	{
		g_SonarCnt++;
	}

	//--------------------------------
	//	Touch Data
	//--------------------------------
	// Update Data of Touch Sensor
	g_Sensor.touch = ecrobot_get_touch_sensor(TOUCH_SENSOR);


	//--------------------------------
	//	Motor Data
	//--------------------------------
	g_Sensor.count_left = nxt_motor_get_count(LEFT_MOTOR);
	g_Sensor.count_right = nxt_motor_get_count(RIGHT_MOTOR);
	g_Sensor.count_tail = nxt_motor_get_count(TAIL_MOTOR);

	//--------------------------------
	//	battery Data
	//--------------------------------
	g_Sensor.battery = ecrobot_get_battery_voltage();

	//==========================================
	//	calculation
	//==========================================
	
	//calculate light valiance
	//g_Sensor.light_V = 1.0*calc_variance(g_Sensor.light_buffer,V_LIGHT_BUFFER_LENGTH);
	
	//calculate gyro valiance
	//g_Sensor.gyro_V = 1.0*calc_variance(g_Sensor.gyro_buffer,V_GYRO_BUFFER_LENGTH);


	//Bottle Detecting
	if(g_EventStatus.bottle_right_length > 0)
	{
		if(g_Sensor.distance < g_EventStatus.bottle_right_length)
		{
			g_Sensor.bottle_is_right = ON;
		}
	}
	if(g_EventStatus.bottle_left_length > 0)
	{
		if(g_Sensor.distance < g_EventStatus.bottle_left_length)
		{
			g_Sensor.bottle_is_left = ON;
		}
	}

	//==========================================
	//  Termination of Task
	//==========================================
	TerminateTask();
}

/*
###################################################################
	Task
	name: TaskLogger
	description:
###################################################################
*/
TASK(TaskLogger)
{
	//==========================================
	//  Calculation for logging
	//==========================================
	S8 ang = g_Sensor.count_right - g_EventStatus.start_pivot_turn_encoder_R - g_EventStatus.target_pivot_turn_angle_R;
	int rest_motor_count = (g_Sensor.count_left + g_Sensor.count_right) / 2 - g_EventStatus.start_motor_count - g_EventStatus.target_motor_count;
	
	//==========================================
	//  Select log type
	//==========================================
	switch(g_LogType)
	{
		case LOG_STATE:
			ecrobot_bt_data_logger( (S8)getCurrentState(), 0 );
			break;

		case LOG_TURN:
			ecrobot_bt_data_logger( (S8)g_Controller.turn, 0 );
			break;

		case LOG_PWM:
			ecrobot_bt_data_logger( (S8)g_g_pwm_L, (S8)g_g_pwm_R );
			break;

		case LOG_TARGET_ANGLE:
			ecrobot_bt_data_logger( (S8)(ang/100), (S8)(ang%100) );
			break;

		case LOG_MOTOR_COUNT:
			ecrobot_bt_data_logger( (S8)(rest_motor_count%100), (S8)(rest_motor_count/100) );
			break;

		case LOG_SONAR:
			ecrobot_bt_data_logger( (S8)(g_Sensor.distance%100), (S8)(g_Sensor.distance/100) );
			break;

		case LOG_DT:
			ecrobot_bt_data_logger( (S8)(g_Sensor.bottle_is_right), (S8)(g_Sensor.bottle_is_left) );
			break;

		case LOG_BALANCE_TAIL:
			if( g_Controller.PIDmode == BALANCE )
			{
				ecrobot_bt_data_logger( (S8)OFF, (S8)ON );
			}
			else if( g_Controller.PIDmode == TAIL )
			{
				ecrobot_bt_data_logger( (S8)ON, (S8)OFF );
			}
			else
			{
				ecrobot_bt_data_logger( (S8)OFF, (S8)OFF );
			}
			break;

		default:
			ecrobot_bt_data_logger( (S8)g_g_pwm_L, (S8)g_g_pwm_R );
			break;

	}

		//==========================================
		//  Termination of Task
		//==========================================
		TerminateTask();
}

/****************************************************************************************************************************
	Function
****************************************************************************************************************************/
/*
===============================================================================================
	name: InitNXT
	Description: ??
 	Parameter: no
	Return Value: no
===============================================================================================
*/
void InitNXT()
{
	U8 i = 0;

	//==========================================
	//	StateMachine
	//==========================================
	InitStateMachine();

	//==========================================
	//	Global variables
	//==========================================
	g_CalibCnt = 0;
	g_CalibGyroSum = 0;
	g_CalibLightSum = 0;
	g_CalibFlag = OFF;

	g_SonarCnt = 0;
	g_LightCnt = 0;
	g_LightAve = 0;
	for(i=0;i<LIGHT_BUFFER_LENGTH_MAX;i++)
	{
		g_LightBuffer[i] = 0;
	}

	//==========================================
	//	Motor & balancer
	//==========================================
	balance_init();
	nxt_motor_set_speed( LEFT_MOTOR, 0, 0);
	nxt_motor_set_speed( RIGHT_MOTOR, 0, 0);
	nxt_motor_set_speed( TAIL_MOTOR, 0, 0);
	nxt_motor_set_count( RIGHT_MOTOR, 0);
	nxt_motor_set_count( LEFT_MOTOR, 0);
	nxt_motor_set_count( TAIL_MOTOR, 0);

	//==========================================
	//	Sensor variables
	//==========================================
	g_Sensor.light = 600;
	//g_Sensor.pre_light = 600;
	g_Sensor.black = 800;
	g_Sensor.white = 400;
	g_Sensor.target_gray = 600;
	g_Sensor.target_gray_base = g_Sensor.target_gray;
	//g_Sensor.threshold_gray = g_Sensor.target_gray;

	g_Sensor.gyro = 600;
	g_Sensor.gyro_offset = 610;
	g_Sensor.gyro_offset_base = g_Sensor.gyro_offset;

	g_Sensor.touch = OFF;
	g_Sensor.distance = 255;
	g_Sensor.count_left = 0;
	g_Sensor.count_right = 0;
	g_Sensor.battery = 600;

	g_Sensor.bottle_is_left = OFF;
	g_Sensor.bottle_is_right = OFF;


	//==========================================
	//	Controller variables
	//==========================================
	//g_Controller.speed = 0;
	g_Controller.forward = 0;
	g_Controller.turn = 0;
	g_Controller.StandMode = NO_STAND_MODE;
	g_Controller.PIDmode = NO_PID_MODE;
	g_Controller.target_tail = 0;
	//g_Controller.tail_run_speed = 0;
	g_Controller.step_offset = 10000;
	g_Controller.gray_offset = 10000;
	g_Controller.color_threshold = 660;
	g_Controller.P_gain = 1.0;
	g_Controller.I_gain = 0.0;
	g_Controller.D_gain = 0.0;

	g_Controller.TP_gain = 0.5;
	g_Controller.TD_gain = 1.0;

	g_Controller.color_threshold = g_Sensor.target_gray;

	//g_Sensor.prev_light_value = g_Controller.color_threshold;
/*
	for(int m=0 ; m < g_Sensor.LIGHT_BUFFER_LENGTH ; m++)
	{
		g_Sensor.light_buffer[m]=g_Sensor.target_gray;
	}
	for(int m=0;m<g_Sensor.GYRO_BUFFER_LENGTH;m++)
	{
		g_Sensor.gyro_buffer[m] = g_Controller.gyro_offset;
	}
*/
}

/*
===============================================================================================
	name: EventSensor
	Description: ??
		Event
		ID description
		00	transition with no event
		01	touch
		02	white (not correctly implemented)
		03	black (not correctly implemented)
		04	gray marker
		05	step
		06	seesaw tilts
		07	dropped from the seesaw
		08	sonar sensor
		09	set time is up
		10 reach the set motor encoder count
		11 receive the bluetooth start signal
		12 finish circling
		13 (some event1)
		14 (some event2)
 	Parameter: no
	Return Value: no
===============================================================================================
*/
State_t EventSensor(){

	State_t tmp;

	//--------------------------------
	//	Event:auto
	//--------------------------------
	tmp = setNextState(AUTO);

	//--------------------------------
	//	Event:touch
	//--------------------------------
	if(g_Sensor.touch == ON && g_EventStatus.touch_status == OFF)
	{
		tmp = setNextState(TOUCH);
		g_EventStatus.touch_status = ON;
	}
	else if(g_Sensor.touch == OFF && g_EventStatus.touch_status == ON)
	{
		g_EventStatus.touch_status = OFF;
	}

	//==========================================
	//	black & white
	//==========================================
	if(g_Sensor.light > (g_Sensor.black - 50) && g_EventStatus.light_status != LIGHT_STATUS_BLACK)
	{
		//--------------------------------
		//	Event:black
		//--------------------------------
		tmp = setNextState(BLACK);
		g_EventStatus.light_status = LIGHT_STATUS_BLACK;
	}
	else if(g_Sensor.light < (g_Sensor.white + 50) && g_EventStatus.light_status != LIGHT_STATUS_WHITE)
	{
		//--------------------------------
		//	Event:white
		//--------------------------------
		tmp = setNextState(WHITE);
		g_EventStatus.light_status = LIGHT_STATUS_WHITE;
	}
	else
	{
		g_EventStatus.light_status = LIGHT_STATUS_GRAY;
	}

	//--------------------------------
	//	Event:gray marker
	//--------------------------------
	//if( g_Sensor.light_ave > g_Controller.gray_offset ){
	if( g_Controller.gray_offset - 10 < g_Sensor.light_ave && g_Sensor.light_ave < g_Controller.gray_offset + 10  )
	{
		tmp = setNextState(GRAY_MARKER);
		g_Controller.PIDmode = WG_PID;
	}
	else
	{
		g_Controller.PIDmode = WB_PID;
	}

	//--------------------------------
	//	Event:step
	//--------------------------------
	if( abs(g_Sensor.gyro - g_Sensor.gyro_offset) > g_Controller.step_offset	)
	{
		tmp = setNextState(STEP);
	}

	//--------------------------------
	//	Event:sonar
	//--------------------------------
   	if(g_Sensor.distance < g_EventStatus.target_distance )
   	{
   		tmp = setNextState(SONAR);
	}

	//--------------------------------
	//	Event:timer
	//--------------------------------
	if( (systick_get_ms() - g_EventStatus.start_time) > g_EventStatus.target_time && g_EventStatus.timer_flag == ON)
	{
		tmp = setNextState(TIMER);
		g_EventStatus.target_time = 0;
		g_EventStatus.start_time = 0;
		g_EventStatus.timer_flag = OFF;
	}

	//--------------------------------
	//	Event:motor count
	//--------------------------------
	int motor_count = (g_Sensor.count_left + g_Sensor.count_right) / 2;
	//int motor_count = (nxt_motor_get_count(LEFT_MOTOR) + nxt_motor_get_count(RIGHT_MOTOR)) / 2;
	if(abs(motor_count - g_EventStatus.start_motor_count) > abs(g_EventStatus.target_motor_count) && g_EventStatus.motor_counter_flag == ON )
	{
		tmp = setNextState(MOTOR_COUNT);
		g_EventStatus.target_motor_count = 0;
		g_EventStatus.start_motor_count = 0;
		g_EventStatus.motor_counter_flag = OFF;
	}

	//--------------------------------
	//	Event:bluetooth start
	//--------------------------------
	boolean bts = BluetoothStart();
	if(g_EventStatus.BTstart == OFF && bts == ON)
	{
		tmp = setNextState(BT_START);
		g_EventStatus.BTstart = ON;
	}
	else if(g_EventStatus.BTstart == ON && bts == OFF)
	{
		g_EventStatus.BTstart = OFF;
	}

	//--------------------------------
	//	Event:pivot turn
	//--------------------------------
	if( g_EventStatus.pivot_turn_flag == ON && abs(g_Sensor.count_right - g_EventStatus.start_pivot_turn_encoder_R) > g_EventStatus.target_pivot_turn_angle_R )
	{
		tmp = setNextState(PIVOT_TURN_END);
		g_EventStatus.pivot_turn_flag = OFF;
	}


	//==========================================
	//	drift turn
	//==========================================
	if(g_EventStatus.bottle_judge == ON)
	{
		//sendevent turn left or right

		if(g_Sensor.bottle_is_right == ON && g_Sensor.bottle_is_left == ON)
		{

		}
		else if(g_Sensor.bottle_is_right == ON && g_Sensor.bottle_is_left == OFF)
		{
			//--------------------------------
			//	Event:bottle is right
			//--------------------------------
			tmp = setNextState(BOTTLE_RIGHT);
		}
		else if(g_Sensor.bottle_is_right == OFF && g_Sensor.bottle_is_left == ON)
		{
			//--------------------------------
			//	Event:bottle is left
			//--------------------------------
			tmp = setNextState(BOTTLE_LEFT);
		}

		g_EventStatus.bottle_judge = ON;
	}

	return tmp;
}


/*
===============================================================================================
	name: serController
	Description: ??
		Action
		ID description
		00	do nothing
		01	balanced stop
		02	run at the balanced linetrace
		03	change the gyro offset
		04	change the gray threshold
		05	run with tail (do NOT linetrace)
		06	down the tail at speed 15
		07	NOT USED (currently same as action 3)
		08	set timer
		09	set motor encoder count
		10 set pid values
		11 NOT USED (currently almost same as action 5)
		12 set the offset to identify the step
		13 up the tail at speed -15
		14 run with the tail at the linetrace
		15 circling
		16 select the logger type
		17 set the gray marker offset
		18 run at the balanced (do NOT linetrace)
		19 (some action1)
		20 set sonar value
		21 look for bottle while turning right before drift turn
		22 look for bottle while turning left before dift turn
  	Parameter: no
	Return Value: no
===============================================================================================
*/
void serController(const State_t state)
{
	switch(state.action_no)
	{
		case DO_NOTHING://do nothing
			//g_Controller.speed = 0;
			g_Controller.forward = 0;
			g_Controller.turn = 0;
			//g_Controller.PIDmode = NO_MODE;
			//g_Controller.StandMode = BALANCE;
			break;

		case BALANCE_STOP://stop
			//g_Controller.speed = 0;
			g_Controller.forward = 0;
			g_Controller.turn = 0;

			//nxt_motor_set_speed(TAIL_MOTOR,0,1);
			g_Controller.PIDmode = WB_PID;
			g_Controller.StandMode = BALANCE;
			//balance_init();
			//nxt_motor_set_count(LEFT_MOTOR,0);
			//nxt_motor_set_count(RIGHT_MOTOR,0);
			break;

		// linetrace
		//@param foward:=value0
		//@param gyro_offset:=value1
		case BALANCE_LINETRACE:
			//g_Controller.speed = state.value0;
			g_Controller.forward = state.value0;
			g_Sensor.gyro_offset = g_Sensor.gyro_offset_base + state.value1;

			g_Controller.PIDmode = WB_PID;
			g_Controller.StandMode = BALANCE;


			//g_Sensor.target_gray = g_Sensor.threshold_gray;
			//nxt_motor_set_speed(TAIL_MOTOR,0,1);

			break;

		//change the gray threshold
		//@param new threshold:=value0
		case CHANGE_GRAY:
			//g_Controller.color_threshold=state.value0;
			//g_Sensor.gray=state.value0;
			//g_Sensor.gray = g_Sensor.calib_gray;
			g_Sensor.target_gray = g_Sensor.target_gray_base + state.value0;

			//g_Controller.PIDmode = WB_PID;
			//g_Controller.speed = 0;
			//g_Controller.forward_power = 0;
			//g_Controller.turn = 0;
			break;

		//run with no linetrace without balance
		//@param target_tail:=value0
		//@param tail_run_speed :=value1
		//@param turn :=value2
		case TAIL_RUN_FREEDOM:
			g_Controller.target_tail = state.value0;
			//g_Controller.tail_run_speed = state.value1;
			g_Controller.forward = state.value1;
			g_Controller.turn = state.value2;
			g_Controller.TP_gain = state.value3;

			g_Controller.PIDmode = NO_PID_MODE;
			g_Controller.StandMode = TAIL;
			break;

		//set timer
		//@param limit_timer:=value0 i.e. 20 = 2.0sec
		case TIMER_SET:
			g_EventStatus.timer_flag = ON;
			g_EventStatus.start_time = systick_get_ms();
			g_EventStatus.target_time = state.value0 * 100;

			break;
		//set motor count
		case MOTOR_SET:
			//g_EventStatus.start_motor_count = (nxt_motor_get_count(LEFT_MOTOR) + nxt_motor_get_count(RIGHT_MOTOR)) / 2;
			g_EventStatus.start_motor_count = (g_Sensor.count_left + g_Sensor.count_right) / 2;
			g_EventStatus.target_motor_count = state.value0;

			break;

		//set PID
		//@param P_gain:=value0/100
		//@param I_gain:=value1/100
		//@param D_gain:=value2/100
		case PID_SET:

			g_Controller.P_gain = (F32)state.value0 / 100;
			g_Controller.I_gain = (F32)state.value1 / 100;
			g_Controller.D_gain = (F32)state.value2 / 100;

			break;

		//set gyro offset for steps
		//@param step_offset := value0
		case STEP_OFFSET_SET:
			g_Controller.step_offset = state.value0;
			//g_Sensor.GYRO_BUFFER_LENGTH = state.value1;
			break;

		//up the tail
		case RAISE_TAIL:
			g_Controller.target_tail = 0;
			//nxt_motor_set_speed(TAIL_MOTOR,-15,1);
			break;

		// tail run
		//@param angle
		//@param speed
 		case TAIL_LINETRACE:
			g_Controller.PIDmode = WB_PID;
			g_Controller.StandMode = TAIL;
			g_Controller.target_tail = state.value0;
			//g_Controller.tail_run_speed = state.value1;
			g_Controller.forward = state.value1;
			g_Controller.TP_gain = state.value2;
			break;

		//circling
		//@param angle to turn
		case PIVOT_TURN:
			//g_Controller.tail_on=1;
			//g_Controller.balance_on=0;
			g_Controller.PIDmode = NO_PID_MODE;
			g_Controller.forward = 0;
			//g_Controller.tail_run_speed=1;
			/*if(g_Controller.balance_flag == ON)
			{
				g_Controller.turn = state.value1;
			}
			else
			{
				g_Controller.turn = -(state.value1);
			}*/

			if( state.value0 > 0 )
			{
				g_Controller.turn = state.value1;
			}
			else
			{
				g_Controller.turn = -(state.value1);
			}

			/*if(state.value0 < 0){
				g_Controller.turn *= -1;
			}*/
			g_EventStatus.start_pivot_turn_encoder_R = g_Sensor.count_right;
			g_EventStatus.target_pivot_turn_angle_R = calcAngle2Encoder(state.value0);
			g_EventStatus.pivot_turn_flag = ON;
			break;

		//selecting logger
		//@param log_type
		case SELECT_LOGTYPE:
			g_LogType = state.value0;
			break;

		//set the gray_market offset
		//@param gray_offset
		case GRAY_MARKER_OFFSET:
			g_Controller.gray_offset = state.value0;
			//g_Sensor.LIGHT_BUFFER_LENGTH = state.value1;

			break;

		//free balance
		case BALANCE_RUN_FREEDOM:
			g_Controller.PIDmode = NO_PID_MODE;
			g_Controller.StandMode = BALANCE;

			g_Controller.forward = state.value0;
			g_Controller.turn = state.value1;
			g_Sensor.gyro_offset = g_Sensor.gyro_offset_base + state.value2;

			//nxt_motor_set_speed(TAIL_MOTOR,0,1);

			break;

		//set_sonar_sensor
		case SONAR_SET:
			g_EventStatus.target_distance = state.value0;
			break;

		case SERACH_BOTTLE_RIGHT:
			g_Controller.forward = 0;

			g_EventStatus.bottle_right_length = state.value0;
			g_EventStatus.bottle_right_flag = ON;
			break;

		case SEARCH_BOTTLE_LEFT:
			g_Controller.forward = 0;

			g_EventStatus.bottle_left_length = state.value0;
			g_EventStatus.bottle_left_flag = ON;

			break;

		case SEARCH_BOTTLE_END:
			g_EventStatus.bottle_right_length = 0;
			g_EventStatus.bottle_left_length = 0;
			break;

		case SEARCH_BOTTLE_JUDGE:
			g_EventStatus.bottle_judge = ON;
			break;
		default:
			break;
	}
}


//****************************************
//return the Left and Right motor power
// from speed and turn for tail running
//***************************************
/*
void tail_run_turn2pwm(S16 tail_run_speed ,float turn ,S8 *_pwm_L, S8 *_pwm_R)
{
	if(tail_run_speed != 0)
	{
		if((tail_run_speed + turn) > 100)
		{
			float _turn_overflow = tail_run_speed + turn - 100;
			*_pwm_R = 100;
			*_pwm_L = (S8)(tail_run_speed - turn - turn_overflow);

		}
		else if(_tail_run_speed-_turn > 100)
		{
			float _turn_overflow = _tail_run_speed-_turn -100;
			*_pwm_L = 100;
			*_pwm_R = (S8)(_tail_run_speed+_turn-_turn_overflow);
		}
		else
		{
			*_pwm_R = (S8)(_tail_run_speed+_turn);
			*_pwm_L = (S8)(_tail_run_speed-_turn);
		}

		/////
	//	*_pwm_L = (S8)(_tail_run_speed+_turn);
	//	*_pwm_R = (S8)(_tail_run_speed-_turn);
		//////
	}else{
		*_pwm_L = 0;
		*_pwm_R = 0;

	}

}
*/
/*
===============================================================================================
	name: calcAngle2Encoder
	Description: ??
===============================================================================================
*/
S16 calcAngle2Encoder(S16 ang){
	S16 ret = (S16)((F32)ang*16.3/8.1);
	if(ret < 0) ret *= -1;

	return ret;
}

/*
***************************************************************
	name:calc_variance
	description:

	Parameter:
		U16* buf: An array of sensor data.
		int _len: length of an array
	Return Value:
***************************************************************

S8 calc_variance(U16 *buf,int _len){
	//calculate light valiance
	long sum=0;
	long sumv=0;
	int ave=0;
	int squ=0;
	for(int k=0;k<_len;k++){

			if(buf[k]==0){return 0;}
			sum += buf[k];
			sumv +=buf[k]*buf[k];

	}
	ave = sum / _len;
	squ = sumv-sum*sum/_len;
	float V = (float)squ /((float)_len -1);
	//V *=10;
	//V /=5;  //may not need to divide by 5
	//int V =(float)sum/(float)_len*100;
	if(V>127){
		V=127;
	}else if(V<-128){
		V= -128;
	}

	return (S8)V;


}
*/

/*
===============================================================================================
	name: TailStand
	Description: ??
===============================================================================================
*/
void TailStand(){
	g_Controller.target_tail = 90;
}