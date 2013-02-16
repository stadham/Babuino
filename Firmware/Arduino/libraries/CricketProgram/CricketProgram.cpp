//------------------------------------------------------------------------------
// This software was written for the Babuino Project
// Author: Murray Lang
// 
// Though the structure of the code has changed considerably, it owes its 
// existence to the the Babuino project AVR source code v033 by:
//           Adeilton Cavalcante de Oliveira Jr
// ...and the core code is much as Adeilton wrote it.
// 
// This version of the firmware was developed to run as a pure Arduino sketch.
// The reasons for this effort were:
//		* so that it can run with no (or little) modification on any Arduino
//			board, AVR, PIC, ARM or otherwise (assuming code space).
//		* so that developers can take maximum advantage of the available 
//			libraries and shields.
// 
// To this end I have avoided direct port manipulation and reprogramming of
// timers.
// 
// All rights to my portions of this code are in accordance with the Babuino 
// Project. 
//------------------------------------------------------------------------------

#include "CricketProgram.h"

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
bool 
CricketProgram::setup()
{
	Serial.begin(9600);
	eepromWriteByte(0x01F0,0);	// Starting address should be 0 for now.
	eepromWriteByte(0x01F1,0);
	
	_motors.setup();
	
	_switches.setuprun();
	setupuserLed();
	setuppiezoBeeper();
	
	double_beep();
	return true;
}

//------------------------------------------------------------------------------
// An adaptation of the loop in main() in the original Babuino code.
// This is intended to be called from within the Arduino loop() function.
//------------------------------------------------------------------------------
void 
CricketProgram::loop()
{
	debounce();
	switch (_states.getMachineState())
		{
		case READY:
			if (serialAvailable() > 0)
			{
				_states.setMachineState(COMM);
			}
			else if (_states.getRunRequest() == RUNNING)
			{
				
				_states.setMachineState(RUN);
			}
			break;

		case COMM:
			doComm();
			_states.setMachineState(READY);
			break;

		case RUN:
			_address.highByte = eepromReadByte(0x01F0); // TO DO: get this EEPROM address in a portable way
			_address.lowByte = eepromReadByte(0x01F1); // TO DO: get this EEPROM address in a portable way
			_stack.reset();
			_lstack.reset();
			code_exec();
				// Turn Motors off
			_motors.off();
			_states.setMachineState(READY);
			break;
		}
}

//------------------------------------------------------------------------------
// This is for running the object outside of the usual Arduino environment.
//------------------------------------------------------------------------------
int
CricketProgram::run()
{
	while (true)
	{
		loop();
	}
	return 0;
}

//------------------------------------------------------------------------------
// An adaptation of a function of the same name in the original Babuino code.
//------------------------------------------------------------------------------
void
CricketProgram::doComm()
{
	byte				cmd = 0;
	byte				remainingChars = 0;
	ShortUnion2Bytes	byte_count = {0};

	_states.setWaitingCmd(true); 		// set the waiting flag
	_states.setCommState(COMM_STARTED);

	while (_states.getCommState() != COMM_FINISHED)
	{
			// The Cricket host expects all characters to be echoed, because
			// this was done by the infrared device used for communication
			// with the Cricket robot.
		char temp = serialRead();
		serialWrite(temp);
		
		if(_states.isWaitingCmd())
		{
			cmd = temp;
				// test received command and set number of bytes to read
			switch(cmd)
			{
			case cmdCricketCheck:
				remainingChars = 1;	// remaining character received should be 0 (not '0')
				_states.setWaitingCmd(false);
				break;

			case cmdSetPointer:
			case cmdReadBytes:
			case cmdWriteBytes:
				remainingChars = 2;
				_states.setWaitingCmd(false);
			break;

			case cmdRun:
				_states.setRunRequest(RUNNING);
				_states.setCommState(COMM_FINISHED);
			break;

			default:
				_states.setCommState(COMM_FINISHED);	// should never get here.
				break;
			}
		}
		else
		{
				// receive bytes corresponding to each command
			_states.setWaitingCmd(false);
			switch(cmd)
			{
			case cmdCricketCheck:
				if (remainingChars == 1)
					serialWrite(cmdCricketCheckACK);

				_states.setCommState(COMM_FINISHED);	// should never get here
				break;


			case cmdSetPointer:
				if (remainingChars == 2)
				{
					remainingChars--;
					_address.highByte = temp;
				}
				else
				{
					remainingChars--;
					_address.lowByte = (unsigned char)temp;
					_states.setCommState(COMM_FINISHED);
				}
				break;

			case cmdWriteBytes:
				if (remainingChars == 2)
				{
					remainingChars--;
					byte_count.highByte = temp;
				}
				else if (remainingChars == 1)
				{
					remainingChars--;
					byte_count.lowByte = (unsigned char)temp;
				}
				else
				{
					serialWrite(~temp);
					eepromWriteByte((int)_address, temp);
					++_address;
					--byte_count;
					if (byte_count == 0)
					{
						_states.setCommState(COMM_FINISHED);
					}
				}
				break;

			case cmdReadBytes:
				if (remainingChars == 2)
				{
					remainingChars--;
					byte_count.highByte = temp;
				}
				else if (remainingChars == 1)
				{
					remainingChars--;
					byte_count.lowByte = (unsigned char)temp;
						// Once byte_count is received, start sending characters.
					while (byte_count > 0)
					{
						serialWrite(eepromReadByte((int)_address));
						++_address;
						--byte_count;
					}
					_states.setCommState(COMM_FINISHED);
				}
				break;

			default:
				_states.setCommState(COMM_FINISHED);	// should never get here
				break;
			}
		}
	}
}

//------------------------------------------------------------------------------
// An adaptation of a function of the same name in the original Babuino code.
//------------------------------------------------------------------------------
void
CricketProgram::code_exec()
{
	int temp1, temp2, temp3;
	int localFlag = 0;		// currently used only for WAITUNTIL
							// 0 => initial entry
	char opcode = 0;

	while (_states.getRunRequest() == RUNNING)
	{
		opcode = eepromReadByte((int)_address);
		
			// Using prefix operator because of arcane C++ problems with 
			// overloading the postfix ++ operator in ShortUnion2Bytes.
		++_address; 

		switch (opcode)
		{
			case OP_BYTE:
				_stack.push(eepromReadByte((int)_address));
				++_address;
				break;

//==================== Added 1/11/10 ===============================
			case OP_NUMBER:
				temp1 = eepromReadByte((int)_address);  // MSB
				++_address;
				temp2 = eepromReadByte((int)_address);  // LSB
				++_address;
				temp3 = (temp1<<8) + (temp2&255);
				_stack.push(temp3);
				break;
//====================End of Add ===================================
			case OP_LIST:
				_stack.push((int)_address + 1);
				temp1 = eepromReadByte((int)_address);
				_address += temp1;
				break;

			case OP_EOL:
//	20090919	address++;
				break;

			case OP_EOLR:
//				address++;
				break;

			case OP_LTHING:
				_stack.push(_lstack.at((int)eepromReadByte((int)_address)));
				++_address;
				break;

			case OP_STOP:
				_address.asShort = _stack.pop();
				_lstack.reset();
				if (_address < 0)	// stack has underflowed
					_states.setRunRequest(STOPPED);
				break;

			case OP_OUTPUT:
				temp1 = _stack.pop();
				_address.asShort = _stack.pop();
				_lstack.reset();
				_stack.push(temp1);
				break;

			case OP_REPEAT:
				temp1 = _stack.pop(); // temp1 = list initial address
				temp2 = _stack.pop(); // temp2 = argument to test
				if (temp2)
				{
					_address.asShort = temp1;
					temp2--;
					_stack.push(temp2);
					_stack.push(temp1);
				}
				break;



			case OP_IF:
				temp1 = _stack.pop(); // temp1 = list initial address
				temp2 = _stack.pop(); // temp2 = argument to test
				if (temp2)
				{
					_address.asShort = temp1;
					temp2--;
					_stack.push(temp2);
					_stack.push(temp1);
				}
				break;

// IFELSE starts with address of THEN and ELSE lists (and CONDITIONAL)
//  on the stack. CONDITIONAL is tested and appropriate list is run. The
//	DONE flag (0x0fff) is pushed instead of THEN list address. When IFELSE
//  is again encountered, DONE is detected and execution falls thru.

			case OP_IFELSE:
				temp1 = _stack.pop(); // temp1 = ELSE list start
				temp2 = _stack.pop(); // temp2 = THEN list start
				temp3 = _stack.pop(); // temp3 = argument to test
				if (temp2 != 0x0fff)	// check for done flag
				{
					if (temp3)
					{
						_address.asShort = temp2; // the "THEN" list
						temp2 = 0x0fff;	// set the done flag
						_stack.push(temp3);
						_stack.push(temp2);
// No need to push temp1 here since it will be pushed when ELSE list is
//	encountered after THEN runs.
					}
					else
					{
						_address.asShort = temp1;	// the "ELSE" list
						temp2 = 0x0fff;	// set the done flag
						_stack.push(temp3);
						_stack.push(temp2);
						_stack.push(temp1);
					}
				}
				break;


			case OP_BEEP:
				//lcd.print("Beep!          ");
				beep();
				break;

//==================== Modded 1/11/10 ===============================
			// localFlag must be 0 on initial entry (and on exit)
			// It is initialized on entry to code_exec.
			case OP_WAITUNTIL:
				temp1 = _stack.pop(); // temp1 = address of test clause (first time)
								 // temp1 = truth value of conditional (subsequent times)
				if (localFlag == 0x01ff)	// all times but the first
				{
					temp2 = _stack.pop(); // temp2 = address of test clause
					if (!temp1)
					{
						_address.asShort = temp2;		// the test clause
						_stack.push(temp2); 			// save the test clause
					}
					else
					{
						localFlag = 0;	// done, clean up and continue
					}
				}
				else		// come here first time only
				{
					localFlag = 0x01ff;				// set the flag to test conditional
					_address.asShort = temp1;		// the test clause
					_stack.push(temp1); 			// save the test clause
				}
				break;
//====================End of Mod ===================================

			case OP_LOOP:
				temp1 = _stack.pop(); // temp1 = list initial address
				_address.asShort = temp1;
				_stack.push(temp1);
				break;


			case OP_WAIT:
				delay(100*_stack.pop());
				break;

//==================== Added 1/11/10 ===============================
			case OP_TIMER:
				
				_stack.push(_timerCount);
				break;

			case OP_RESETT:
				_timerCount = 0;
				break;
//====================End of Add ===================================

			case OP_SEND:
				serialWrite(_stack.pop());
				delay(100);
				break;

// ================== BEGIN UPDATE 20100519 ========================
			case OP_IR:
				temp1 = serialRead();
				_stack.push(temp1);
				delay(100);
				break;

			case OP_NEWIR:
				temp1 = serialAvailable();
				_stack.push(temp1);
				delay(100);
				break;

// ==================== END UPDATE 20100519 ========================

			case OP_RANDOM:
				_stack.push(temp1 = rand());
				break;

			case OP_PLUS:
				_stack.push(temp1 = _stack.pop()+_stack.pop());
				break;

			case OP_MINUS:
				temp2 = _stack.pop();
				temp1 = _stack.pop();
				_stack.push(temp1 -= temp2);
				break;

			case OP_MUL:
				_stack.push(temp1 = _stack.pop()*_stack.pop());
				break;

			case OP_DIV:
				temp2 = _stack.pop();
				temp1 = _stack.pop();
				_stack.push(temp1 = temp1/temp2);
				break;

			case OP_REMAIN_DIV:
				temp2 = _stack.pop();
				temp1 = _stack.pop();
				_stack.push(temp1 = temp1 % temp2);
				break;


			case OP_EQUAL:
				temp2 = _stack.pop();
				temp1 = _stack.pop();
				_stack.push((temp1 == temp2));
				break;


			case OP_GREATER_THAN:
				temp2 = _stack.pop();
				temp1 = _stack.pop();
				_stack.push((temp1 > temp2));
				break;


			case OP_LESS_THAN:
				temp2 = _stack.pop();
				temp1 = _stack.pop();
				_stack.push((temp1 < temp2));
				break;


			case OP_AND:
				temp2 = _stack.pop();
				temp1 = _stack.pop();
				_stack.push((temp1 & temp2));
				break;


			case OP_OR:
				temp2 = _stack.pop();
				temp1 = _stack.pop();
				_stack.push((temp1 | temp2));
				break;


			case OP_XOR:
				temp2 = _stack.pop();
				temp1 = _stack.pop();
				_stack.push((temp1 ^ temp2));
				break;


			case OP_NOT:
				_stack.push(temp1 = !_stack.pop());
				break;

			case OP_SETGLOBAL:
				temp1 = _stack.pop();	// Value of variable
				temp2 = _stack.pop();	// Location of variable
				_variables[temp2] = temp1;
				break;

			case OP_GLOBAL:
				_stack.push(_variables[_stack.pop()]);
				break;
/*
			case RECORD:
				local_temp = pop16();
				modbus_memory[modbus_data_ptr] = local_temp;
				modbus_data_ptr++;
			break;


			case RECALL:
				local_temp = modbus_memory[modbus_data_ptr];
				push16(local_temp);
				modbus_data_ptr++;
			break;


			case RESETDP:
				modbus_data_ptr = 0;
			break;


			case SETDP:
				local_temp = pop16();
				modbus_data_ptr = local_temp;
			break;


			case ERASE:
				local_temp = 0;
				modbus_memory[modbus_data_ptr] = local_temp;
				modbus_data_ptr++;
			break;

*/
			case OP_SEL_A:
				_selectedMotors = MotorShield::MOTOR_A;
				break;
			case OP_SEL_B:
				_selectedMotors = MotorShield::MOTOR_B;
				break;
			case OP_SEL_AB:
				_selectedMotors = MotorShield::MOTOR_AB;
				break;


			case OP_THISWAY:
				temp1 = _stack.pop();	// Motor selection
				_motors.setDirection((MotorShield::Selected)(temp1), MotorBase::THIS_WAY);
				break;


			case OP_THATWAY:
				temp1 = _stack.pop();	// Motor selection
				_motors.setDirection((MotorShield::Selected)(temp1), MotorBase::THAT_WAY);

				break;

			case OP_RD:
				temp1 = _stack.pop();	// Motor selection
				_motors.reverseDirection((MotorShield::Selected)(temp1));
				break;
				
			case OP_SETPOWER:
				temp1 = _stack.pop();	// Motor selection
				temp2 = _stack.pop(); 
				if (temp2 > 7)	
					temp2 = 7;
				_motors.setPower((MotorShield::Selected)(temp1), (byte)temp2);
				break;


			case OP_BRAKE:
				temp1 = _stack.pop();	// Motor selection
				_motors.setBrake((MotorShield::Selected)(temp1), MotorBase::BRAKE_ON);
				break;

			case OP_ON:
				temp1 = _stack.pop();	// Motor selection
				_motors.on((MotorShield::Selected)(temp1));
				break;

			case OP_ONFOR:
				temp1 = _stack.pop();	// Motor selection
				_motors.on((MotorShield::Selected)(temp1));
				delay(100*_stack.pop());
				_motors.off((MotorShield::Selected)(temp1));
				break;

			case OP_OFF:
				temp1 = _stack.pop();	// Motor selection
				_motors.off((MotorShield::Selected)(temp1));
				break;
			
/* Only two motors supported at present unfortunately
			case SEL_C:
				motorMask = motorMask_C;
			break;
			case SEL_D:
				motorMask = motorMask_D;
			break;
			case SEL_CD:
				motorMask = motorMask_C;
				motorMask |= motorMask_D;
			break;
			case SEL_ABCD:
				motorMask = motorMask_A;
				motorMask |= motorMask_B;
				motorMask |= motorMask_C;
				motorMask |= motorMask_D;
			break;
*/



			case OP_SENSORB:
				_stack.push(_sensors.B());
				break;


/*
			case SENSORA:	// SENSOR x FOR INEX MODE
				temp2 = INEX_MODE;
				if (interpreter_mode == temp2)
				{
					temp1 = pop16();
				}
				else
				{
					temp1 = 0;
				}
				push16(adc_read(temp1));
			break;
*/
			case OP_SENSORA:
				_stack.push(_sensors.A());
				break;


		/*
			case SWITCHA:
				temp2 = INEX_MODE;
				if (interpreter_mode == temp2)
				{
					temp1 = pop16();
				}
				else
				{
					temp1 = 0;
				}
				push16(adc_read(0)>>7);
			break;
			*/
			case OP_SWITCHA:
				_stack.push(_sensors.A()>>7);
				break;


			case OP_SWITCHB:
				_stack.push(_sensors.B()>>7);
				break;

			case OP_STOP1:

				_states.setRunRequest(STOPPED);
//				commStatus = COMM_IDLE;
				break;

/*
			case BABUINO_01:
				beep();
				break;


			case BABUINO_02:
				beep();
				beep();
				break;


			case BABUINO_03:
				beep();
				beep();
				beep();
				break;
			*/
			case OP_SENSORC:
				_stack.push(_sensors.C());
				break;

			case OP_SENSORD:
				_stack.push(_sensors.D());
				break;

			case OP_SWITCHC:
				_stack.push(_sensors.C()>>7); //push16(adc_read(2)>>7);
				break;

			case OP_SWITCHD:
				_stack.push(_sensors.D()>>7); //push16(adc_read(3)>>7);
				break;

			case OP_LEDON:
				userLed(true);	// set the correct bit
				break;

			case OP_LEDOFF:
				userLed(false);
				break;

			default:
					// I think Procedure Call should go here?
					/* Experimental Move into Switch*/
				if ((temp1 = opcode & 0x80))
				{
					_stack.push((int)_address+1);
					_address.asShort = ((opcode & 0x7F) + eepromReadByte((int)_address));
		//			arg_num = code[address];
		//			temp2 = arg_num;
					temp2 = eepromReadByte(_address);
					if (temp2)			// handles proc arguments if any
					{
						temp1 = _stack.pop();
						while (temp2)
						{
							_lstack.push(_stack.pop());
							temp2--;
						}
						_stack.push(temp1); //push16(temp1);
					}
					++_address;
					temp2 = (int)_address;
				}
		/** End Experiment */
				else
				{
					beep();	// let's get an indication for now.
				}
				break;

		}
	}
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
void 
CricketProgram::beep() const
{
	piezoBeeper(127);
	delay(50);
	piezoBeeper(0);
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
void 
CricketProgram::double_beep() const
{
	beep();
	delay(50);
	beep();
}
