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

#include "ReportTools.h"

#include <unistd.h>
#include <syslog.h>
#include <stdarg.h>
#include <stdio.h>

#define DEFAULT_COLOR "\x1b[m"
#define RED_COLOR     "\x1b[01;31m"
#define GREEN_COLOR   "\x1b[01;32m"
#define YELLOW_COLOR  "\x1b[01;33m"
#define BLUE_COLOR    "\x1b[01;34m"
#define MAGENTA_COLOR "\x1b[01;35m"
#define CYAN_COLOR    "\x1b[01;36m"
#define WHITE_COLOR   "\x1b[01;37m"

int reportOption = LOGGER_OUTPUT_CONSOLE;

void doReport(int priority, const char* format, va_list arguments)
{
  if (reportOption & LOGGER_OUTPUT_CONSOLE)
  {
    priority &= 7;
    const char* priorities[] =
    {
      RED_COLOR,      // Emergency
      RED_COLOR,      // Alert
      RED_COLOR,      // Critical
      YELLOW_COLOR,   // Error
      MAGENTA_COLOR,  // Warning
      WHITE_COLOR,    // Notice
      GREEN_COLOR,    // Informational
      CYAN_COLOR      // Debug
    };
    printf(priorities[priority]);
    vprintf(format, arguments);
    printf(DEFAULT_COLOR);
  }

  if (reportOption & LOGGER_OUTPUT_SYSLOG)
    vsyslog(priority, format, arguments);
}

void setReportOutput(int option)
{
  reportOption = option;
}

void report(int priority, const char* format, ...)
{
  va_list arguments;
  va_start(arguments, format);
  doReport(priority, format, arguments);
  va_end(arguments);
}

void print(const char* format, ...)
{
  va_list arguments;
  va_start(arguments, format);
  doReport(LOG_INFO, format, arguments);
  va_end(arguments);
}
