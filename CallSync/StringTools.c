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

#include "StringTools.h"
#include <ctype.h>
#include <stdlib.h>

char* extract(char** pointer, size_t length)
{
  char* string = *pointer;
  (*pointer)[length] = '\0';
  (*pointer) += length + 1;
  return string;
}

char* replace(char* string, char search, char replace, ssize_t length)
{
  char* pointer = string;
  while ((*pointer != '\0') && (length != 0))
  {
    if (*pointer == search)
      *pointer = replace;
    pointer ++;
    length --;
  }
  return string;
}

char* lower(char* string, ssize_t length)
{
  char* pointer = string;
  while ((*pointer != '\0') && (length != 0))
  {
    *pointer = tolower(*pointer);
    pointer ++;
    length --;
  }
  return string;
}

char* upper(char* string, ssize_t length)
{
  char* pointer = string;
  while ((*pointer != '\0') && (length != 0))
  {
    *pointer = toupper(*pointer);
    pointer ++;
    length --;
  }
  return string;
}

void clean(char* string, char replace, size_t length)
{
  while (length > 0)
  {
    if (*string <= ' ')
      *string = replace;
    string ++;
    length --;
  }
}

void release(char** array)
{
  char** string = array;
  if (array != NULL)
  {
    while (*string != NULL)
    {
      free(*string);
      string ++;
    }
    free(array);
  }
}
