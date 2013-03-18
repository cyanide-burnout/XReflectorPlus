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

#ifndef STRINGTOOLS_H
#define STRINGTOOLS_H

#include <unistd.h>

#ifdef __cplusplus
extern "C"
{
#endif

char* extract(char** pointer, size_t length);
char* replace(char* string, char search, char replace, ssize_t length);
char* lower(char* string, ssize_t length);
char* upper(char* string, ssize_t length);
void clean(char* string, char replace, size_t length);
void release(char** array);

#ifdef __cplusplus
};
#endif

#endif
