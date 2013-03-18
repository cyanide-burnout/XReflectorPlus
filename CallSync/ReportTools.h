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

#ifndef REPORTTOOLS_H
#define REPORTTOOLS_H

#include <syslog.h>
#include <stdarg.h>

#define LOGGER_OUTPUT_CONSOLE  (1 << 0)
#define LOGGER_OUTPUT_SYSLOG   (1 << 1)

#ifdef __cplusplus
extern "C"
{
#endif

void setReportOutput(int option);
void doReport(int priority, const char* format, va_list arguments);
void report(int priority, const char* format, ...);
void print(const char* format, ...);

#ifdef __cplusplus
}
#endif

#endif
