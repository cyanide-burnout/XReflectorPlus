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

#ifndef DB_H
#define DB_H

#include <stddef.h>
#include <stdint.h>
#include <handlersocket/hstcpcli.hpp>

#define DB_BTREE   0
#define DB_RDONLY  0

#ifdef __cplusplus
extern "C"
{
#endif

#pragma mark DB API Wrapper

typedef struct dbt DBT;
typedef struct db DB;

struct dbt
{
  char* data;
  size_t size;
};

struct db
{
  dena::hstcpcli_i* client;
  int (*open)(DB* context, const char* unknown1, const char* file, const char* unknown2, int type, int flags, int unknown3);
  int (*close)(DB* context, int flags);
  int (*get)(DB* context, const char* unknown, DBT* key, DBT* data, int flags);
};

int db_create(DB** context, const char* unknown1, int unknown2);
const char* db_strerror(int code);

#pragma mark Specific API

int plus_get_nat_type(DB* context, const char* call);
int plus_validate_connection(DB* context, const char* call, const char* address);

#ifdef __cplusplus
};
#endif

#endif
