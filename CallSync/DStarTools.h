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

#ifndef DSTARTOOLS_H
#define DSTARTOOLS_H

#include <stddef.h>
#include <stdint.h>

#define COPY_MAKE_STRING       (1 << 0)
#define COPY_CLEAR_MODULE      (1 << 1)

#ifdef __cplusplus
extern "C"
{
#endif

char GetDStarModule(const char* call);
void CopyDStarCall(const char* source, char* destination, char* module, int options);
char CompareDStarCall(const char* call1, const char* call2);
uint16_t CalculateCCITTCheckSum(const void* data, size_t length);

#ifdef __cplusplus
};
#endif

#endif
