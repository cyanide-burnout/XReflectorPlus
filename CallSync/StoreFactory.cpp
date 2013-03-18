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

#include "StoreFactory.h"
#ifdef USE_SQLITE3
#include "SQLiteStore.h"
#endif
#include "MySQLStore.h"
#ifdef USE_HANDLERSOCKET
#include "HSStore.h"
#endif

#include <string.h>

#define CREATE_STORE(name, class)                     \
  if (strcmp(type, name) == 0)                        \
  {                                                   \
    class* store = new class(file);                   \
    store->onReport = (class::ReportHandler)handler;  \
    if (store->checkConnectionError())                \
    {                                                 \
      delete store;                                   \
      return NULL;                                    \
    }                                                 \
    return store;                                     \
  }


DDBClient::Store* StoreFactory::createStore(const char* type, const char* file, DDBClient::ReportHandler handler)
{
#ifdef USE_SQLITE3
  CREATE_STORE("SQLite3", SQLiteStore);
#endif
  CREATE_STORE("MySQL", MySQLStore);
#ifdef USE_HANDLERSOCKET
  CREATE_STORE("HandlerSocket", HSStore);
#endif
  return NULL;
}
