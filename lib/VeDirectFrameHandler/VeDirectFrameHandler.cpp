/* framehandler.cpp
 *
 * Arduino library to read from Victron devices using VE.Direct protocol.
 * Derived from Victron framehandler reference implementation.
 * 
 * The MIT License
 * 
 * Copyright (c) 2019 Victron Energy BV
 * Portions Copyright (C) 2020 Chris Terwilliger
 * https://github.com/cterwilliger/VeDirectFrameHandler
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *  
 * 2020.05.05 - 0.2 - initial release
 * 2020.06.21 - 0.2 - add MIT license, no code changes
 * 2020.08.20 - 0.3 - corrected #include reference
 * 
 */
 
#include <Arduino.h>
#include "VeDirectFrameHandler.h"

char MODULE[] = "VE.Frame";	// Victron seems to use this to find out where logging messages were generated

// The name of the record that contains the checksum.
static constexpr char checksumTagName[] = "CHECKSUM";

HardwareSerial VedirectSerial(1);

VeDirectFrameHandler VeDirect;

VeDirectFrameHandler::VeDirectFrameHandler() :
	//mStop(false),	// don't know what Victron uses this for, not using
	_state(IDLE),
	_checksum(0),
	_name(""),
	_value(""),
	_pollInterval(5),
	_lastPoll(0)
{
}

void VeDirectFrameHandler::init()
{
    VedirectSerial.begin(19200, SERIAL_8N1, VICTRON_PIN_RX, VICTRON_PIN_TX);
    VedirectSerial.flush();
}

void VeDirectFrameHandler::setPollInterval(unsigned long interval)
{
    _pollInterval = interval;
}

void VeDirectFrameHandler::loop()
{
	if ((millis() - getLastUpdate()) < _pollInterval * 1000) {
		return;
	}
	
	while ( VedirectSerial.available()) {
		rxData(VedirectSerial.read());
	}
}

/*
 *	rxData
 *  This function is called by loop() which passes a byte of serial data
 *  Based on Victron's example code. But using String and Map instead of pointer and arrays
 */
void VeDirectFrameHandler::rxData(uint8_t inbyte)
{
	//if (mStop) return;
	if ( (inbyte == ':') && (_state != CHECKSUM) ) {
		_state = RECORD_HEX;
	}
	if (_state != RECORD_HEX) {
		_checksum += inbyte;
	}
	inbyte = toupper(inbyte);

	switch(_state) {
	case IDLE:
		/* wait for \n of the start of an record */
		switch(inbyte) {
		case '\n':
			_state = RECORD_BEGIN;
			break;
		case '\r': /* Skip */
		default:
			break;
		}
		break;
	case RECORD_BEGIN:
		_name = (char) inbyte;
		_state = RECORD_NAME;
		break;
	case RECORD_NAME:
		// The record name is being received, terminated by a \t
		switch(inbyte) {
		case '\t':
			// the Checksum record indicates a EOR
			if (_name.equals(checksumTagName)) {
				_state = CHECKSUM;
				break;
			}
			_state = RECORD_VALUE;
			_value = "";
			break;
		case '#': /* Ignore # from serial number*/
			break;
		default:
			// add byte to name, but do no overflow
			_name += (char) inbyte;
			break;
		}
		break;
	case RECORD_VALUE:
		// The record value is being received.  The \r indicates a new record.
		switch(inbyte) {
		case '\n':
			_tmpMap[_name] = _value;
			_state = RECORD_BEGIN;
			break;
		case '\r': /* Skip */
			break;
		default:
			// add byte to value, but do no overflow
			_value += (char) inbyte;
			break;
		}
		break;
	case CHECKSUM:
	{
		bool valid = _checksum == 0;
		if (!valid)
			logE(MODULE,"[CHECKSUM] Invalid frame");
		_checksum = 0;
		_state = IDLE;
		frameEndEvent(valid);
		break;
	}
	case RECORD_HEX:
		if (hexRxEvent(inbyte)) {
			_checksum = 0;
			_state = IDLE;
		}
		break;
	}
}

/*
 *	frameEndEvent
 *  This function is called at the end of the received frame.  If the checksum is valid, the temp buffer is read line by line.
 *  If the name exists in the public buffer, the new value is copied to the public buffer.	If not, a new name/value entry
 *  is created in the public buffer.
 */
void VeDirectFrameHandler::frameEndEvent(bool valid) {
	if ( valid ) {
		veMap = _tmpMap;
		setLastUpdate();
	}
	_tmpMap.clear();
}

/*
 *	logE
 *  This function included for continuity and possible future use.	
 */
void VeDirectFrameHandler::logE(const char * module, const char * error) {
	Serial.print("MODULE: ");
    Serial.println(module);
    Serial.print("ERROR: ");
    Serial.println(error);
	return;
}

/*
 *	hexRxEvent
 *  This function included for continuity and possible future use.	
 */
bool VeDirectFrameHandler::hexRxEvent(uint8_t inbyte) {
	return true;		// stubbed out for future
}

unsigned long VeDirectFrameHandler::getLastUpdate()
{
    return _lastPoll;
}

/*
 * setLastUpdate
 * This function is called every time a new ve.direct frame was read.
 */
void VeDirectFrameHandler::setLastUpdate()
{
    _lastPoll = millis();
}

/*
 * getPidAsString
 * This function returns the product id (PID) as readable text.
 */
String VeDirectFrameHandler::getPidAsString(const char* pid)
{
	String strPID ="";

	long lPID = strtol(pid, nullptr, 0);
	switch(lPID) {
		case 0x0300:
			strPID =  "BlueSolar MPPT 70|15";
			break;
		case 0xA040:
			strPID =  "BlueSolar MPPT 75|50";
			break;
		case 0xA041:
			strPID =  "BlueSolar MPPT 150|35";
			break;
		case 0xA042:
			strPID =  "BlueSolar MPPT 75|15";
			break;
		case 0xA043:
			strPID =  "BlueSolar MPPT 100|15";
			break;
		case 0xA044:
			strPID =  "BlueSolar MPPT 100|30";
			break;
		case 0xA045:
			strPID =  "BlueSolar MPPT 100|50";
			break;
		case 0xA046:
			strPID =  "BlueSolar MPPT 100|70";
			break;
		case 0xA047:
			strPID =  "BlueSolar MPPT 150|100";
			break;
		case 0xA049:
			strPID =  "BlueSolar MPPT 100|50 rev2";
			break;
		case 0xA04A:
			strPID =  "BlueSolar MPPT 100|30 rev2";
			break;
		case 0xA04B:
			strPID =  "BlueSolar MPPT 150|35 rev2";
			break;
		case 0XA04C:
			strPID =  "BlueSolar MPPT 75|10";
			break;
		case 0XA04D:
			strPID =  "BlueSolar MPPT 150|45";
			break;
		case 0XA04E:
			strPID =  "BlueSolar MPPT 150|60";
			break;
		case 0XA04F:
			strPID =  "BlueSolar MPPT 150|85";
			break;
		case 0XA050:
			strPID =  "SmartSolar MPPT 250|100";
			break;
		case 0XA051:
			strPID =  "SmartSolar MPPT 150|100";
			break;
		case 0XA052:
			strPID =  "SmartSolar MPPT 150|85";
			break;
		case 0XA053:
			strPID =  "SmartSolar MPPT 75|15";
			break;
		case 0XA054:
			strPID =  "SmartSolar MPPT 75|10";
			break;
		case 0XA055:
			strPID =  "SmartSolar MPPT 100|15";
			break;
		case 0XA056:
			strPID =  "SmartSolar MPPT 100|30";
			break;
		case 0XA057:
			strPID =  "SmartSolar MPPT 100|50";
			break;
		case 0XA058:
			strPID =  "SmartSolar MPPT 100|35";
			break;
		case 0XA059:
			strPID =  "SmartSolar MPPT 150|10 rev2";
			break;
		case 0XA05A:
			strPID =  "SmartSolar MPPT 150|85 rev2";
			break;
		case 0XA05B:
			strPID =  "SmartSolar MPPT 250|70";
			break;
		case 0XA05C:
			strPID =  "SmartSolar MPPT 250|85";
			break;
		case 0XA05D:
			strPID =  "SmartSolar MPPT 250|60";
			break;
		case 0XA05E:
			strPID =  "SmartSolar MPPT 250|45";
			break;
		case 0XA05F:
			strPID =  "SmartSolar MPPT 100|20";
			break;
		case 0XA060:
			strPID =  "SmartSolar MPPT 100|20 48V";
			break;
		case 0XA061:
			strPID =  "SmartSolar MPPT 150|45";
			break;
		case 0XA062:
			strPID =  "SmartSolar MPPT 150|60";
			break;
		case 0XA063:
			strPID =  "SmartSolar MPPT 150|70";
			break;
		case 0XA064:
			strPID =  "SmartSolar MPPT 250|85 rev2";
			break;
		case 0XA065:
			strPID =  "SmartSolar MPPT 250|100 rev2";
			break;
		case 0XA066:
			strPID =  "BlueSolar MPPT 100|20";
			break;
		case 0XA067:
			strPID =  "BlueSolar MPPT 100|20 48V";
			break;
		case 0XA068:
			strPID =  "SmartSolar MPPT 250|60 rev2";
			break;
		case 0XA069:
			strPID =  "SmartSolar MPPT 250|70 rev2";
			break;
		case 0XA06A:
			strPID =  "SmartSolar MPPT 150|45 rev2";
			break;
		case 0XA06B:
			strPID =  "SmartSolar MPPT 150|60 rev2";
			break;
		case 0XA06C:
			strPID =  "SmartSolar MPPT 150|70 rev2";
			break;
		case 0XA06D:
			strPID =  "SmartSolar MPPT 150|85 rev3";
			break;
		case 0XA06E:
			strPID =  "SmartSolar MPPT 150|100 rev3";
			break;
		case 0XA06F:
			strPID =  "BlueSolar MPPT 150|45 rev2";
			break;
		case 0XA070:
			strPID =  "BlueSolar MPPT 150|60 rev2";
			break;
		case 0XA071:
			strPID =  "BlueSolar MPPT 150|70 rev2";
			break;
		case 0XA102:
			strPID =  "SmartSolar MPPT VE.Can 150|70";
			break;
		case 0XA103:
			strPID =  "SmartSolar MPPT VE.Can 150|45";
			break;
		case 0XA104:
			strPID =  "SmartSolar MPPT VE.Can 150|60";
			break;
		case 0XA105:
			strPID =  "SmartSolar MPPT VE.Can 150|85";
			break;
		case 0XA106:
			strPID =  "SmartSolar MPPT VE.Can 150|100";
			break;
		case 0XA107:
			strPID =  "SmartSolar MPPT VE.Can 250|45";
			break;
		case 0XA108:
			strPID =  "SmartSolar MPPT VE.Can 250|60";
			break;
		case 0XA109:
			strPID =  "SmartSolar MPPT VE.Can 250|80";
			break;
		case 0XA10A:
			strPID =  "SmartSolar MPPT VE.Can 250|85";
			break;
		case 0XA10B:
			strPID =  "SmartSolar MPPT VE.Can 250|100";
			break;
		case 0XA10C:
			strPID =  "SmartSolar MPPT VE.Can 150|70 rev2";
			break;
		case 0XA10D:
			strPID =  "SmartSolar MPPT VE.Can 150|85 rev2";
			break;
		case 0XA10E:
			strPID =  "SmartSolar MPPT VE.Can 150|100 rev2";
			break;
		case 0XA10F:
			strPID =  "BlueSolar MPPT VE.Can 150|100";
			break;
		case 0XA112:
			strPID =  "BlueSolar MPPT VE.Can 250|70";
			break;
		case 0XA113:
			strPID =  "BlueSolar MPPT VE.Can 250|100";
			break;
		case 0XA114:
			strPID =  "SmartSolar MPPT VE.Can 250|70 rev2";
			break;
		case 0XA115:
			strPID =  "SmartSolar MPPT VE.Can 250|100 rev2";
			break;
		case 0XA116:
			strPID =  "SmartSolar MPPT VE.Can 250|85 rev2";
			break;
		default:
			strPID = pid;
	}
	return strPID;
}

/*
 * getCsAsString
 * This function returns the state of operations (CS) as readable text.
 */
String VeDirectFrameHandler::getCsAsString(const char* cs)
{
	String strCS ="";

	int iCS = atoi(cs);
	switch(iCS) {
		case 0:
			strCS =  "OFF";
			break;
		case 2:
			strCS =  "Fault";
			break;
		case 3:
			strCS =  "Bulk";
			break;
		case 4:
			strCS =  "Absorbtion";
			break;
		case 5:
			strCS =  "Float";
			break;
		case 7:
			strCS =  "Equalize (manual)";
			break;
		case 245:
			strCS =  "Starting-up";
			break;
		case 247:
			strCS =  "Auto equalize / Recondition";
			break;
		case 252:
			strCS =  "External Control";
			break;
		default:
			strCS = cs;
	}
	return strCS;
}

/*
 * getErrAsString
 * This function returns error state (ERR) as readable text.
 */
String VeDirectFrameHandler::getErrAsString(const char* err)
{
	String strERR ="";

	int iERR = atoi(err);
	switch(iERR) {
		case 0:
			strERR =  "No error";
			break;
		case 2:
			strERR =  "Battery voltage too high";
			break;
		case 17:
			strERR =  "Charger temperature too high";
			break;
		case 18:
			strERR =  "Charger over current";
			break;
		case 19:
			strERR =  "Charger current reversed";
			break;
		case 20:
			strERR =  "Bulk time limit exceeded";
			break;
		case 21:
			strERR =  "Current sensor issue(sensor bias/sensor broken)";
			break;
		case 26:
			strERR =  "Terminals overheated";
			break;
		case 28:
			strERR =  "Converter issue (dual converter models only)";
			break;
		case 33:
			strERR =  "Input voltage too high (solar panel)";
			break;
		case 34:
			strERR =  "Input current too high (solar panel)";
			break;
		case 38:
			strERR =  "Input shutdown (due to excessive battery voltage)";
			break;
		case 39:
			strERR =  "Input shutdown (due to current flow during off mode)";
			break;
		case 40:
			strERR =  "Input";
			break;
		case 65:
			strERR =  "Lost communication with one of devices";
			break;
		case 67:
			strERR =  "Synchronisedcharging device configuration issue";
			break;
		case 68:
			strERR =  "BMS connection lost";
			break;
		case 116:
			strERR =  "Factory calibration data lost";
			break;
		case 117:
			strERR =  "Invalid/incompatible firmware";
			break;
		case 118:
			strERR =  "User settings invalid";
			break;
		default:
			strERR = err;
	}
	return strERR;
}

/*
 * getOrAsString
 * This function returns the off reason (OR) as readable text.
 */
String VeDirectFrameHandler::getOrAsString(const char* offReason)
{
	String strOR ="";

	long lOR = strtol(offReason, nullptr, 0);
	switch(lOR) {
		case 0x00000000:
			strOR =  "Not off";
			break;
		case 0x00000001:
			strOR =  "No input power";
			break;
		case 0x00000002:
			strOR =  "Switched off (power switch)";
			break;
		case 0x00000004:
			strOR =  "Switched off (device moderegister)";
			break;
		case 0x00000008:
			strOR =  "Remote input";
			break;
		case 0x00000010:
			strOR =  "Protection active";
			break;
		case 0x00000020:
			strOR =  "Paygo";
			break;
		case 0x00000040:
			strOR =  "BMS";
			break;
		case 0x00000080:
			strOR =  "Engine shutdown detection";
			break;
		case 0x00000100:
			strOR =  "Analysing input voltage";
			break;
		default:
			strOR = offReason;
	}
	return strOR;
}

/*
 * getMpptAsString
 * This function returns the state of MPPT (MPPT) as readable text.
 */
String VeDirectFrameHandler::getMpptAsString(const char* mppt)
{
	String strMPPT ="";

	int iMPPT = atoi(mppt);
	switch(iMPPT) {
		case 0:
			strMPPT =  "Off";
			break;
		case 1:
			strMPPT =  "Voltage or current limited";
			break;
		case 2:
			strMPPT =  "MPP Tracker active";
			break;
		default:
			strMPPT = mppt;
	}
	return strMPPT;
}