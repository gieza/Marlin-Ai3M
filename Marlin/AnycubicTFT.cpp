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
      TFTstate=ANYCUBIC_TFT_STATE_SDPRINT;
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
      TFTstate=ANYCUBIC_TFT_STATE_SDPRINT;
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
      TFTstate=ANYCUBIC_TFT_STATE_SDPAUSE_REQ;
      ANYCUBIC_TFT_DEBUG_ECHOLNPGM("DEBUG: Set Pause again because of timeout");

      // clear the timeout flag to ensure the print continues on the
      // next push of CONTINUE
      ai3m_pause_state = 2;
      ANYCUBIC_TFT_DEBUG_ECHOLNPGM("DEBUG: Nozzle timeout flag cleared");

      break;
    case 5:
      // nozzle was timed out before (runout), do not enter printing state yet
      TFTstate=ANYCUBIC_TFT_STATE_SDPAUSE_REQ;
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
      ANYCUBIC_TFT_DEBUG_ECHOLNPAIR(" DEBUG: AI3M Pause State: ", ai3m_pause_state);
      
      IsParked = true;
      // show filament runout prompt on screen
      ANYCUBIC_SERIAL_PROTOCOLLNPGM("J23");
      
      ANYCUBIC_TFT_DEBUG_ECHOLNPGM("DEBUG: J23 Show filament prompt");
    }
  #endif
  TFTstate=ANYCUBIC_TFT_STATE_SDPAUSE_REQ;
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
  wait_for_heatup=false;
  ai3m_pause_state = 0;
  ANYCUBIC_TFT_DEBUG_ECHOLNPAIR(" DEBUG: AI3M Pause State: ", ai3m_pause_state);
  
  TFTstate=ANYCUBIC_TFT_STATE_SDSTOP_REQ;
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
  ANYCUBIC_TFT_DEBUG_ECHOLNPAIR(" DEBUG: AI3M Pause State: ", ai3m_pause_state);

  // call M600 and set display state to paused
  enqueue_and_echo_commands_P(PSTR("M600"));
  TFTstate=ANYCUBIC_TFT_STATE_SDPAUSE_REQ;
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
  TFTstate=ANYCUBIC_TFT_STATE_SDPAUSE_REQ;
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

float AnycubicTFTClass::CodeValue()
{
  return (strtod(&TFTcmdbuffer[TFTbufindr][TFTstrchr_pointer - TFTcmdbuffer[TFTbufindr] + 1], NULL));
}

bool AnycubicTFTClass::CodeSeen(char code)
{
  TFTstrchr_pointer = strchr(TFTcmdbuffer[TFTbufindr], code);
  return (TFTstrchr_pointer != NULL); //Return True if a character was found
}

void AnycubicTFTClass::HandleSpecialMenu()
{
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

void AnycubicTFTClass::Ls()
{
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
    else if(card.cardOK)
    {
      uint16_t cnt=filenumber;
      uint16_t max_files;
      uint16_t dir_files=card.getnrfilenames();

      if((dir_files-filenumber)<4)
      {
        max_files=dir_files;
      } else {
        max_files=filenumber+3;
      }

      for(cnt=filenumber; cnt<=max_files; cnt++)
      {
        if (cnt==0) // Special Entry
        {
          if(strcmp(card.getWorkDirName(),"/") == 0) {
            ANYCUBIC_SERIAL_PROTOCOLLNPGM("<Special Menu>\r\n<Special Menu>");
            SERIAL_PROTOCOL(cnt);
            SERIAL_PROTOCOLLNPGM("<Special_Menu>");
          } else {
            ANYCUBIC_SERIAL_PROTOCOLLNPGM("/..\r\n/..");
            SERIAL_PROTOCOL(cnt);
            SERIAL_PROTOCOLLNPGM("/..");
          }
        } else {
          card.getfilename(cnt-1);
          //      card.getfilename(cnt);

          if(card.filenameIsDir) {
            ANYCUBIC_SERIAL_PROTOCOLPGM("/");
            ANYCUBIC_SERIAL_PROTOCOLLN(card.filename);
            ANYCUBIC_SERIAL_PROTOCOLPGM("/");
            ANYCUBIC_SERIAL_PROTOCOLLN(card.longFilename);
            SERIAL_PROTOCOL(cnt);
            SERIAL_PROTOCOLPGM("/");
            SERIAL_PROTOCOLLN(card.longFilename);
          } else {
            ANYCUBIC_SERIAL_PROTOCOLLN(card.filename);
            ANYCUBIC_SERIAL_PROTOCOLLN(card.longFilename);
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
          TFTstate=ANYCUBIC_TFT_STATE_SDPRINT;
          starttime=millis();

          // --> Send print info to display... most probably print started via gcode
        }
      #endif
      break;
    case ANYCUBIC_TFT_STATE_SDPRINT:
      #ifdef SDSUPPORT
        if(!card.sdprinting) {
          // It seems that we are to printing anymore... pause or stopped?
          if (card.isFileOpen()) {
            // File is still open --> paused
            TFTstate=ANYCUBIC_TFT_STATE_SDPAUSE;
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
    case ANYCUBIC_TFT_STATE_SDPAUSE:
      break;
    case ANYCUBIC_TFT_STATE_SDPAUSE_OOF:
      #ifdef ANYCUBIC_FILAMENT_RUNOUT_SENSOR
        if(!FilamentTestStatus) {
          // We got filament again
          TFTstate=ANYCUBIC_TFT_STATE_SDPAUSE;
        }
      #endif
      break;
    case ANYCUBIC_TFT_STATE_SDPAUSE_REQ:
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
              TFTstate=ANYCUBIC_TFT_STATE_SDPAUSE;
            } else {
              // Pause because of "out of filament"
              TFTstate=ANYCUBIC_TFT_STATE_SDPAUSE_OOF;
            }
          #endif
          ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Serial Debug: SD print paused done... J18");
        }
      #endif
      break;
    case ANYCUBIC_TFT_STATE_SDSTOP_REQ:
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

void AnycubicTFTClass::GetCommandFromTFT()
{
  char *starpos = NULL;
  while( AnycubicSerial.available() > 0  && TFTbuflen < TFTBUFSIZE)
  {
    serial3_char = AnycubicSerial.read();
    if(serial3_char == '\n' ||
    serial3_char == '\r' ||
    serial3_char == ':'  ||
    serial3_count >= (TFT_MAX_CMD_SIZE - 1) )
    {
      if(!serial3_count) { //if empty line
        return;
      }

      TFTcmdbuffer[TFTbufindw][serial3_count] = 0; //terminate string

      if((strchr(TFTcmdbuffer[TFTbufindw], 'A') != NULL)) {
        int16_t a_command;
        TFTstrchr_pointer = strchr(TFTcmdbuffer[TFTbufindw], 'A');
        a_command=((int)((strtod(&TFTcmdbuffer[TFTbufindw][TFTstrchr_pointer - TFTcmdbuffer[TFTbufindw] + 1], NULL))));

        #ifdef ANYCUBIC_TFT_DEBUG
        if ((a_command>7) && (a_command != 20)) // No debugging of status polls, please!
        SERIAL_ECHOLNPAIR("TFT Serial Command: ", TFTcmdbuffer[TFTbufindw]);
        #endif

        switch(a_command) {

          case 0: //A0 GET HOTEND TEMP
            ANYCUBIC_SERIAL_PROTOCOLPGM("A0V ");
            ANYCUBIC_SERIAL_PROTOCOLLN(itostr3(int(thermalManager.degHotend(0) + 0.5)));
            break;

          case 1: //A1  GET HOTEND TARGET TEMP
            ANYCUBIC_SERIAL_PROTOCOLPGM("A1V ");
            ANYCUBIC_SERIAL_PROTOCOLLN(itostr3(int(thermalManager.degTargetHotend(0) + 0.5)));
            break;

          case 2: //A2 GET HOTBED TEMP
            ANYCUBIC_SERIAL_PROTOCOLPGM("A2V ");
            ANYCUBIC_SERIAL_PROTOCOLLN(itostr3(int(thermalManager.degBed() + 0.5)));
            break;

          case 3: //A3 GET HOTBED TARGET TEMP
            ANYCUBIC_SERIAL_PROTOCOLPGM("A3V ");
            ANYCUBIC_SERIAL_PROTOCOLLN(itostr3(int(thermalManager.degTargetBed() + 0.5)));
            break;

          case 4://A4 GET FAN SPEED
            {
              unsigned int temp;

              temp=((fanSpeeds[0]*100)/255);
              temp=constrain(temp,0,100);

              ANYCUBIC_SERIAL_PROTOCOLPGM("A4V ");
              ANYCUBIC_SERIAL_PROTOCOLLN(temp);
            }
            break;
          case 5:// A5 GET CURRENT COORDINATE
            ANYCUBIC_SERIAL_PROTOCOLPGM("A5V X: ");
            ANYCUBIC_SERIAL_PROTOCOL(current_position[X_AXIS]);
            ANYCUBIC_SERIAL_PROTOCOLPGM(" Y: ");
            ANYCUBIC_SERIAL_PROTOCOL(current_position[Y_AXIS]);
            ANYCUBIC_SERIAL_PROTOCOLPGM(" Z: ");
            ANYCUBIC_SERIAL_PROTOCOL(current_position[Z_AXIS]);
            ANYCUBIC_SERIAL_PROTOCOLLNPGM(" ");
            break;
          case 6: //A6 GET SD CARD PRINTING STATUS
            #ifdef SDSUPPORT
              if(card.sdprinting) {
                ANYCUBIC_SERIAL_PROTOCOLPGM("A6V ");
                if(card.cardOK)
                {
                  ANYCUBIC_SERIAL_PROTOCOL(itostr3(card.percentDone()));
                }
                else
                {
                  ANYCUBIC_SERIAL_PROTOCOLPGM("J02");
                }
              }
              else
              ANYCUBIC_SERIAL_PROTOCOLPGM("A6V ---");
              ANYCUBIC_SERIAL_ENTER();
            #endif
            break;
          case 7://A7 GET PRINTING TIME
            {
              ANYCUBIC_SERIAL_PROTOCOLPGM("A7V ");
              if(starttime != 0) // print time
              {
                uint16_t time = millis()/60000 - starttime/60000;
                ANYCUBIC_SERIAL_PROTOCOL(itostr2(time/60));
                ANYCUBIC_SERIAL_PROTOCOLPGM(" H ");
                ANYCUBIC_SERIAL_PROTOCOL(itostr2(time%60));
                ANYCUBIC_SERIAL_PROTOCOLLNPGM(" M");
              }else{
                ANYCUBIC_SERIAL_PROTOCOLLNPGM(" 999:999");
              }
            break;
          }
          case 8: // A8 GET  SD LIST
            #ifdef SDSUPPORT
              SelectedDirectory[0]=0;
              if(!IS_SD_INSERTED())
              {
                ANYCUBIC_SERIAL_PROTOCOLLNPGM("J02");
              }
              else
              {
                if(CodeSeen('S'))
                filenumber=CodeValue();

                ANYCUBIC_SERIAL_PROTOCOLLNPGM("FN "); // Filelist start
                Ls();
                ANYCUBIC_SERIAL_PROTOCOLLNPGM("END"); // Filelist stop
              }
            #endif
            break;
          case 9: // A9 pause sd print
            #ifdef SDSUPPORT
              if(card.sdprinting)
              {
                PausePrint();
              }
              else
              {
                ai3m_pause_state = 0;
                ANYCUBIC_TFT_DEBUG_ECHOLNPAIR(" DEBUG: AI3M Pause State: ", ai3m_pause_state);
                StopPrint();
              }
            #endif
            break;
          case 10: // A10 resume sd print
            #ifdef SDSUPPORT
              if((TFTstate==ANYCUBIC_TFT_STATE_SDPAUSE) || (TFTstate==ANYCUBIC_TFT_STATE_SDOUTAGE))
              {
                StartPrint();
                ANYCUBIC_SERIAL_PROTOCOLLNPGM("J04");// J04 printing form sd card now
                ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Serial Debug: SD print started... J04");
              }
              if (nozzle_timed_out) {
                ReheatNozzle();
              }
            #endif
            break;
          case 11: // A11 STOP SD PRINT
            #ifdef SDSUPPORT
              if((card.sdprinting) || (TFTstate==ANYCUBIC_TFT_STATE_SDOUTAGE))
              {
                StopPrint();
              } else {
                ANYCUBIC_SERIAL_PROTOCOLLNPGM("J16");
                TFTstate=ANYCUBIC_TFT_STATE_IDLE;
                ai3m_pause_state = 0;
                ANYCUBIC_TFT_DEBUG_ECHOLNPAIR(" DEBUG: AI3M Pause State: ", ai3m_pause_state);
              }
              #endif
            break;
          case 12: // A12 kill
            kill(PSTR(MSG_KILLED));
            break;
          case 13: // A13 SELECTION FILE
              #ifdef SDSUPPORT
              if((TFTstate!=ANYCUBIC_TFT_STATE_SDOUTAGE))
              {
                starpos = (strchr(TFTstrchr_pointer + 4,'*'));
                if (TFTstrchr_pointer[4] == '/') {
                  strcpy(SelectedDirectory, TFTstrchr_pointer+5);
                } else if (TFTstrchr_pointer[4] == '<') {
                  strcpy(SelectedDirectory, TFTstrchr_pointer+4);
                } else {
                  SelectedDirectory[0]=0;

                  if(starpos!=NULL)
                  *(starpos-1)='\0';
                  card.openFile(TFTstrchr_pointer + 4,true);
                  if (card.isFileOpen()) {
                    ANYCUBIC_SERIAL_PROTOCOLLNPGM("J20"); // J20 Open successful
                    ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Serial Debug: File open successful... J20");
                  } else {
                    ANYCUBIC_SERIAL_PROTOCOLLNPGM("J21"); // J21 Open failed
                    ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Serial Debug: File open failed... J21");
                  }
                }
                ANYCUBIC_SERIAL_ENTER();
              }
              #endif
            break;
          case 14: // A14 START PRINTING
            #ifdef SDSUPPORT
              if((!planner.movesplanned()) && (TFTstate!=ANYCUBIC_TFT_STATE_SDPAUSE) && (TFTstate!=ANYCUBIC_TFT_STATE_SDOUTAGE) && (card.isFileOpen()))
              {
                ai3m_pause_state = 0;
                ANYCUBIC_TFT_DEBUG_ECHOLNPAIR(" DEBUG: AI3M Pause State: ", ai3m_pause_state);
                StartPrint();
                IsParked = false;
                ANYCUBIC_SERIAL_PROTOCOLLNPGM("J04"); // J04 Starting Print
                ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Serial Debug: Starting SD Print... J04");
              }
            #endif
            break;
          case 15: // A15 RESUMING FROM OUTAGE
            //if((!planner.movesplanned())&&(!TFTresumingflag))
            //  {
            //    if(card.cardOK)
            //      FlagResumFromOutage=true;
            //      ResumingFlag=1;
            //      card.startFileprint();
            //      starttime=millis();
            //      ANYCUBIC_SERIAL_SUCC_START;
            //  }
            //ANYCUBIC_SERIAL_ENTER();
            break;
          case 16: // A16 set hotend temp
            {
              unsigned int tempvalue;
              if(CodeSeen('S'))
              {
                tempvalue=constrain(CodeValue(),0,275);
                thermalManager.setTargetHotend(tempvalue,0);
              }
              else if((CodeSeen('C'))&&(!planner.movesplanned()))
              {
                if((current_position[Z_AXIS]<10))
                enqueue_and_echo_commands_P(PSTR("G1 Z10")); //RASE Z AXIS
                tempvalue=constrain(CodeValue(),0,275);
                thermalManager.setTargetHotend(tempvalue,0);
              }
            }
            //  ANYCUBIC_SERIAL_ENTER();
            break;
          case 17:// A17 set heated bed temp
            {
              unsigned int tempbed;
              if(CodeSeen('S')) {tempbed=constrain(CodeValue(),0,150);
                thermalManager.setTargetBed(tempbed);}
            }
            //  ANYCUBIC_SERIAL_ENTER();
            break;
          case 18:// A18 set fan speed
            unsigned int temp;
            if (CodeSeen('S'))
            {
              temp=(CodeValue()*255/100);
              temp=constrain(temp,0,255);
              fanSpeeds[0]=temp;
            }
            else fanSpeeds[0]=255;
            ANYCUBIC_SERIAL_ENTER();
            break;
          case 19: // A19 stop stepper drivers
            if((!planner.movesplanned())
            #ifdef SDSUPPORT
            &&(!card.sdprinting)
            #endif
            )
            {
              quickstop_stepper();
              disable_X();
              disable_Y();
              disable_Z();
              disable_E0();
            }
            ANYCUBIC_SERIAL_ENTER();
            break;
          case 20:// A20 read printing speed
          {
            if(CodeSeen('S')) {
              feedrate_percentage=constrain(CodeValue(),40,999);
            }
            else{
              ANYCUBIC_SERIAL_PROTOCOLPGM("A20V ");
              ANYCUBIC_SERIAL_PROTOCOLLN(feedrate_percentage);
            }
          }
          break;
          case 21: // A21 all home
            if((!planner.movesplanned()) && (TFTstate!=ANYCUBIC_TFT_STATE_SDPAUSE) && (TFTstate!=ANYCUBIC_TFT_STATE_SDOUTAGE))
            {
              if(CodeSeen('X')||CodeSeen('Y')||CodeSeen('Z'))
              {
                if(CodeSeen('X')) enqueue_and_echo_commands_P(PSTR("G28 X"));
                if(CodeSeen('Y')) enqueue_and_echo_commands_P(PSTR("G28 Y"));
                if(CodeSeen('Z')) enqueue_and_echo_commands_P(PSTR("G28 Z"));
              }
              else if(CodeSeen('C')) enqueue_and_echo_commands_P(PSTR("G28"));
            }
            break;
          case 22: // A22 move X/Y/Z or extrude
            if((!planner.movesplanned()) && (TFTstate!=ANYCUBIC_TFT_STATE_SDPAUSE) && (TFTstate!=ANYCUBIC_TFT_STATE_SDOUTAGE))
            {
              float coorvalue;
              unsigned int movespeed=0;
              char value[30];
              if(CodeSeen('F')) // Set feedrate
              movespeed = CodeValue();

              enqueue_and_echo_commands_P(PSTR("G91")); // relative coordinates

              if(CodeSeen('X')) // Move in X direction
              {
                coorvalue=CodeValue();
                if((coorvalue<=0.2)&&coorvalue>0) {sprintf_P(value,PSTR("G1 X0.1F%i"),movespeed);}
                else if((coorvalue<=-0.1)&&coorvalue>-1) {sprintf_P(value,PSTR("G1 X-0.1F%i"),movespeed);}
                else {sprintf_P(value,PSTR("G1 X%iF%i"),int(coorvalue),movespeed);}
                enqueue_and_echo_command(value);
              }
              else if(CodeSeen('Y')) // Move in Y direction
              {
                coorvalue=CodeValue();
                if((coorvalue<=0.2)&&coorvalue>0) {sprintf_P(value,PSTR("G1 Y0.1F%i"),movespeed);}
                else if((coorvalue<=-0.1)&&coorvalue>-1) {sprintf_P(value,PSTR("G1 Y-0.1F%i"),movespeed);}
                else {sprintf_P(value,PSTR("G1 Y%iF%i"),int(coorvalue),movespeed);}
                enqueue_and_echo_command(value);
              }
              else if(CodeSeen('Z')) // Move in Z direction
              {
                coorvalue=CodeValue();
                if((coorvalue<=0.2)&&coorvalue>0) {sprintf_P(value,PSTR("G1 Z0.1F%i"),movespeed);}
                else if((coorvalue<=-0.1)&&coorvalue>-1) {sprintf_P(value,PSTR("G1 Z-0.1F%i"),movespeed);}
                else {sprintf_P(value,PSTR("G1 Z%iF%i"),int(coorvalue),movespeed);}
                enqueue_and_echo_command(value);
              }
              else if(CodeSeen('E')) // Extrude
              {
                coorvalue=CodeValue();
                if((coorvalue<=0.2)&&coorvalue>0) {sprintf_P(value,PSTR("G1 E0.1F%i"),movespeed);}
                else if((coorvalue<=-0.1)&&coorvalue>-1) {sprintf_P(value,PSTR("G1 E-0.1F%i"),movespeed);}
                else {sprintf_P(value,PSTR("G1 E%iF500"),int(coorvalue)); }
                enqueue_and_echo_command(value);
              }
              enqueue_and_echo_commands_P(PSTR("G90")); // absolute coordinates
            }
            ANYCUBIC_SERIAL_ENTER();
            break;
          case 23: // A23 preheat pla
            if((!planner.movesplanned())&& (TFTstate!=ANYCUBIC_TFT_STATE_SDPAUSE) && (TFTstate!=ANYCUBIC_TFT_STATE_SDOUTAGE))
            {
              if((current_position[Z_AXIS]<10)) enqueue_and_echo_commands_P(PSTR("G1 Z10")); // RAISE Z AXIS
              thermalManager.setTargetBed(50);
              thermalManager.setTargetHotend(200, 0);
              ANYCUBIC_SERIAL_SUCC_START;
              ANYCUBIC_SERIAL_ENTER();
            }
            break;
          case 24:// A24 preheat abs
            if((!planner.movesplanned()) && (TFTstate!=ANYCUBIC_TFT_STATE_SDPAUSE) && (TFTstate!=ANYCUBIC_TFT_STATE_SDOUTAGE))
            {
              if((current_position[Z_AXIS]<10)) enqueue_and_echo_commands_P(PSTR("G1 Z10")); //RAISE Z AXIS
              thermalManager.setTargetBed(80);
              thermalManager.setTargetHotend(240, 0);

              ANYCUBIC_SERIAL_SUCC_START;
              ANYCUBIC_SERIAL_ENTER();
            }
            break;
          case 25: // A25 cool down
            if((!planner.movesplanned())&& (TFTstate!=ANYCUBIC_TFT_STATE_SDPAUSE) && (TFTstate!=ANYCUBIC_TFT_STATE_SDOUTAGE))
            {
              thermalManager.setTargetHotend(0,0);
              thermalManager.setTargetBed(0);
              ANYCUBIC_SERIAL_PROTOCOLLNPGM("J12"); // J12 cool down
              ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Serial Debug: Cooling down... J12");
            }
            break;
          case 26: // A26 refresh SD
            #ifdef SDSUPPORT
              if (SelectedDirectory[0]==0) {
                card.initsd();
              } else {
                if ((SelectedDirectory[0] == '.') && (SelectedDirectory[1] == '.')) {
                  card.updir();
                } else {
                  if (SelectedDirectory[0] == '<') {
                    HandleSpecialMenu();
                  } else {
                    card.chdir(SelectedDirectory);
                  }
                }
              }

              SelectedDirectory[0]=0;

              if(!IS_SD_INSERTED())
              {
                ANYCUBIC_SERIAL_PROTOCOLLNPGM("J02"); // J02 SD Card initilized
                ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Serial Debug: SD card initialized... J02");
              }
            #endif
            break;
          #ifdef SERVO_ENDSTOPS
            case 27: // A27 servos angles  adjust
              break;
          #endif
          case 28: // A28 filament test
            {
              if(CodeSeen('O'));
              else if(CodeSeen('C'));
            }
            ANYCUBIC_SERIAL_ENTER();
            break;
          case 29: // A29 Z PROBE OFFESET SET
            break;

          case 30: // A30 assist leveling, the original function was canceled
            if(CodeSeen('S')) {
              ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Entering level menue...");
            } else if(CodeSeen('O')) {
              ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Leveling started and movint to front left...");
              enqueue_and_echo_commands_P(PSTR("G91\nG1 Z10 F240\nG90\nG28\nG29\nG1 X20 Y20 F6000\nG1 Z0 F240"));
            } else if(CodeSeen('T')) {
              ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Level checkpoint front right...");
              enqueue_and_echo_commands_P(PSTR("G1 Z5 F240\nG1 X190 Y20 F6000\nG1 Z0 F240"));
            } else if(CodeSeen('C')) {
              ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Level checkpoint back right...");
              enqueue_and_echo_commands_P(PSTR("G1 Z5 F240\nG1 X190 Y190 F6000\nG1 Z0 F240"));
            } else if(CodeSeen('Q')) {
              ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Level checkpoint back right...");
              enqueue_and_echo_commands_P(PSTR("G1 Z5 F240\nG1 X190 Y20 F6000\nG1 Z0 F240"));
            } else if(CodeSeen('H')) {
              ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Level check no heating...");
              //enqueue_and_echo_commands_P(PSTR("... TBD ..."));
              ANYCUBIC_SERIAL_PROTOCOLLNPGM("J22"); // J22 Test print done
              ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Serial Debug: Leveling print test done... J22");
            } else if(CodeSeen('L')) {
              ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Level check heating...");
              //enqueue_and_echo_commands_P(PSTR("... TBD ..."));
              ANYCUBIC_SERIAL_PROTOCOLLNPGM("J22"); // J22 Test print done
              ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Serial Debug: Leveling print test with heating done... J22");
            }
            ANYCUBIC_SERIAL_SUCC_START;
            ANYCUBIC_SERIAL_ENTER();

            break;
          case 31: // A31 zoffset
            if((!planner.movesplanned())&&(TFTstate!=ANYCUBIC_TFT_STATE_SDPAUSE) && (TFTstate!=ANYCUBIC_TFT_STATE_SDOUTAGE))
            {
              #if HAS_BED_PROBE
                char value[30];
                char *s_zoffset;
                //if((current_position[Z_AXIS]<10))
                //  z_offset_auto_test();

                if(CodeSeen('S')) {
                  ANYCUBIC_SERIAL_PROTOCOLPGM("A9V ");
                  ANYCUBIC_SERIAL_PROTOCOL(itostr3(int(zprobe_zoffset*100.00 + 0.5)));
                  ANYCUBIC_SERIAL_ENTER();
                  ANYCUBIC_TFT_DEBUG_ECHOPGM("TFT sending current z-probe offset data... <\r\nA9V ");
                  ANYCUBIC_TFT_DEBUG_ECHO(itostr3(int(zprobe_zoffset*100.00 + 0.5)));
                  ANYCUBIC_TFT_DEBUG_ECHOLNPGM(">");
                }
                if(CodeSeen('D'))
                {
                  s_zoffset=ftostr32(float(CodeValue())/100.0);
                  sprintf_P(value,PSTR("M851 Z"));
                  strcat(value,s_zoffset);
                  enqueue_and_echo_command(value); // Apply Z-Probe offset
                  enqueue_and_echo_commands_P(PSTR("M500")); // Save to EEPROM
                }
              #endif
            }
            ANYCUBIC_SERIAL_ENTER();
            break;
          case 32: // A32 clean leveling beep flag
            if(CodeSeen('S')) {
              ANYCUBIC_TFT_DEBUG_ECHOLNPGM("TFT Level saving data...");
              enqueue_and_echo_commands_P(PSTR("M500\nM420 S1\nG1 Z10 F240\nG1 X0 Y0 F6000"));
              ANYCUBIC_SERIAL_SUCC_START;
              ANYCUBIC_SERIAL_ENTER();
            }
            break;
          case 33: // A33 get version info
            {
              ANYCUBIC_SERIAL_PROTOCOLPGM("J33 ");
              ANYCUBIC_SERIAL_PROTOCOLLNPGM(MSG_MY_VERSION);
            }
            break;
          default: break;
        }
      }
      TFTbufindw = (TFTbufindw + 1)%TFTBUFSIZE;
      TFTbuflen += 1;
      serial3_count = 0; //clear buffer
    }
    else
    {
      TFTcmdbuffer[TFTbufindw][serial3_count++] = serial3_char;
    }
  }
}

void AnycubicTFTClass::CommandScan()
{
  CheckHeaterError();
  CheckSDCardChange();
  StateHandler();

  if(TFTbuflen<(TFTBUFSIZE-1))
  GetCommandFromTFT();
  if(TFTbuflen)
  {
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

  if(TFTstate==ANYCUBIC_TFT_STATE_SDPRINT)
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
