/*
  AnycubicTFT.cpp  --- Support for Anycubic i3 Mega TFT
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


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "Arduino.h"

#include "MarlinConfig.h"
#include "Marlin.h"
#include "cardreader.h"
#include "planner.h"
#include "temperature.h"
#include "language.h"
#include "stepper.h"
#include "serial.h"
#include "printcounter.h"
#include "macros.h"
#include "buzzer.h"

#ifdef ANYCUBIC_TFT_MODEL
#include "AnycubicTFT.h"
#include "AnycubicSerial.h"

char _conv[8];

char *itostr2(const uint8_t &x)
{
  //sprintf(conv,"%5.1f",x);
  int xx=x;
  _conv[0]=(xx/10)%10+'0';
  _conv[1]=(xx)%10+'0';
  _conv[2]=0;
  return _conv;
}

#ifndef ULTRA_LCD
  #define DIGIT(n) ('0' + (n))
  #define DIGIMOD(n, f) DIGIT((n)/(f) % 10)
  #define RJDIGIT(n, f) ((n) >= (f) ? DIGIMOD(n, f) : ' ')
  #define MINUSOR(n, alt) (n >= 0 ? (alt) : (n = -n, '-'))


  char* itostr3(const int x) {
    int xx = x;
    _conv[4] = MINUSOR(xx, RJDIGIT(xx, 100));
    _conv[5] = RJDIGIT(xx, 10);
    _conv[6] = DIGIMOD(xx, 1);
    return &_conv[4];
  }


  // Convert signed float to fixed-length string with 023.45 / -23.45 format

  char *ftostr32(const float &x) {
    long xx = x * 100;
    _conv[1] = MINUSOR(xx, DIGIMOD(xx, 10000));
    _conv[2] = DIGIMOD(xx, 1000);
    _conv[3] = DIGIMOD(xx, 100);
    _conv[4] = '.';
    _conv[5] = DIGIMOD(xx, 10);
    _conv[6] = DIGIMOD(xx, 1);
    return &_conv[1];
  }

#endif

AnycubicTFTClass::AnycubicTFTClass() {
}

void AnycubicTFTClass::Setup() {
  AnycubicSerial.begin(115200);
  //ANYCUBIC_SERIAL_START();
  ANYCUBIC_SERIAL_PROTOCOLLNPGM("J17"); // J17 Main board reset
  delay(10);
  ANYCUBIC_SERIAL_PROTOCOLLNPGM("J12"); // J12 Ready

  #if ENABLED(SDSUPPORT) && PIN_EXISTS(SD_DETECT)
    pinMode(SD_DETECT_PIN, INPUT);
    WRITE(SD_DETECT_PIN, HIGH);
  #endif

  #if ENABLED(ANYCUBIC_FILAMENT_RUNOUT_SENSOR)
    pinMode(FIL_RUNOUT_PIN,INPUT);
    WRITE(FIL_RUNOUT_PIN,HIGH);
    if(READ(FIL_RUNOUT_PIN)==true)
    {
      ANYCUBIC_SERIAL_PROTOCOLLNPGM("J15"); //J15 FILAMENT LACK
      ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Serial Debug: Filament runout... J15");
    }
  #endif

  SelectedDirectory[0]=0;
  SpecialMenu=false;

  #ifdef STARTUP_CHIME
    buzzer.tone(250, 554); // C#5
    buzzer.tone(250, 740); // F#5
    buzzer.tone(250, 554); // C#5
    buzzer.tone(500, 831); // G#5
  #endif
}

void AnycubicTFTClass::WriteOutageEEPromData() {
  //TODO: the method is not used, functionality not implemented
  //int pos=E2END-256;

}

void AnycubicTFTClass::ReadOutageEEPromData() {
  //TODO: the method is not used, functionality not implemented
  //int pos=E2END-256;

}

void AnycubicTFTClass::KillTFT()
{
  ANYCUBIC_SERIAL_PROTOCOLLNPGM("J11"); // J11 Kill
  ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Serial Debug: Kill command... J11");
}


void AnycubicTFTClass::StartPrint() {
  // which kind of starting behaviour is needed?
  switch (ai3m_pause_state) {
    case 0:
      // no pause, just a regular start
      starttime=millis();
      card.startFileprint();
      TFTstate=ANYCUBIC_TFT_STATE_SD_PRINT;
      ANYCUBIC_TFT_DEBUG_ECHOLNPAIR(" DEBUG: AI3M Pause State: ", ai3m_pause_state);
      ANYCUBIC_TFT_DEBUG_ECHOLNPGM("DEBUG: Regular Start");
      break;
    case 1:
      // regular sd pause
      enqueue_and_echo_commands_P(PSTR("M24")); // unpark nozzle
      
      ANYCUBIC_TFT_DEBUG_ECHOLNPAIR(" DEBUG: AI3M Pause State: ", ai3m_pause_state);
      ANYCUBIC_TFT_DEBUG_ECHOLNPGM("DEBUG: M24 Resume from regular pause");
      
      IsParked=false; // remove parked flag
      starttime=millis();
      card.startFileprint(); // resume regularly
      TFTstate=ANYCUBIC_TFT_STATE_SD_PRINT;
      ai3m_pause_state = 0;
      
      ANYCUBIC_TFT_DEBUG_ECHOLNPAIR(" DEBUG: AI3M Pause State: ", ai3m_pause_state);
      break;
    case 2:
      // paused by M600
      ANYCUBIC_TFT_DEBUG_ECHOLNPAIR(" DEBUG: AI3M Pause State: ", ai3m_pause_state);
      ANYCUBIC_TFT_DEBUG_ECHOLNPGM("DEBUG: Start M108 routine");

      FilamentChangeResume(); // enter display M108 routine
      ai3m_pause_state = 0; // clear flag

      ANYCUBIC_TFT_DEBUG_ECHOLNPGM("DEBUG: Filament Change Flag cleared");
      
      break;
    case 3:
      // paused by filament runout
      enqueue_and_echo_commands_P(PSTR("M24")); // unpark nozzle and resume
      
      ANYCUBIC_TFT_DEBUG_ECHOLNPGM("DEBUG: M24 Resume from Filament Runout");
      
      IsParked = false; // clear flags
      ai3m_pause_state = 0;
      ANYCUBIC_TFT_DEBUG_ECHOLNPGM("DEBUG: Filament Pause Flag cleared");
      
      break;
    case 4:
      // nozzle was timed out before (M600), do not enter printing state yet
      TFTstate=ANYCUBIC_TFT_STATE_SD_PAUSE_REQ;
      ANYCUBIC_TFT_DEBUG_ECHOLNPGM("DEBUG: Set Pause again because of timeout");

      // clear the timeout flag to ensure the print continues on the
      // next push of CONTINUE
      ai3m_pause_state = 2;
      ANYCUBIC_TFT_DEBUG_ECHOLNPGM("DEBUG: Nozzle timeout flag cleared");

      break;
    case 5:
      // nozzle was timed out before (runout), do not enter printing state yet
      TFTstate=ANYCUBIC_TFT_STATE_SD_PAUSE_REQ;
      ANYCUBIC_TFT_DEBUG_ECHOLNPGM("DEBUG: Set Pause again because of timeout");

      // clear the timeout flag to ensure the print continues on the
      // next push of CONTINUE
      ai3m_pause_state = 3;
      ANYCUBIC_TFT_DEBUG_ECHOLNPGM("DEBUG: Nozzle timeout flag cleared");
      
      break;
    default:
      break;
  }
}

void AnycubicTFTClass::PausePrint() {
  #ifdef SDSUPPORT
    if(ai3m_pause_state < 2) { // is this a regular pause?
      card.pauseSDPrint(); // pause print regularly
      ANYCUBIC_TFT_DEBUG_ECHOLNPAIR(" DEBUG: AI3M Pause State: ", ai3m_pause_state);
      ANYCUBIC_TFT_DEBUG_ECHOLNPGM("DEBUG: Regular Pause");
    } else { // pause caused by filament runout
      ANYCUBIC_TFT_DEBUG_ECHOLNPGM("DEBUG: Filament Runout Pause");
      
      // filament runout, retract and beep
      enqueue_and_echo_commands_P(PSTR("G91")); // relative mode
      enqueue_and_echo_commands_P(PSTR("G1 E-3 F1800")); // retract 3mm
      enqueue_and_echo_commands_P(PSTR("G90")); // absolute mode
      buzzer.tone(200, 1567);
      buzzer.tone(200, 1174);
      buzzer.tone(200, 1567);
      buzzer.tone(200, 1174);
      buzzer.tone(2000, 1567);

      ANYCUBIC_TFT_DEBUG_ECHOLNPGM("DEBUG: Filament runout - Retract, beep and park.");
      
      enqueue_and_echo_commands_P(PSTR("M25")); // pause print and park nozzle
      ai3m_pause_state = 3;
      ANYCUBIC_TFT_DEBUG_ECHOLNPGM("DEBUG: M25 sent, parking nozzle");
      ANYCUBIC_TFT_DEBUG_ECHOLNPGM(" DEBUG: AI3M Pause State: 3");
      
      IsParked = true;
      // show filament runout prompt on screen
      ANYCUBIC_SERIAL_PROTOCOLLNPGM("J23");
      
      ANYCUBIC_TFT_DEBUG_ECHOLNPGM("DEBUG: J23 Show filament prompt");
    }
  #endif
  TFTstate=ANYCUBIC_TFT_STATE_SD_PAUSE_REQ;
}

void AnycubicTFTClass::StopPrint(){
    // stop print, disable heaters
    card.stopSDPrint();
    clear_command_queue();
    ANYCUBIC_TFT_DEBUG_ECHOLNPGM("DEBUG: Stopped and cleared");

    print_job_timer.stop();
    thermalManager.disable_all_heaters();
    // we are not parked yet, do it in the display state routine
    IsParked = false;
// turn off fan, cancel any heatups and set display state
#if FAN_COUNT > 0
    for (uint8_t i = 0; i < FAN_COUNT; i++) fanSpeeds[i] = 0;
#endif
    wait_for_heatup = false;
    ai3m_pause_state = 0;
    ANYCUBIC_TFT_DEBUG_ECHOLNPGM(" DEBUG: AI3M Pause State: 0");

    TFTstate = ANYCUBIC_TFT_STATE_SD_STOP_REQ;
}

void AnycubicTFTClass::FilamentChangeResume(){
  // call M108 to break out of M600 pause
  enqueue_and_echo_commands_P(PSTR("M108"));
  ANYCUBIC_TFT_DEBUG_ECHOLNPGM("DEBUG: M108 Resume called");

  // remove waiting flags
  wait_for_heatup = false;
  wait_for_user = false;

  // resume with proper progress state
  card.startFileprint();
  ANYCUBIC_TFT_DEBUG_ECHOLNPGM("DEBUG: M108 Resume done");
}

void AnycubicTFTClass::FilamentChangePause(){
  // set filament change flag to ensure the M108 routine
  // gets used when the user hits CONTINUE
  ai3m_pause_state = 2;
  ANYCUBIC_TFT_DEBUG_ECHOLNPGM(" DEBUG: AI3M Pause State: 2");

  // call M600 and set display state to paused
  enqueue_and_echo_commands_P(PSTR("M600"));
  TFTstate=ANYCUBIC_TFT_STATE_SD_PAUSE_REQ;
  ANYCUBIC_TFT_DEBUG_ECHOLNPGM("DEBUG: M600 Pause called");
}

void AnycubicTFTClass::ReheatNozzle(){
  ANYCUBIC_TFT_DEBUG_ECHOLNPGM("DEBUG: Send reheat M108");
  enqueue_and_echo_commands_P(PSTR("M108"));
  ANYCUBIC_TFT_DEBUG_ECHOLNPGM("DEBUG: Resume heating");

  // enable heaters again
  HOTEND_LOOP() thermalManager.reset_heater_idle_timer(e);
  ANYCUBIC_TFT_DEBUG_ECHOLNPGM("DEBUG: Clear flags");

  // clear waiting flags
  nozzle_timed_out = false;
  wait_for_user = false;
  wait_for_heatup = false;
  // lower the pause flag by two to restore initial pause condition
  if (ai3m_pause_state > 3) {
    ai3m_pause_state -= 2;
    ANYCUBIC_TFT_DEBUG_ECHOLNPAIR(" DEBUG: NTO done, AI3M Pause State: ", ai3m_pause_state);
  }

  // set pause state to show CONTINUE button again
  TFTstate=ANYCUBIC_TFT_STATE_SD_PAUSE_REQ;
}

void AnycubicTFTClass::ParkAfterStop(){
  // only park the nozzle if homing was done before
  if (!axis_unhomed_error()) {
    // raize nozzle by 25mm respecting Z_MAX_POS
    do_blocking_move_to_z(MIN(current_position[Z_AXIS] + 25, Z_MAX_POS), 5);
      ANYCUBIC_TFT_DEBUG_ECHOLNPGM("DEBUG: SDSTOP: Park Z");
    // move bed and hotend to park position
    do_blocking_move_to_xy((X_MIN_POS + 10), (Y_MAX_POS - 10), 100);
    ANYCUBIC_TFT_DEBUG_ECHOLNPGM("DEBUG: SDSTOP: Park XY");
  }
  enqueue_and_echo_commands_P(PSTR("M84")); // disable stepper motors
  enqueue_and_echo_commands_P(PSTR("M27")); // force report of SD status
  ai3m_pause_state = 0;
  ANYCUBIC_TFT_DEBUG_ECHOLNPAIR(" DEBUG: AI3M Pause State: ", ai3m_pause_state);
}

float AnycubicTFTClass::CodeValue() {
  return (strtod(&TFTcmdbuffer[TFTbufindr][TFTstrchr_pointer - TFTcmdbuffer[TFTbufindr] + 1], NULL));
}

bool AnycubicTFTClass::CodeSeen(char code) {
  TFTstrchr_pointer = strchr(TFTcmdbuffer[TFTbufindr], code);
  return (TFTstrchr_pointer != NULL); //Return True if a character was found
}

void AnycubicTFTClass::HandleSpecialMenu() {
  if(strcmp_P(SelectedDirectory, PSTR("<special menu>"))==0) {
    SpecialMenu=true;
  } else if (strcmp_P(SelectedDirectory, PSTR("<auto tune hotend pid>"))==0) {
    SERIAL_PROTOCOLLNPGM("Special Menu: Auto Tune Hotend PID");
    enqueue_and_echo_commands_P(PSTR("M106 S204\nM303 E0 S210 C15 U1"));
  } else if (strcmp_P(SelectedDirectory, PSTR("<auto tune hotbed pid>"))==0) {
    SERIAL_PROTOCOLLNPGM("Special Menu: Auto Tune Hotbed Pid");
    enqueue_and_echo_commands_P(PSTR("M303 E-1 S60 C6 U1"));
  } else if (strcmp_P(SelectedDirectory, PSTR("<save eeprom>"))==0) {
    SERIAL_PROTOCOLLNPGM("Special Menu: Save EEPROM");
    enqueue_and_echo_commands_P(PSTR("M500"));
    buzzer.tone(105, 1108);
    buzzer.tone(210, 1661);
  } else if (strcmp_P(SelectedDirectory, PSTR("<load fw defaults>"))==0) {
    SERIAL_PROTOCOLLNPGM("Special Menu: Load FW Defaults");
    enqueue_and_echo_commands_P(PSTR("M502"));
    buzzer.tone(105, 1661);
    buzzer.tone(210, 1108);
  } else if (strcmp_P(SelectedDirectory, PSTR("<preheat bed>"))==0) {
    SERIAL_PROTOCOLLNPGM("Special Menu: Preheat Bed");
    enqueue_and_echo_commands_P(PSTR("M140 S60"));
  } else if (strcmp_P(SelectedDirectory, PSTR("<start mesh leveling>"))==0) {
    SERIAL_PROTOCOLLNPGM("Special Menu: Start Mesh Leveling");
    enqueue_and_echo_commands_P(PSTR("G29 S1"));
  } else if (strcmp_P(SelectedDirectory, PSTR("<next mesh point>"))==0) {
    SERIAL_PROTOCOLLNPGM("Special Menu: Next Mesh Point");
    enqueue_and_echo_commands_P(PSTR("G29 S2"));
  } else if (strcmp_P(SelectedDirectory, PSTR("<z up 0.1>"))==0) {
    SERIAL_PROTOCOLLNPGM("Special Menu: Z Up 0.1");
    enqueue_and_echo_commands_P(PSTR("G91\nG1 Z+0.1\nG90"));
  } else if (strcmp_P(SelectedDirectory, PSTR("<z up 0.02>"))==0) {
    SERIAL_PROTOCOLLNPGM("Special Menu: Z Up 0.02");
    enqueue_and_echo_commands_P(PSTR("G91\nG1 Z+0.02\nG90"));
  } else if (strcmp_P(SelectedDirectory, PSTR("<z down 0.02>"))==0) {
    SERIAL_PROTOCOLLNPGM("Special Menu: Z Down 0.02");
    enqueue_and_echo_commands_P(PSTR("G91\nG1 Z-0.02\nG90"));
  } else if (strcmp_P(SelectedDirectory, PSTR("<z down 0.1>"))==0) {
    SERIAL_PROTOCOLLNPGM("Special Menu: Z Down 0.1");
    enqueue_and_echo_commands_P(PSTR("G91\nG1 Z-0.1\nG90"));
  } else if (strcmp_P(SelectedDirectory, PSTR("<filamentchange pause>"))==0) {
    SERIAL_PROTOCOLLNPGM("Special Menu: FilamentChange Pause");
    FilamentChangePause();
  } else if (strcmp_P(SelectedDirectory, PSTR("<filamentchange resume>"))==0) {
    SERIAL_PROTOCOLLNPGM("Special Menu: FilamentChange Resume");
    FilamentChangeResume();
  } else if (strcmp_P(SelectedDirectory, PSTR("<exit>"))==0) {
    SpecialMenu=false;
  }
}

void AnycubicTFTClass::ls() {
  if (SpecialMenu) {
    switch (filenumber) {
      case 0: // First Page
          ANYCUBIC_SERIAL_PROTOCOLLNPGM("<Z Up 0.1>\r\n<Z Up 0.1>");
          ANYCUBIC_SERIAL_PROTOCOLLNPGM("<Z Up 0.02>\r\n<Z Up 0.02>");
          ANYCUBIC_SERIAL_PROTOCOLLNPGM("<Z Down 0.02>\r\n<Z Down 0.02>");
          ANYCUBIC_SERIAL_PROTOCOLLNPGM("<Z Down 0.1>\r\n<Z Down 0.1>");
          break;

      case 4: // Second Page
          ANYCUBIC_SERIAL_PROTOCOLLNPGM("<Preheat bed>\r\n<Preheat bed>");
          ANYCUBIC_SERIAL_PROTOCOLLNPGM("<Start Mesh Leveling>\r\n<Start Mesh Leveling>");
          ANYCUBIC_SERIAL_PROTOCOLLNPGM("<Next Mesh Point>\r\n<Next Mesh Point>");
          ANYCUBIC_SERIAL_PROTOCOLLNPGM("<Save EEPROM>\r\n<Save EEPROM>");
          break;

      case 8: // Third Page
        ANYCUBIC_SERIAL_PROTOCOLLNPGM("<Exit>\r\n<Exit>");
        ANYCUBIC_SERIAL_PROTOCOLLNPGM("<Auto Tune Hotend PID>\r\n<Auto Tune Hotend PID>");
        ANYCUBIC_SERIAL_PROTOCOLLNPGM("<Auto Tune Hotbed PID>\r\n<Auto Tune Hotbed PID>");
        ANYCUBIC_SERIAL_PROTOCOLLNPGM("<Load FW Defaults>\r\n<Load FW Defaults>");
      break;

      case 12: // Fourth Page
        ANYCUBIC_SERIAL_PROTOCOLLNPGM("<FilamentChange Pause>\r\n<FilamentChange Pause>");
        ANYCUBIC_SERIAL_PROTOCOLLNPGM("<FilamentChange Resume>\r\n<FilamentChange Resume>");
        break;

      default:
      break;
    }
  }
  #ifdef SDSUPPORT
    else if(card.cardOK) {
      uint16_t max_files;
      uint16_t dir_files=card.getnrfilenames(); //recursevly reads and counts number of files and directories on current dir

      //filenumber is reported by TFT
      if ((dir_files - filenumber) < 4) {   //if requested filenumber is smaller by 4 than total number of files on sd
      //i.e. we are not on the last page
          max_files = dir_files;
      } else {
        //we are on the last page; will have to report some empty files???
          max_files = filenumber + 3;
      }

      for(uint16_t cnt=filenumber; cnt<=max_files; cnt++) {
        if (cnt==0) {// Special Entry
          if(strcmp(card.getWorkDirName(),"/") == 0) {        //shows that we are in the root folder
              ANYCUBIC_SERIAL_PROTOCOLLNPGM("<Special Menu>\r\n<Special Menu>");
              SERIAL_PROTOCOL(cnt);
              SERIAL_PROTOCOLLNPGM("<Special_Menu>");
          } else {                                          //shows, that we are in subfolder -- it is possible to go up
              ANYCUBIC_SERIAL_PROTOCOLLNPGM("/..\r\n/..");
              //this is debug log 
              SERIAL_PROTOCOL(cnt);
              SERIAL_PROTOCOLLNPGM("/..");
          }
        } else if (cnt > dir_files) {
            card.getfilename(cnt-1);
            ANYCUBIC_SERIAL_PROTOCOLLN(card.filename);
            ANYCUBIC_SERIAL_PROTOCOLLN(card.longFilename);
            //this is debug log :
            SERIAL_PROTOCOL(cnt);
            SERIAL_PROTOCOLLN(card.longFilename);
            SERIAL_PROTOCOLPGM("Too many files requested");
        } else {
          card.getfilename(cnt-1);
          //      card.getfilename(cnt);

          if(card.filenameIsDir) {
            SERIAL_PROTOCOL(cnt);
            SERIAL_PROTOCOLLN(card.longFilename);
            ANYCUBIC_SERIAL_PROTOCOLPGM("/");
            ANYCUBIC_SERIAL_PROTOCOLLN(card.filename);
            ANYCUBIC_SERIAL_PROTOCOLPGM("/");
            ANYCUBIC_SERIAL_PROTOCOLLN(card.longFilename);
            //this is debug log :
            SERIAL_PROTOCOL(cnt);
            SERIAL_PROTOCOLPGM("/");
            SERIAL_PROTOCOLLN(card.longFilename);
          } else {
            ANYCUBIC_SERIAL_PROTOCOLLN(card.filename);
            ANYCUBIC_SERIAL_PROTOCOLLN(card.longFilename);
            //this is debug log :
            SERIAL_PROTOCOL(cnt);
            SERIAL_PROTOCOLLN(card.longFilename);
          }
        }
      }
    }
  #endif
  else {
    ANYCUBIC_SERIAL_PROTOCOLLNPGM("<Special_Menu>\r\n<Special_Menu>");
  }
}

void AnycubicTFTClass::CheckSDCardChange()
{
  #ifdef SDSUPPORT
    if (LastSDstatus != IS_SD_INSERTED())
    {
      LastSDstatus = IS_SD_INSERTED();

      if (LastSDstatus)
      {
        card.initsd();
        ANYCUBIC_SERIAL_PROTOCOLLNPGM("J00"); // J00 SD Card inserted
        ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Serial Debug: SD card inserted... J00");
      }
      else
      {
        ANYCUBIC_SERIAL_PROTOCOLLNPGM("J01"); // J01 SD Card removed
        ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Serial Debug: SD card removed... J01");
      }
    }
  #endif
}

void AnycubicTFTClass::CheckHeaterError()
{
  if ((thermalManager.degHotend(0) < 5) || (thermalManager.degHotend(0) > 290))
  {
    if (HeaterCheckCount > 60000)
    {
      HeaterCheckCount = 0;
      ANYCUBIC_SERIAL_PROTOCOLLNPGM("J10"); // J10 Hotend temperature abnormal
      ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Serial Debug: Hotend temperature abnormal... J20");

    }
    else
    {
      HeaterCheckCount++;
    }
  }
  else
  {
    HeaterCheckCount = 0;
  }
}

void AnycubicTFTClass::StateHandler()
{
  switch (TFTstate) {
    case ANYCUBIC_TFT_STATE_IDLE:
      #ifdef SDSUPPORT
        if(card.sdprinting) {
          TFTstate=ANYCUBIC_TFT_STATE_SD_PRINT;
          starttime=millis();

          // --> Send print info to display... most probably print started via gcode
        }
      #endif
      break;
    case ANYCUBIC_TFT_STATE_SD_PRINT:
      #ifdef SDSUPPORT
        if(!card.sdprinting) {
          // It seems that we are to printing anymore... pause or stopped?
          if (card.isFileOpen()) {
            // File is still open --> paused
            TFTstate=ANYCUBIC_TFT_STATE_SD_PAUSE;
          } else if ((!card.isFileOpen()) && (ai3m_pause_state == 0)) {
            // File is closed --> stopped
            TFTstate=ANYCUBIC_TFT_STATE_IDLE;
            ANYCUBIC_SERIAL_PROTOCOLLNPGM("J14");// J14 print done
            ai3m_pause_state = 0;
            ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Serial Debug: SD print done... J14");
            ANYCUBIC_TFT_DEBUG_ECHOLNPAIR(" DEBUG: AI3M Pause State: ", ai3m_pause_state);
          }
        }
      #endif
      break;
    case ANYCUBIC_TFT_STATE_SD_PAUSE:
      break;
    case ANYCUBIC_TFT_STATE_SD_PAUSE_OOF:
      #ifdef ANYCUBIC_FILAMENT_RUNOUT_SENSOR
        if(!FilamentTestStatus) {
          // We got filament again
          TFTstate=ANYCUBIC_TFT_STATE_SD_PAUSE;
        }
      #endif
      break;
    case ANYCUBIC_TFT_STATE_SD_PAUSE_REQ:
      ANYCUBIC_SERIAL_PROTOCOLLNPGM("J18");
      #ifdef SDSUPPORT
        if((!card.sdprinting) && (!planner.movesplanned())) {
          // We have to wait until the sd card printing has been settled
          if(ai3m_pause_state < 2) {

            // no flags, this is a regular pause.
            ai3m_pause_state = 1;
            ANYCUBIC_TFT_DEBUG_ECHOLNPAIR(" DEBUG: AI3M Pause State: ", ai3m_pause_state);
            ANYCUBIC_TFT_DEBUG_ECHOLNPGM("DEBUG: Regular Pause requested");
            if(!IsParked) {
              // park head and retract 2mm
              enqueue_and_echo_commands_P(PSTR("M125 L2"));
              IsParked = true;
            }
          }
          #ifdef ANYCUBIC_FILAMENT_RUNOUT_SENSOR
            if(FilamentTestStatus) {
              TFTstate=ANYCUBIC_TFT_STATE_SD_PAUSE;
            } else {
              // Pause because of "out of filament"
              TFTstate=ANYCUBIC_TFT_STATE_SD_PAUSE_OOF;
            }
          #endif
          ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Serial Debug: SD print paused done... J18");
        }
      #endif
      break;
    case ANYCUBIC_TFT_STATE_SD_STOP_REQ:
      #ifdef SDSUPPORT
        ANYCUBIC_SERIAL_PROTOCOLLNPGM("J16");// J16 stop print
        if((!card.sdprinting) && (!planner.movesplanned())) {
          // enter idle display state
          TFTstate=ANYCUBIC_TFT_STATE_IDLE;
          ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Serial Debug: SD print stopped... J16");
          ai3m_pause_state = 0;
          ANYCUBIC_TFT_DEBUG_ECHOLNPAIR(" DEBUG: AI3M Pause State: ", ai3m_pause_state);
        }
        // did we park the hotend already?
        if((!IsParked) && (!card.sdprinting) && (!planner.movesplanned())) {
          enqueue_and_echo_commands_P(PSTR("G91\nG1 E-1 F1800\nG90"));  //retract
          ParkAfterStop();
          IsParked = true;
        }
      #endif
      break;
    default:
      break;
  }
}

void AnycubicTFTClass::FilamentRunout()
{
  #if ENABLED(ANYCUBIC_FILAMENT_RUNOUT_SENSOR)
    FilamentTestStatus=READ(FIL_RUNOUT_PIN)&0xff;

    if(FilamentTestStatus>FilamentTestLastStatus) {
      // filament sensor pin changed, save current timestamp.
      const millis_t fil_ms = millis();
      static millis_t fil_delay;

      // since this is inside a loop, only set delay time once
      if (FilamentSetMillis) {
        ANYCUBIC_TFT_DEBUG_ECHOLNPGM("DEBUG: Set filament trigger time");
        // set the delayed timestamp to 3000ms later
        fil_delay = fil_ms + 3000UL;
        // this doesn't need to run until the filament is recovered again
        FilamentSetMillis=false;
      }

      // if three seconds passed and the sensor is still triggered,
      // we trigger the filament runout status
      if ((FilamentTestStatus>FilamentTestLastStatus) && (ELAPSED(fil_ms, fil_delay))) {
        if (!IsParked){
          ANYCUBIC_TFT_DEBUG_ECHOLNPGM("DEBUG: 3000ms delay done");
          if((card.sdprinting==true)) {
            ai3m_pause_state = 3; // set runout pause flag
            ANYCUBIC_TFT_DEBUG_ECHOLNPAIR(" DEBUG: AI3M Pause State: ", ai3m_pause_state);
            PausePrint();
          } else if((card.sdprinting==false)) {
            ANYCUBIC_SERIAL_PROTOCOLLNPGM("J15"); //J15 FILAMENT LACK
            ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Serial Debug: Filament runout... J15");
            FilamentTestLastStatus=FilamentTestStatus;
          }
        }
        FilamentTestLastStatus=FilamentTestStatus;
      }

    }
    else if(FilamentTestStatus!=FilamentTestLastStatus) {
      FilamentSetMillis=true; // set the timestamps on the next loop again
      FilamentTestLastStatus=FilamentTestStatus;
      ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Serial Debug: Filament runout recovered");
    }
  #endif
}

void AnycubicTFTClass::GetCommandFromTFT() {
    while (AnycubicSerial.available() > 0 && TFTbuflen < TFTBUFSIZE) {
        serial3_char = AnycubicSerial.read();
        if (serial3_char == '\n' || serial3_char == '\r' ||
            serial3_char == ':' || serial3_count >= (TFT_MAX_CMD_SIZE - 1)) {
            if (!serial3_count) {  // if empty line
                return;
            }

            TFTcmdbuffer[TFTbufindw][serial3_count] = 0;  // terminate string

            if ((strchr(TFTcmdbuffer[TFTbufindw], 'A') != NULL)) {
                TFTstrchr_pointer = strchr(TFTcmdbuffer[TFTbufindw], 'A');
                processCommandFromTFT();
            }
            TFTbufindw = (TFTbufindw + 1) % TFTBUFSIZE;
            TFTbuflen += 1;
            serial3_count = 0;  // clear buffer
        } else {
            TFTcmdbuffer[TFTbufindw][serial3_count++] = serial3_char;
        }
    }
}

void AnycubicTFTClass::processCommandFromTFT() {
    
    int16_t a_command = ((int)((strtod(&TFTcmdbuffer[TFTbufindw][TFTstrchr_pointer - TFTcmdbuffer[TFTbufindw] + 1], NULL))));

    #ifdef ANYCUBIC_TFT_DEBUG
        if ((a_command > 7) &&
            (a_command != 20))  // No debugging of status polls, please!
            SERIAL_ECHOLNPAIR("TFT Serial Command: ", TFTcmdbuffer[TFTbufindw]);
    #endif

    switch (a_command) {
        case A0_GET_HOTEND_TEMPERATURE:
            ANYCUBIC_SERIAL_PROTOCOLPGM("A0V ");
            ANYCUBIC_SERIAL_PROTOCOLLN(itostr3(int(thermalManager.degHotend(0) + 0.5)));
            break;

        case A1_GET_HOTEND_TARGET_TEMPERATURE:
            ANYCUBIC_SERIAL_PROTOCOLPGM("A1V ");
            ANYCUBIC_SERIAL_PROTOCOLLN(itostr3(int(thermalManager.degTargetHotend(0) + 0.5)));
            break;

        case A2_GET_HOTBED_TEMPERATURE:  // A2 GET HOTBED TEMP
            ANYCUBIC_SERIAL_PROTOCOLPGM("A2V ");
            ANYCUBIC_SERIAL_PROTOCOLLN(itostr3(int(thermalManager.degBed() + 0.5)));
            break;

        case A3_GET_HOTBED_TARGET_TEMPERATURE:  // A3 GET HOTBED TARGET TEMP
            ANYCUBIC_SERIAL_PROTOCOLPGM("A3V ");
            ANYCUBIC_SERIAL_PROTOCOLLN(itostr3(int(thermalManager.degTargetBed() + 0.5)));
            break;

        case A4_GET_FAN_SPEED:  // A4 GET FAN SPEED
            { unsigned int temp = ((fanSpeeds[0] * 100) / 255);
            temp = constrain(temp, 0, 100);

            ANYCUBIC_SERIAL_PROTOCOLPGM("A4V ");
            ANYCUBIC_SERIAL_PROTOCOLLN(temp); }
            break;
        case A5_GET_CURRENT_COORDINATE:  // A5 GET CURRENT COORDINATE
            ANYCUBIC_SERIAL_PROTOCOLPGM("A5V X: ");
            ANYCUBIC_SERIAL_PROTOCOL(current_position[X_AXIS]);
            ANYCUBIC_SERIAL_PROTOCOLPGM(" Y: ");
            ANYCUBIC_SERIAL_PROTOCOL(current_position[Y_AXIS]);
            ANYCUBIC_SERIAL_PROTOCOLPGM(" Z: ");
            ANYCUBIC_SERIAL_PROTOCOL(current_position[Z_AXIS]);
            ANYCUBIC_SERIAL_PROTOCOLLNPGM(" ");
            break;
        case A6_GET_SD_CARD_PRINTING_STATUS:  // A6 GET SD CARD PRINTING STATUS
            doGetSdCardPrintingStatus();
            break;
        case A7_GET_PRINTING_TIME:  // A7 GET PRINTING TIME
            doGetPrintingTime();
            break;
        case A8_GET_SD_LIST:  // A8 GET  SD LIST
            doGetSdList();
            break;
        case A9_PAUSE_SD_PRINT:  // A9 pause sd print
            doPauseSdPrint();
            break;
        case A10_RESUME_SD_PRINT:  // A10 resume sd print
            doResumeSdPrint();
            break;
        case A11_STOP_SD_PRINT:  // A11 STOP SD PRINT
            doStopSdPrint();
            break;
        case A12_KILL:  // A12 kill
            kill(PSTR(MSG_KILLED));
            break;
        case A13_SELECTION_FILE:  // A13 SELECTION FILE
            doSelectFile();
            break;
        case A14_START_PRINTING:  // A14 START PRINTING
            doStartPrinting();
            break;
        case A15_RESUMING_FROM_OUTAGE:  // A15 RESUMING FROM OUTAGE
            // if((!planner.movesplanned())&&(!TFTresumingflag))
            //  {
            //    if(card.cardOK)
            //      FlagResumFromOutage=true;
            //      ResumingFlag=1;
            //      card.startFileprint();
            //      starttime=millis();
            //      ANYCUBIC_SERIAL_SUCC_START;
            //  }
            // ANYCUBIC_SERIAL_ENTER();
            break;
        case A16_SET_HOTEND_TEMPERATURE:  // A16 set hotend temp
            doSetHotEndTemp();
            break;
        case A17_SET_HEATED_BED_TEMPERATURE:  // A17 set heated bed temp
            doSetHotBedTemp();
            break;
        case A18_SET_FAN_SPEED:  // A18 set fan speed
            doSetFanSpeed();
            break;
        case A19_STOP_STEPPER_DRIVERS:  // A19 stop stepper drivers
            doStopSteppers();
            break;
        case A20_READ_PRINTING_SPEED:  // A20 read printing speed
            doReportPrintingSpeed();
            break;
        case A21_ALL_HOME:  // A21 all home
            doHomePrinter();
            break;
        case A22_MOVE_XYZ_EXTRUDE:  // A22 move X/Y/Z or extrude
            doMoveXYZorExtrude();
            break;
        case A23_PREHEAT_PLA:  // A23 preheat pla
            doPreHeatPrinter(50, 200, 0);
            break;
        case A24_PREHEAT_ABS:  // A24 preheat abs
            doPreHeatPrinter(80, 240, 0);
            break;
        case A25_COOL_DOWN:  // A25 cool down
            doPrinterCoolDown();
            break;
        case A26_REFRESH_SD:  // A26 refresh SD or change directory
            doRefershSD();
            break;
        #ifdef SERVO_ENDSTOPS
        case A27_SERVOS_ANGLES_ADJUST:  // A27 servos angles  adjust
            break;
        #endif
        case A28_FILAMENT_TEST:  // A28 filament test
            if (CodeSeen('O')) {
                ;
            } else if (CodeSeen('C')) {
                ;
            }
            ANYCUBIC_SERIAL_ENTER();
            break;
        case A29_Z_PROBE_OFFESET_SET:  // A29 Z PROBE OFFESET SET
            //not in use
            break;

        case A30_ASSIST_LEVELING:  // A30 assist leveling, the original function was canceled
            doAssistLeveling();
            break;
        case A31_ZOFFSET:  // A31 zoffset
            doZOffset();
            
            break;
        case A32_CLEAN_LEVELING_BEEP_FLAG:  // A32 clean leveling beep flag
            doCleanLevelingBeepFlag();
            break;
        case A33_GET_VERSION_INFO:  // A33 get version info
            ANYCUBIC_SERIAL_PROTOCOLPGM("J33 ");
            ANYCUBIC_SERIAL_PROTOCOLLNPGM(MSG_MY_VERSION);
            break;
        default:
            ANYCUBIC_TFT_DEBUG_ECHOLNPGM(a_command);
            break;
    }
}

void AnycubicTFTClass::doGetSdCardPrintingStatus() {
#ifdef SDSUPPORT
    ANYCUBIC_SERIAL_PROTOCOLPGM("A6V ");
    if (card.sdprinting) {
        if (card.cardOK) {
            ANYCUBIC_SERIAL_PROTOCOL(itostr3(card.percentDone()));
        } else {
            ANYCUBIC_SERIAL_PROTOCOLPGM("J02");
        }
    } else {
        ANYCUBIC_SERIAL_PROTOCOLPGM("---");
    }
    ANYCUBIC_SERIAL_ENTER();
#endif
};

void AnycubicTFTClass::doGetPrintingTime() {
    ANYCUBIC_SERIAL_PROTOCOLPGM("A7V ");
    if (starttime == 0) {  // print time
        ANYCUBIC_SERIAL_PROTOCOLLNPGM(" 999:999");
    } else {
        uint16_t time = millis() / 60000 - starttime / 60000;
        ANYCUBIC_SERIAL_PROTOCOL(itostr2(time / 60));
        ANYCUBIC_SERIAL_PROTOCOLPGM(" H ");
        ANYCUBIC_SERIAL_PROTOCOL(itostr2(time % 60));
        ANYCUBIC_SERIAL_PROTOCOLLNPGM(" M");
    }
}

void AnycubicTFTClass::doGetSdList() {
#ifdef SDSUPPORT
    SelectedDirectory[0] = 0;
    if (!IS_SD_INSERTED()) {
        ANYCUBIC_SERIAL_PROTOCOLLNPGM("J02");
    } else {
        if (CodeSeen('S')) {
          filenumber = CodeValue();
        }

        ANYCUBIC_SERIAL_PROTOCOLLNPGM("FN ");  // Filelist start
        ls();
        ANYCUBIC_SERIAL_PROTOCOLLNPGM("END");  // Filelist stop
    }
#endif
}

void AnycubicTFTClass::doRefershSD() {
#ifdef SDSUPPORT
    if (SelectedDirectory[0] == 0) {
        card.initsd();
    } else if (SelectedDirectory[0] == '<') {
        HandleSpecialMenu();
    } else if ((SelectedDirectory[0] == '.') && (SelectedDirectory[1] == '.')) {
        card.updir();
    } else {
      card.chdir(SelectedDirectory);
    }

    SelectedDirectory[0] = 0;

    if (!IS_SD_INSERTED()) {
        ANYCUBIC_SERIAL_PROTOCOLLNPGM("J02");  // J02 SD Card initilized
        ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Serial Debug: SD card initialized... J02");
    }
#endif
}

void AnycubicTFTClass::doPauseSdPrint() {
#ifdef SDSUPPORT
    if (card.sdprinting) {
        PausePrint();
    } else {
        ai3m_pause_state = 0;
        ANYCUBIC_TFT_DEBUG_ECHOLNPAIR(" DEBUG: AI3M Pause State: ", ai3m_pause_state);
        StopPrint();
    }
#endif
}

void AnycubicTFTClass::doResumeSdPrint() {
#ifdef SDSUPPORT
    if ((TFTstate == ANYCUBIC_TFT_STATE_SD_PAUSE) ||
        (TFTstate == ANYCUBIC_TFT_STATE_SD_OUTAGE)) {
        StartPrint();
        ANYCUBIC_SERIAL_PROTOCOLLNPGM("J04");  // J04 printing form sd card now
        ANYCUBIC_TFT_DEBUG_ECHOLNPGM( "TFT Serial Debug: SD print started... J04");
    }
    if (nozzle_timed_out) {
        ReheatNozzle();
    }
#endif
}

void AnycubicTFTClass::doStopSdPrint() {
#ifdef SDSUPPORT
    if (card.sdprinting || (TFTstate == ANYCUBIC_TFT_STATE_SD_OUTAGE)) {
        StopPrint();
    } else {
        ANYCUBIC_SERIAL_PROTOCOLLNPGM("J16");
        TFTstate = ANYCUBIC_TFT_STATE_IDLE;
        ai3m_pause_state = 0;
        ANYCUBIC_TFT_DEBUG_ECHOLNPAIR(" DEBUG: AI3M Pause State: ",
                                      ai3m_pause_state);
    }
#endif
}

void AnycubicTFTClass::doSelectFile() {
#ifdef SDSUPPORT
    if (TFTstate == ANYCUBIC_TFT_STATE_SD_OUTAGE) {
        return;
    }

    char *starpos = NULL;
    starpos = strchr(TFTstrchr_pointer + 4, '*');
    if (TFTstrchr_pointer[4] == '/') {
        strcpy(SelectedDirectory, TFTstrchr_pointer + 5);
    } else if (TFTstrchr_pointer[4] == '<') {
        strcpy(SelectedDirectory, TFTstrchr_pointer + 4);
    } else {
        SelectedDirectory[0] = 0;

        if (starpos != NULL) {
            *(starpos - 1) = '\0';
        }
        card.openFile(TFTstrchr_pointer + 4, true);
        if (card.isFileOpen()) {
            ANYCUBIC_SERIAL_PROTOCOLLNPGM("J20");  // J20 Open successful
            ANYCUBIC_TFT_DEBUG_ECHOLNPGM(
                "TFT Serial Debug: File open successful... J20");
        } else {
            ANYCUBIC_SERIAL_PROTOCOLLNPGM("J21");  // J21 Open failed
            ANYCUBIC_TFT_DEBUG_ECHOLNPGM(
                "TFT Serial Debug: File open failed... J21");
        }
    }
    ANYCUBIC_SERIAL_ENTER();

#endif
}

void AnycubicTFTClass::doStartPrinting() {
#ifdef SDSUPPORT
    if (isPrinterReadyForCmd() && card.isFileOpen()) {
        ai3m_pause_state = 0;
        ANYCUBIC_TFT_DEBUG_ECHOLNPGM("DEBUG: AI3M Pause State: 0");
        StartPrint();
        IsParked = false;
        ANYCUBIC_SERIAL_PROTOCOLLNPGM("J04");  // J04 Starting Print
        ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Serial Debug: Starting SD Print... J04");
    }
#endif
}

void AnycubicTFTClass::doSetHotEndTemp() {
    unsigned int tempvalue;
    if (CodeSeen('S')) {
        tempvalue = constrain(CodeValue(), 0, 275);
        thermalManager.setTargetHotend(tempvalue, 0);
    } else if (CodeSeen('C') && (!planner.movesplanned())) {
        raiseZAxisIfNeeded();
        tempvalue = constrain(CodeValue(), 0, 275);
        thermalManager.setTargetHotend(tempvalue, 0);
    }
}

void AnycubicTFTClass::doSetHotBedTemp() {
    if (CodeSeen('S')) {
        unsigned int tempbed = constrain(CodeValue(), 0, 150);
        thermalManager.setTargetBed(tempbed);
    }
}

void AnycubicTFTClass::doSetFanSpeed() {
    if (CodeSeen('S')) {
        unsigned int temp = (CodeValue() * 255 / 100);
        temp = constrain(temp, 0, 255);
        fanSpeeds[0] = temp;
    } else {
        fanSpeeds[0] = 255;
    }
    ANYCUBIC_SERIAL_ENTER();
}

void AnycubicTFTClass::doStopSteppers() {
#ifdef SDSUPPORT
    if ((!planner.movesplanned()) && (!card.sdprinting)) {
#else
    if (!planner.movesplanned()) {
#endif
        quickstop_stepper();
        disable_X();
        disable_Y();
        disable_Z();
        disable_E0();
    }
    ANYCUBIC_SERIAL_ENTER();
}

void AnycubicTFTClass::doReportPrintingSpeed() {
    if (CodeSeen('S')) {
        feedrate_percentage = constrain(CodeValue(), 40, 999);
    } else {
        ANYCUBIC_SERIAL_PROTOCOLPGM("A20V ");
        ANYCUBIC_SERIAL_PROTOCOLLN(feedrate_percentage);
    }
}

void AnycubicTFTClass::doHomePrinter() {
    if (isPrinterReadyForCmd()) {
        if (CodeSeen('C')) {
            enqueue_and_echo_commands_P(PSTR("G28"));
        } else {
            if (CodeSeen('X')) enqueue_and_echo_commands_P(PSTR("G28 X"));
            if (CodeSeen('Y')) enqueue_and_echo_commands_P(PSTR("G28 Y"));
            if (CodeSeen('Z')) enqueue_and_echo_commands_P(PSTR("G28 Z"));
        }
    }
}

void AnycubicTFTClass::doMoveXYZorExtrude() {
    if (isPrinterReadyForCmd()) {
        float coorvalue;
        unsigned int movespeed = 0;
        char value[30];
        if (CodeSeen('F')) {  // Set feedrate
            movespeed = CodeValue();
        }

        enqueue_and_echo_commands_P(PSTR("G91"));  // relative coordinates

        if (CodeSeen('X')) {  // Move in X direction
            coorvalue = CodeValue();
            if ((coorvalue <= 0.2) && coorvalue > 0) {
                sprintf_P(value, PSTR("G1 X0.1F%i"), movespeed);
            } else if ((coorvalue <= -0.1) && coorvalue > -1) {
                sprintf_P(value, PSTR("G1 X-0.1F%i"), movespeed);
            } else {
                sprintf_P(value, PSTR("G1 X%iF%i"), int(coorvalue), movespeed);
            }
            enqueue_and_echo_command(value);
        } else if (CodeSeen('Y')) {  // Move in Y direction
            coorvalue = CodeValue();
            if ((coorvalue <= 0.2) && coorvalue > 0) {
                sprintf_P(value, PSTR("G1 Y0.1F%i"), movespeed);
            } else if ((coorvalue <= -0.1) && coorvalue > -1) {
                sprintf_P(value, PSTR("G1 Y-0.1F%i"), movespeed);
            } else {
                sprintf_P(value, PSTR("G1 Y%iF%i"), int(coorvalue), movespeed);
            }
            enqueue_and_echo_command(value);
        } else if (CodeSeen('Z')) {  // Move in Z direction
            coorvalue = CodeValue();
            if ((coorvalue <= 0.2) && coorvalue > 0) {
                sprintf_P(value, PSTR("G1 Z0.1F%i"), movespeed);
            } else if ((coorvalue <= -0.1) && coorvalue > -1) {
                sprintf_P(value, PSTR("G1 Z-0.1F%i"), movespeed);
            } else {
                sprintf_P(value, PSTR("G1 Z%iF%i"), int(coorvalue), movespeed);
            }
            enqueue_and_echo_command(value);
        } else if (CodeSeen('E')) {  // Extrude
            coorvalue = CodeValue();
            if ((coorvalue <= 0.2) && coorvalue > 0) {
                sprintf_P(value, PSTR("G1 E0.1F%i"), movespeed);
            } else if ((coorvalue <= -0.1) && coorvalue > -1) {
                sprintf_P(value, PSTR("G1 E-0.1F%i"), movespeed);
            } else {
                sprintf_P(value, PSTR("G1 E%iF500"), int(coorvalue));
            }
            enqueue_and_echo_command(value);
        }
        enqueue_and_echo_commands_P(PSTR("G90"));  // absolute coordinates
    }
    ANYCUBIC_SERIAL_ENTER();
}

void AnycubicTFTClass::doPreHeatPrinter(const int16_t celsiusBed, const int16_t celsiusHotEnd, const uint8_t hotEnd2) {
    if (isPrinterReadyForCmd()) {
        raiseZAxisIfNeeded();  // RAISE Z AXIS
        thermalManager.setTargetBed(celsiusBed);
        thermalManager.setTargetHotend(celsiusHotEnd, hotEnd2);
        ANYCUBIC_SERIAL_SUCC_START;
        ANYCUBIC_SERIAL_ENTER();
    }
}

void AnycubicTFTClass::doPrinterCoolDown() {
    if (isPrinterReadyForCmd()) {
        thermalManager.setTargetHotend(0, 0);
        thermalManager.setTargetBed(0);
        ANYCUBIC_SERIAL_PROTOCOLLNPGM("J12");  // J12 cool down
        ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Serial Debug: Cooling down... J12");
    }
}

void AnycubicTFTClass::doAssistLeveling() {
    if (CodeSeen('S')) {
        ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Entering level menue...");
    } else if (CodeSeen('O')) {
        ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Leveling started and movint to front left...");
        enqueue_and_echo_commands_P(PSTR("G91\nG1 Z10 F240\nG90\nG28\nG29\nG1 X20 Y20 F6000\nG1 Z0 F240"));
    } else if (CodeSeen('T')) {
        ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Level checkpoint front right...");
        enqueue_and_echo_commands_P(PSTR("G1 Z5 F240\nG1 X190 Y20 F6000\nG1 Z0 F240"));
    } else if (CodeSeen('C')) {
        ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Level checkpoint back right...");
        enqueue_and_echo_commands_P(PSTR("G1 Z5 F240\nG1 X190 Y190 F6000\nG1 Z0 F240"));
    } else if (CodeSeen('Q')) {
        ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Level checkpoint back right...");
        enqueue_and_echo_commands_P(PSTR("G1 Z5 F240\nG1 X190 Y20 F6000\nG1 Z0 F240"));
    } else if (CodeSeen('H')) {
        ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Level check no heating...");
        // enqueue_and_echo_commands_P(PSTR("... TBD ..."));
        ANYCUBIC_SERIAL_PROTOCOLLNPGM("J22");  // J22 Test print done
        ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Serial Debug: Leveling print test done... J22");
    } else if (CodeSeen('L')) {
        ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Level check heating...");
        // enqueue_and_echo_commands_P(PSTR("... TBD ..."));
        ANYCUBIC_SERIAL_PROTOCOLLNPGM("J22");  // J22 Test print done
        ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Serial Debug: Leveling print test with heating done... J22");
    }
    ANYCUBIC_SERIAL_SUCC_START;
    ANYCUBIC_SERIAL_ENTER();
}

void AnycubicTFTClass::doZOffset() {
#if HAS_BED_PROBE
    if (isPrinterReadyForCmd()) {
        char value[30];
        char *s_zoffset;
        // if((current_position[Z_AXIS]<10))
        //  z_offset_auto_test();

        if (CodeSeen('S')) {
            ANYCUBIC_SERIAL_PROTOCOLPGM("A9V ");
            ANYCUBIC_SERIAL_PROTOCOL(
                itostr3(int(zprobe_zoffset * 100.00 + 0.5)));
            ANYCUBIC_SERIAL_ENTER();
            ANYCUBIC_TFT_DEBUG_ECHOPGM(
                "TFT sending current z-probe offset data... <\r\nA9V ");
            ANYCUBIC_TFT_DEBUG_ECHO(
                itostr3(int(zprobe_zoffset * 100.00 + 0.5)));
            ANYCUBIC_TFT_DEBUG_ECHOLNPGM(">");
        }
        if (CodeSeen('D')) {
            s_zoffset = ftostr32(float(CodeValue()) / 100.0);
            sprintf_P(value, PSTR("M851 Z"));
            strcat(value, s_zoffset);
            enqueue_and_echo_command(value);            // Apply Z-Probe offset
            enqueue_and_echo_commands_P(PSTR("M500"));  // Save to EEPROM
        }
    }
#endif
    ANYCUBIC_SERIAL_ENTER();
}

void AnycubicTFTClass::doCleanLevelingBeepFlag(){
    if (CodeSeen('S')) {
        ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Level saving data...");
        enqueue_and_echo_commands_P(PSTR("M500\nM420 S1\nG1 Z10 F240\nG1 X0 Y0 F6000"));
        ANYCUBIC_SERIAL_SUCC_START;
        ANYCUBIC_SERIAL_ENTER();
    }
}

bool AnycubicTFTClass::isPrinterReadyForCmd() {
    return (!planner.movesplanned()) &&
           (TFTstate != ANYCUBIC_TFT_STATE_SD_PAUSE) &&
           (TFTstate != ANYCUBIC_TFT_STATE_SD_OUTAGE);
}

void AnycubicTFTClass::raiseZAxisIfNeeded() {
    if (current_position[Z_AXIS] < 10) {
        enqueue_and_echo_commands_P(PSTR("G1 Z10"));  // RASE Z AXIS
    }
}

void AnycubicTFTClass::CommandScan() {
  CheckHeaterError();
  CheckSDCardChange();
  StateHandler();

  if(TFTbuflen<(TFTBUFSIZE-1)) {
      GetCommandFromTFT();
  }

  if(TFTbuflen) {
    TFTbuflen = (TFTbuflen-1);
    TFTbufindr = (TFTbufindr + 1)%TFTBUFSIZE;
  }
}

void AnycubicTFTClass::HeatingStart()
{
  ANYCUBIC_SERIAL_PROTOCOLLNPGM("J06"); // J07 hotend heating start
  ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Serial Debug: Nozzle is heating... J06");
}

void AnycubicTFTClass::HeatingDone()
{
  ANYCUBIC_SERIAL_PROTOCOLLNPGM("J07"); // J07 hotend heating done
  ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Serial Debug: Nozzle heating is done... J07");

  if(TFTstate==ANYCUBIC_TFT_STATE_SD_PRINT)
  {
    ANYCUBIC_SERIAL_PROTOCOLLNPGM("J04"); // J04 printing from sd card
    ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Serial Debug: Continuing SD print after heating... J04");
  }
}

void AnycubicTFTClass::BedHeatingStart()
{
  ANYCUBIC_SERIAL_PROTOCOLLNPGM("J08"); // J08 hotbed heating start
  ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Serial Debug: Bed is heating... J08");
}

void AnycubicTFTClass::BedHeatingDone()
{
  ANYCUBIC_SERIAL_PROTOCOLLNPGM("J09"); // J09 hotbed heating done
  ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Serial Debug: Bed heating is done... J09");
}


AnycubicTFTClass AnycubicTFT;
#endif
