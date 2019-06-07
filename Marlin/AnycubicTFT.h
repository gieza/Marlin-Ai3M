/*
 AnycubicTFT.h  --- Support for Anycubic i3 Mega TFT
 Created by Christian Hopp on 09.12.17.

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.

 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/


#ifndef AnycubicTFT_h
#define AnycubicTFT_h

#include <stdio.h>
#include "MarlinConfig.h"

char *itostr2(const uint8_t &x);

#ifndef ULTRA_LCD
char *itostr3(const int);
char *ftostr32(const float &);
#endif

#ifdef ANYCUBIC_TFT_DEBUG
	#define ANYCUBIC_TFT_DEBUG_ECHOLNPGM(x)			          SERIAL_PROTOCOLLNPGM(x)
  #define ANYCUBIC_TFT_DEBUG_ECHOPAIR(name, value)	    SERIAL_PROTOCOLPAIR(name, value)
  #define ANYCUBIC_TFT_DEBUG_ECHOLNPAIR(name, value)	  SERIAL_PROTOCOLLNPAIR(name, value)
  #define ANYCUBIC_TFT_DEBUG_ECHO(x)                    SERIAL_PROTOCOL(x)
#else
	#define ANYCUBIC_TFT_DEBUG_ECHOLNPGM(x)
  #define ANYCUBIC_TFT_DEBUG_ECHOPAIR(name, value)
  #define ANYCUBIC_TFT_DEBUG_ECHOLNPAIR(name, value)
  #define ANYCUBIC_TFT_DEBUG_ECHO(x)
#endif

#define TFTBUFSIZE 4
#define TFT_MAX_CMD_SIZE 96
#define MSG_MY_VERSION "V116"

#define ANYCUBIC_TFT_STATE_IDLE           0
#define ANYCUBIC_TFT_STATE_SD_PRINT       1
#define ANYCUBIC_TFT_STATE_SD_PAUSE       2
#define ANYCUBIC_TFT_STATE_SD_PAUSE_REQ   3
#define ANYCUBIC_TFT_STATE_SD_PAUSE_OOF   4
#define ANYCUBIC_TFT_STATE_SD_STOP_REQ    5
#define ANYCUBIC_TFT_STATE_SD_OUTAGE      99

#define A0_GET_HOTEND_TEMPERATURE             0
#define A1_GET_HOTEND_TARGET_TEMPERATURE      1
#define A2_GET_HOTBED_TEMPERATURE             2
#define A3_GET_HOTBED_TARGET_TEMPERATURE      3
#define A4_GET_FAN_SPEED                      4
#define A5_GET_CURRENT_COORDINATE             5
#define A6_GET_SD_CARD_PRINTING_STATUS        6
#define A7_GET_PRINTING_TIME                  7
#define A8_GET_SD_LIST                        8
#define A9_PAUSE_SD_PRINT                     9
#define A10_RESUME_SD_PRINT                   10
#define A11_STOP_SD_PRINT                     11
#define A12_KILL                              12
#define A13_SELECTION_FILE                    13
#define A14_START_PRINTING                    14
#define A15_RESUMING_FROM_OUTAGE              15
#define A16_SET_HOTEND_TEMPERATURE            16
#define A17_SET_HEATED_BED_TEMPERATURE        17
#define A18_SET_FAN_SPEED                     18
#define A19_STOP_STEPPER_DRIVERS              19
#define A20_READ_PRINTING_SPEED               20
#define A21_ALL_HOME                          21
#define A22_MOVE_XYZ_EXTRUDE                  22
#define A23_PREHEAT_PLA                       23
#define A24_PREHEAT_ABS                       24
#define A25_COOL_DOWN                         25
#define A26_REFRESH_SD                        26
#define A27_SERVOS_ANGLES_ADJUST              27
#define A28_FILAMENT_TEST                     28
#define A29_Z_PROBE_OFFESET_SET               29
#define A30_ASSIST_LEVELING                   30
#define A31_ZOFFSET                           31
#define A32_CLEAN_LEVELING_BEEP_FLAG          32
#define A33_GET_VERSION_INFO                  33

class AnycubicTFTClass {
public:
  AnycubicTFTClass();
  void Setup();
  void CommandScan();
  void BedHeatingStart();
  void BedHeatingDone();
  void HeatingDone();
  void HeatingStart();
  void FilamentRunout();
  void KillTFT();
  char TFTstate=ANYCUBIC_TFT_STATE_IDLE;

  /**
  * Anycubic TFT pause states:
  *
  * 0 - printing / stopped
  * 1 - regular pause
  * 2 - M600 pause
  * 3 - filament runout pause
  * 4 - nozzle timeout on M600
  * 5 - nozzle timeout on filament runout
  */
  uint8_t ai3m_pause_state = 0;

private:
  char TFTcmdbuffer[TFTBUFSIZE][TFT_MAX_CMD_SIZE];
  int TFTbuflen=0;
  int TFTbufindr = 0;
  int TFTbufindw = 0;
  char serial3_char;
  int serial3_count = 0;
  char *TFTstrchr_pointer;
  char FlagResumFromOutage=0;
  uint16_t filenumber=0;
  unsigned long starttime=0;
  unsigned long stoptime=0;
  uint8_t tmp_extruder=0;
  char LastSDstatus=0;
  uint16_t HeaterCheckCount=0;
  bool IsParked = false;

  struct OutageDataStruct {
    char OutageDataVersion;
    char OutageFlag;
    float last_position[XYZE];
    float last_bed_temp;
    float last_hotend_temp;
    long lastSDposition;
  } OutageData;

  void WriteOutageEEPromData();
  void ReadOutageEEPromData();

  float CodeValue();
  bool CodeSeen(char);
  void ls();
  void StartPrint();
  void PausePrint();
  void StopPrint();
  void StateHandler();
  void GetCommandFromTFT();
  void processCommandFromTFT();
  void CheckSDCardChange();
  void CheckHeaterError();
  void HandleSpecialMenu();
  void FilamentChangePause();
  void FilamentChangeResume();
  void ReheatNozzle();
  void ParkAfterStop();
  void doGetSdCardPrintingStatus();
  void doGetPrintingTime();
  void doGetSdList();
  void doRefershSD();
  void doPauseSdPrint();
  void doResumeSdPrint();
  void doStopSdPrint();
  void doSelectFile();
  void doStartPrinting();
  void doSetHotEndTemp();
  void doSetHotBedTemp();
  void doSetFanSpeed();
  void doStopSteppers();
  void doReportPrintingSpeed();
  void doHomePrinter();
  void doMoveXYZorExtrude();
  void doPreHeatPrinter(const int16_t celsiusBed, const int16_t celsiusHotEnd, const uint8_t hotEnd2);
  void doPrinterCoolDown();
  void doAssistLeveling();
  void doZOffset();
  void doCleanLevelingBeepFlag();

  bool isPrinterReadyForCmd();
  void raiseZAxisIfNeeded();

  char     SelectedDirectory[30];
  uint8_t  SpecialMenu=false;

#if ENABLED(ANYCUBIC_FILAMENT_RUNOUT_SENSOR)
  char FilamentTestStatus=false;
  char FilamentTestLastStatus=false;
  bool FilamentSetMillis=true;

#endif
};

extern AnycubicTFTClass AnycubicTFT;

#endif /* AnycubicTFT_h */
