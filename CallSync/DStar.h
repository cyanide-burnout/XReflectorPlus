/*

Copyright (C) 2013   Artem Prilutskiy, R3ABM (r3abm@dstar.su)

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#ifndef DSTAR_H
#define DSTAR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#pragma pack(push, 1)

#define CQCQCQ                     "CQCQCQ  "
#define VISIBILITY_ON              "VIS   ON"
#define VISIBILITY_OFF             "VIS  OFF"

#define LONG_CALLSIGN_LENGTH       8
#define SHORT_CALLSIGN_LENGTH      4

#define MODULE_NAME_POSITION       (LONG_CALLSIGN_LENGTH - 1)

#define RADIO_HEADER_LENGTH        41

#define SLOW_DATA_SYNC_VECTOR      0
#define SLOW_DATA_FRAME_COUNT      20
#define SLOW_DATA_BLOCK_COUNT      (SLOW_DATA_FRAME_COUNT / 2)

#define SLOW_DATA_FRAME_SIZE       3
#define SLOW_DATA_BLOCK_SIZE       (SLOW_DATA_FRAME_SIZE * 2)
#define SLOW_DATA_DATA_SIZE        (SLOW_DATA_BLOCK_SIZE - 1)
#define SLOW_DATA_BUFFER_SIZE      256

#define SLOW_DATA_TYPE_MASK        0xf0
#define SLOW_DATA_LENGTH_MASK      0x0f
#define SLOW_DATA_FILLER_BYTE      0x66

#define SLOW_DATA_TYPE_GPS         0x30
#define SLOW_DATA_TYPE_TEXT        0x40
#define SLOW_DATA_TYPE_HEADER      0x50
#define SLOW_DATA_TYPE_SQUELCH     0xC0

#define SLOW_DATA_SQUELCH_LENGTH   2
#define SLOW_DATA_TEXT_LENGTH      20

#define SLOW_DATA_TEXT_PART_COUNT  (SLOW_DATA_TEXT_LENGTH / SLOW_DATA_DATA_SIZE)
#define SLOW_DATA_TEXT_PART_MASK   (SLOW_DATA_TEXT_PART_COUNT - 1)

#define SCRAMBLER_BYTE1            0x70
#define SCRAMBLER_BYTE2            0x4f
#define SCRAMBLER_BYTE3            0x93

#define SYNC_VECTOR_BYTE1          0x55
#define SYNC_VECTOR_BYTE2          0x2d
#define SYNC_VECTOR_BYTE3          0x93

#define CODEC_DATA_FRAME_SIZE      9
#define AUDIO_DATA_FRAME_SIZE      CODEC_DATA_FRAME_SIZE

#define SEQUENCE_FLAG_CLOSE        0x40

struct DStarRoute
{
  uint8_t flags[3];
  char repeater2[LONG_CALLSIGN_LENGTH];
  char repeater1[LONG_CALLSIGN_LENGTH];
  char yourCall[LONG_CALLSIGN_LENGTH];
  char ownCall1[LONG_CALLSIGN_LENGTH];
  char ownCall2[SHORT_CALLSIGN_LENGTH];
};

struct DStarRadioHeader
{
  struct DStarRoute route;
  uint16_t checkSum;
};

struct DStarDVFrame
{
  uint8_t codecData[CODEC_DATA_FRAME_SIZE];
  uint8_t slowData[SLOW_DATA_FRAME_SIZE];
};

#pragma pack(pop)

#ifdef __cplusplus
}
#endif

#endif
