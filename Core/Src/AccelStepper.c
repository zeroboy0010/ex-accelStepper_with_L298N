#include <math.h>
#include "AccelStepper.h"
#include <stdio.h>
#include "main.h"

#ifdef DEBUG_MODE
//Some debugging assistance
void dump(uint8_t* p, int l)
{
    int i;

    for (i = 0; i < l; i++)
    {
			printf("%20X", p[i]);
			printf(" ");
    }
    printf("\n");
}
#endif

void moveTo(Stepper_t* motor, long absolute)
{
    if (motor->_targetPos != absolute)
    {
    	motor->_targetPos = absolute;
		computeNewSpeed(motor);
		// compute new n?
    }
}

void move(Stepper_t* motor, long relative)
{
    moveTo(motor, motor->_currentPos + relative);
}

// Implements steps according to the current step interval
// You must call this at least once per step
// returns true if a step occurred
uint8_t runSpeed(Stepper_t* motor)
{
    // Dont do anything unless we actually have a step interval
    if (!motor->_stepInterval) return 0; // false

	unsigned long time = HAL_GetTick() * 10; //Arduino: micros();
	unsigned long nextStepTime = motor->_lastStepTime + motor->_stepInterval;

	// Gymnastics to detect wrapping of either the nextStepTime and/or the current time
	if (((nextStepTime >= motor->_lastStepTime) && ((time >= nextStepTime) || (time < motor->_lastStepTime)))
	|| ((nextStepTime < motor->_lastStepTime) && ((time >= nextStepTime) && (time < motor->_lastStepTime))))
	{
		if (motor->_direction == DIRECTION_CW)
		{
			// Clockwise
			motor->_currentPos += 1;
		}
		else
		{
			// Anticlockwise
			motor->_currentPos -= 1;
		}

		step(motor, motor->_currentPos);

		motor->_lastStepTime = time;

		return 1; // true
    }
    else
    {
    	return 0; // false
    }
}

long distanceToGo(Stepper_t* motor)
{
    return (motor->_targetPos - motor->_currentPos);
}

long targetPosition(Stepper_t* motor)
{
    return motor->_targetPos;
}

long currentPosition(Stepper_t* motor)
{
    return motor->_currentPos;
}

// Useful during initialisations or after initial positioning
// Sets speed to 0
void setCurrentPosition(Stepper_t* motor, long position)
{
	motor->_targetPos = motor->_currentPos = position;
	motor->_n = 0;
	motor->_stepInterval = 0;
	motor->_speed = 0.0;
}

void computeNewSpeed(Stepper_t* motor)
{
    long distanceTo = distanceToGo(motor); // +ve is clockwise from curent location

    long stepsToStop = (long)((motor->_speed * motor->_speed) / (2.0 * motor->_acceleration)); // Equation 16

    if (distanceTo == 0 && stepsToStop <= 1)
    {
    	// We are at the target and its time to stop
    	motor->_stepInterval = 0;
    	motor->_speed = 0.0;
    	motor->_n = 0;
    	return;
    }

    if (distanceTo > 0)
    {
		// We are anticlockwise from the target
		// Need to go clockwise from here, maybe decelerate now
		if (motor->_n > 0)
		{
			// Currently accelerating, need to decel now? Or maybe going the wrong way?
			if ((stepsToStop >= distanceTo) || motor->_direction == DIRECTION_CCW)
				motor->_n = -stepsToStop; // Start deceleration
		}
		else if (motor->_n < 0)
		{
			// Currently decelerating, need to accel again?
			if ((stepsToStop < distanceTo) && motor->_direction == DIRECTION_CW)
				motor->_n = -motor->_n; // Start accceleration
		}
	}
	else if (distanceTo < 0)
	{
		// We are clockwise from the target
		// Need to go anticlockwise from here, maybe decelerate
		if (motor->_n > 0)
		{
			// Currently accelerating, need to decel now? Or maybe going the wrong way?
			if ((stepsToStop >= -distanceTo) || motor->_direction == DIRECTION_CW)
				motor->_n = -stepsToStop; // Start deceleration
		}
		else if (motor->_n < 0)
		{
			// Currently decelerating, need to accel again?
			if ((stepsToStop < -distanceTo) && motor->_direction == DIRECTION_CCW)
				motor->_n = -motor->_n; // Start accceleration
		}
	}

	// Need to accelerate or decelerate
	if (motor->_n == 0)
	{
		// First step from stopped
		motor->_cn = motor->_c0;
		motor->_direction = (distanceTo > 0) ? DIRECTION_CW : DIRECTION_CCW;
	}
	else
	{
		// Subsequent step. Works for accel (n is +_ve) and decel (n is -ve).
		motor->_cn = motor->_cn - ((2.0 * motor->_cn) / ((4.0 * motor->_n) + 1)); // Equation 13
		motor->_cn = (motor->_cn > motor->_cmin) ? motor->_cn : motor->_cmin; //max(motor->_cn, motor->_cmin);
	}

	motor->_n++;
	motor->_stepInterval = motor->_cn;
	motor->_speed = 1000000.0 / motor->_cn;

	if (motor->_direction == DIRECTION_CCW)
		motor->_speed = -motor->_speed;

#ifdef DEBUG_MODE
    printf("%f\n", motor->_speed);
    printf("%f\n", motor->_acceleration);
    printf("%f\n", motor->_cn);
    printf("%f\n", motor->_c0);
    printf("%ld\n", motor->_n);
    printf("%lu\n", motor->_stepInterval);
//    Serial.println(distanceTo);
//    Serial.println(stepsToStop);
#endif
}

// Run the motor to implement speed and acceleration in order to proceed to the target position
// You must call this at least once per step, preferably in your main loop
// If the motor is in the desired position, the cost is very small
// returns true if the motor is still running to the target position.
uint8_t run(Stepper_t* motor)
{
    if (runSpeed(motor)) computeNewSpeed(motor);

    return motor->_speed != 0.0 || distanceToGo(motor) != 0;
}

void InitStepper(Stepper_t* motor, uint8_t interface, uint16_t pin1, GPIO_TypeDef* GPIOxPin1, uint16_t pin2, GPIO_TypeDef* GPIOxPin2, uint16_t pin3, GPIO_TypeDef* GPIOxPin3, uint16_t pin4, GPIO_TypeDef* GPIOxPin4)
{
	motor->_interface = interface;
	motor->_currentPos = 0;
	motor->_targetPos = 0;
	motor->_speed = 0.0;
	motor->_maxSpeed = 1.0;
	motor->_acceleration = 0.0;
	motor->_sqrt_twoa = 1.0;
	motor->_stepInterval = 0;
	motor->_minPulseWidth = 1;
	motor->_GPIOxEnablePin = NULL;
	motor->_lastStepTime = 0;
	motor->_pin[0] = pin1;
	motor->_GPIOxPin[0] = GPIOxPin1;
	motor->_pin[1] = pin2;
	motor->_GPIOxPin[1] = GPIOxPin2;
	motor->_pin[2] = pin3;
	motor->_GPIOxPin[2] = GPIOxPin3;
	motor->_pin[3] = pin4;
	motor->_GPIOxPin[3] = GPIOxPin4;

    // NEW
	motor->_n = 0;
	motor->_c0 = 0.0;
    motor->_cn = 0.0;
    motor->_cmin = 1.0;
    motor->_direction = DIRECTION_CCW;

    int i;
    for (i = 0; i < 4; i++)
    	motor->_pinInverted[i] = 0;

    // Some reasonable default
    setAcceleration(motor, 1);
}

void InitStepperFunct(Stepper_t* motor, void (*forward)(), void (*backward)())
{
	motor->_interface = 0;
	motor->_currentPos = 0;
	motor->_targetPos = 0;
	motor->_speed = 0.0;
	motor->_maxSpeed = 1.0;
	motor->_acceleration = 0.0;
	motor->_sqrt_twoa = 1.0;
	motor->_stepInterval = 0;
	motor->_minPulseWidth = 1;
	motor->_enablePin = 0xff;
	motor->_GPIOxEnablePin = NULL;
	motor->_lastStepTime = 0;
	motor->_pin[0] = 0;
	motor->_GPIOxPin[0] = NULL;
	motor->_pin[1] = 0;
	motor->_GPIOxPin[1] = NULL;
	motor->_pin[2] = 0;
	motor->_GPIOxPin[2] = NULL;
	motor->_pin[3] = 0;
	motor->_GPIOxPin[3] = NULL;
	motor->_forward = forward;
	motor->_backward = backward;

    // NEW
	motor->_n = 0;
	motor->_c0 = 0.0;
	motor->_cn = 0.0;
	motor->_cmin = 1.0;
	motor->_direction = DIRECTION_CCW;

    int i;
    for (i = 0; i < 4; i++)
    	motor->_pinInverted[i] = 0;

    // Some reasonable default
    setAcceleration(motor, 1);
}

void setMaxSpeed(Stepper_t* motor, float speed)
{
    if (motor->_maxSpeed != speed)
    {
    	motor->_maxSpeed = speed;
    	motor->_cmin = 1000000.0 / speed;
		// Recompute _n from current speed and adjust speed if accelerating or cruising
		if (motor->_n > 0)
		{
			motor->_n = (long)((motor->_speed * motor->_speed) / (2.0 * motor->_acceleration)); // Equation 16
			computeNewSpeed(motor);
		}
    }
}

float maxSpeed(Stepper_t* motor)
{
    return motor->_maxSpeed;
}

void setAcceleration(Stepper_t* motor, float acceleration)
{
    if (acceleration == 0.0)
	return;
    if (motor->_acceleration != acceleration)
    {
	    // Recompute _n per Equation 17
    	motor->_n = motor->_n * (motor->_acceleration / acceleration);
		// New c0 per Equation 7, with correction per Equation 15
    	motor->_c0 = 0.676 * sqrt(2.0 / acceleration) * 1000000.0;// Equation 15
    	motor->_acceleration = acceleration;
		computeNewSpeed(motor);
    }
}

float constrain(float value, float minimum, float maximum)
{
	if(value < minimum) return minimum;
	else if(value > maximum) return maximum;
	else return value;
}

void setSpeed(Stepper_t* motor, float speed)
{
    if (speed == motor->_speed)
        return;

    speed = constrain(speed, -motor->_maxSpeed, motor->_maxSpeed);

    if (speed == 0.0)
    {
    	motor->_stepInterval = 0;
    }
    else
    {
    	motor->_stepInterval = fabs(1000000.0 / speed);
    	motor->_direction = (speed > 0.0) ? DIRECTION_CW : DIRECTION_CCW;
    }

    motor->_speed = speed;
}

float speed(Stepper_t* motor)
{
    return motor->_speed;
}

// Subclasses can override
void setOutputPins(Stepper_t* motor, uint8_t mask)
{
    uint8_t numpins = 2;
    if (motor->_interface == FULL4WIRE || motor->_interface == HALF4WIRE) numpins = 4;
    else if (motor->_interface == FULL3WIRE || motor->_interface == HALF3WIRE) numpins = 3;

	uint8_t i;
	for (i = 0; i < numpins; i++)
	{
		//Arduino: digitalWrite(motor->_pin[i], (mask & (1 << i)) ? (HIGH ^ motor->_pinInverted[i]) : (LOW ^ motor->_pinInverted[i]));
		if(mask & (1 << i))
		{
			if (0x11 ^ motor->_pinInverted[i]) HAL_GPIO_WritePin(motor->_GPIOxPin[i], motor->_pin[i], GPIO_PIN_SET);
			else HAL_GPIO_WritePin(motor->_GPIOxPin[i], motor->_pin[i], GPIO_PIN_RESET);
		}
		else
		{
			if (0x00 ^ motor->_pinInverted[i]) HAL_GPIO_WritePin(motor->_GPIOxPin[i], motor->_pin[i], GPIO_PIN_SET);
			else HAL_GPIO_WritePin(motor->_GPIOxPin[i], motor->_pin[i], GPIO_PIN_RESET);
		}
	}
}
void step(Stepper_t* motor, long step)
{
    switch (motor->_interface)
    {
        case FUNCTION:
            step0(motor, step);
            break;

		case DRIVER:
			step1(motor, step);
			break;

		case FULL2WIRE:
			step2(motor, step);
			break;

		case FULL3WIRE:
			step3(motor, step);
			break;

		case FULL4WIRE:
			step4(motor, step);
			break;

		case HALF3WIRE:
			step6(motor, step);
			break;

		case HALF4WIRE:
			step8(motor, step);
			break;
    }
}

// You might want to override this to implement eg serial output
// bit 0 of the mask corresponds to _pin[0]
// bit 1 of the mask corresponds to _pin[1]
// ....

void step0(Stepper_t* motor, long step)
{
	if (motor->_speed > 0)
		motor->_forward();
	else
		motor->_backward();
}

// 1 pin step function (ie for stepper drivers)
// This is passed the current step number (0 to 7)
// Subclasses can override
void step1(Stepper_t* motor, long step)
{
    // _pin[0] is step, _pin[1] is direction
    setOutputPins(motor, motor->_direction ? 0b10 : 0b00); // Set direction first else get rogue pulses
    setOutputPins(motor, motor->_direction ? 0b11 : 0b01); // step HIGH
    // Caution 200ns setup time
    // Delay the minimum allowed pulse width
    HAL_Delay(motor->_minPulseWidth / 100000); //delayMicroseconds(motor->_minPulseWidth);
    setOutputPins(motor, motor->_direction ? 0b10 : 0b00); // step LOW

}


// 2 pin step function
// This is passed the current step number (0 to 7)
// Subclasses can override
void step2(Stepper_t* motor, long step)
{
    switch (step & 0x3)
    {
		case 0: /* 01 */
			setOutputPins(motor, 0b10);
			break;

		case 1: /* 11 */
			setOutputPins(motor, 0b11);
			break;

		case 2: /* 10 */
			setOutputPins(motor, 0b01);
			break;

		case 3: /* 00 */
			setOutputPins(motor, 0b00);
			break;
    }
}
// 3 pin step function
// This is passed the current step number (0 to 7)
// Subclasses can override
void step3(Stepper_t* motor, long step)
{
    switch (step % 3)
    {
		case 0:    // 100
			setOutputPins(motor, 0b100);
			break;

		case 1:    // 010
			setOutputPins(motor, 0b010);
			break;

		case 2:    //001
			setOutputPins(motor, 0b001);
			break;
    }
}

// 4 pin step function for half stepper
// This is passed the current step number (0 to 7)
// Subclasses can override
void step4(Stepper_t* motor, long step)
{
    switch (step & 0x3)
    {
		case 0:    // 1010
			setOutputPins(motor, 0b0101);
			break;

		case 1:    // 0110
			setOutputPins(motor, 0b0110);
			break;

		case 2:    //0101
			setOutputPins(motor, 0b1010);
			break;

		case 3:    //1001
			setOutputPins(motor, 0b1001);
			break;
    }
}

// 3 pin half step function
// This is passed the current step number (0 to 7)
// Subclasses can override
void step6(Stepper_t* motor, long step)
{
    switch (step % 6)
    {
		case 0:    // 100
			setOutputPins(motor, 0b100);
			break;

		case 1:    // 110
			setOutputPins(motor, 0b110);
			break;

		case 2:    // 010
			setOutputPins(motor, 0b010);
			break;

		case 3:    // 011
			setOutputPins(motor, 0b011);
			break;

		case 4:    // 001
			setOutputPins(motor, 0b001);
			break;

		case 5:    // 101
			setOutputPins(motor, 0b101);
			break;
    }
}

// 4 pin half step function
// This is passed the current step number (0 to 7)
// Subclasses can override
void step8(Stepper_t* motor, long step)
{
    switch (step & 0x7)
    {
		case 0:    // 1000
			setOutputPins(motor, 0b1000);
			break;

		case 1:    // 1100
			setOutputPins(motor, 0b1100);
			break;

		case 2:    // 0100
			setOutputPins(motor, 0b0100);
			break;

		case 3:    // 0110
			setOutputPins(motor, 0b0110);
			break;

		case 4:    // 0010
			setOutputPins(motor, 0b0010);
			break;

		case 5:    //0011
			setOutputPins(motor, 0b0011);
			break;

		case 6:    // 0001
			setOutputPins(motor, 0b0001);
			break;

		case 7:    //1001
			setOutputPins(motor, 0b1001);
			break;
    }
}

void setMinPulseWidth(Stepper_t* motor, unsigned int minWidth)
{
	motor->_minPulseWidth = minWidth;
}
// Blocks until the target position is reached and stopped
void runToPosition(Stepper_t* motor)
{
    while (run(motor)) ;
}

uint8_t runSpeedToPosition(Stepper_t* motor)
{
    if (motor->_targetPos == motor->_currentPos)
    	return 0; // false
    if (motor->_targetPos > motor->_currentPos)
    	motor->_direction = DIRECTION_CW;
    else
    	motor->_direction = DIRECTION_CCW;

    return runSpeed(motor);
}

// Blocks until the new target position is reached
void runToNewPosition(Stepper_t* motor, long position)
{
    moveTo(motor, position);
    runToPosition(motor);
}

void stop(Stepper_t* motor)
{
    if (motor->_speed != 0.0)
    {
		long stepsToStop = (long)((motor->_speed * motor->_speed) / (2.0 * motor->_acceleration)) + 1; // Equation 16 (+integer rounding)

		if (motor->_speed > 0)
			move(motor, stepsToStop);

		else
			move(motor, -stepsToStop);
    }
}

uint8_t isRunning(Stepper_t* motor)
{
    return !(motor->_speed == 0.0 && motor->_targetPos == motor->_currentPos);
}
