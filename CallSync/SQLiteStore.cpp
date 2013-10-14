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

#include "SQLiteStore.h"
#include "DStarTools.h"
#include <arpa/inet.h>
#include <string.h>
#include <syslog.h>
#include <stdio.h>

#define FIND_ROUTE              0
#define VERIFY_ADDRESS          1
#define RESET_USER_STATE        2
#define STORE_USER_SERVER       3
#define STORE_USER              4
#define REMOVE_USER             5
#define FIND_ACTIVE_BOT         6
#define GET_LAST_MODIFIED_DATE  7
#define GET_COUNT_BY_DATE       8
#define STORE_TABLE_DATA        9

SQLiteStore::SQLiteStore(const char* file) :
  connection(NULL),
  onReport(NULL)
{
  const char* queries[] =
  {
    // FIND_ROUTE
    "SELECT Repeater, Gateway, Address FROM Routes"
    "  WHERE Call = ?;",
    // VERIFY_ADDRESS
    "SELECT Call FROM Gateways"
    "  WHERE Address = ?"
    "  LIMIT 1;"
    // RESET_USER_STATE
    "UPDATE Users SET [Enabled] = 0;",
    // STORE_USER_SERVER
    "UPDATE Users SET [Server] = ?"
    "  WHERE [[Nick] = ?;"
    // STORE_USER
    "INSERT OR REPLACE INTO Users ([Nick], [Name], [Address])"
    "  VALUES (?, ?, ?);",
    // REMOVE_USER
    "UPDATE Users"
    "  SET [Enabled] = 0 WHERE [Nick] = ?;",
    // FIND_ACTIVE_BOT
    "    SELECT [Nick], [Date], 2 AS [Priority] FROM Users"
    "      WHERE ([Server] = ?) AND ([Nick] LIKE 's-%') AND ([Enabled] = 1)"
    "  UNION"
    "    SELECT [Nick], [Date], 2 AS [Priority] FROM Users"
    "      WHERE ([Nick] LIKE 's-%') AND ([Enabled] = 1)"
    "  ORDER BY [Priority] ASC, [Date] DESC"
    "  LIMIT 1;",
    // GET_LAST_MODIFIED_DATE
    "SELECT MAX([Date]) FROM Tables"
    "  WHERE [Table] = ?;",
    // GET_COUNT_BY_DATE
    "SELECT COUNT(*) FROM Tables"
    "  WHERE ([Table] = ?) AND ([Date] = ?);",
    // STORE_TABLE_DATA
    "INSERT OR REPLACE INTO Tables ([Table], [Date], [Call1], [Call2])"
    "  VALUES (?, ?, ?, ?);"
  };

  sqlite3_open_v2(file, &connection, SQLITE_OPEN_READWRITE, NULL);

  for (size_t index = 0; index < PREPARED_STATEMENT_COUNT; index ++)
  {
    const char* query = queries[index];
    sqlite3_prepare_v2(connection, query, strlen(query), &statements[index], NULL);
  }
}

SQLiteStore::~SQLiteStore()
{
  for (size_t index = 0; index < PREPARED_STATEMENT_COUNT; index ++)
    sqlite3_finalize(statements[index]);

  sqlite3_close(connection);
}

void SQLiteStore::report(int priority, const char* format, ...)
{
  if (onReport != NULL)
  {
    va_list arguments;
    va_start(arguments, format);
    onReport(priority, format, arguments);
    va_end(arguments);
  }
}

bool SQLiteStore::checkConnectionError()
{
  int code = sqlite3_errcode(connection);
  if ((code > SQLITE_OK) && (code < SQLITE_ROW))
  {
    const char* message = sqlite3_errmsg(connection);
    report(LOG_CRIT, "SQLite error: %s\n", message);
    return true;
  }
  return false;
}

bool SQLiteStore::findRoute(const char* call, struct DStarRoute* route, struct in_addr* address)
{
  bool outcome = false;
  char filter[LONG_CALLSIGN_LENGTH + 1];
  CopyDStarCall(call, filter, NULL, COPY_MAKE_STRING);

  sqlite3_stmt* statement = statements[FIND_ROUTE];
  if (sqlite3_bind_text(statement, 1, filter, LONG_CALLSIGN_LENGTH, SQLITE_STATIC) == SQLITE_OK)
  {
    if (sqlite3_step(statement) == SQLITE_ROW)
    {
      CopyDStarCall(call, route->yourCall, NULL, 0);
      CopyDStarCall((char*)sqlite3_column_text(statement, 0), route->repeater2, NULL, 0);
      CopyDStarCall((char*)sqlite3_column_text(statement, 1), route->repeater1, NULL, 0);
      inet_aton((char*)sqlite3_column_text(statement, 2), address);
      route->repeater1[LONG_CALLSIGN_LENGTH - 1] = 'G';
      outcome = true;
    }
    sqlite3_reset(statement);
  }
  checkConnectionError();

  return outcome;
}

bool SQLiteStore::verifyAddress(const struct in_addr& address)
{
  bool outcome = false;
  char* filter = inet_ntoa(address);

  sqlite3_stmt* statement = statements[VERIFY_ADDRESS];
  if (sqlite3_bind_text(statement, 1, filter, -1, SQLITE_STATIC) == SQLITE_OK)
  {
    outcome = (sqlite3_step(statement) == SQLITE_ROW);
    sqlite3_reset(statement);
  }
  checkConnectionError();

  return outcome;
}

void SQLiteStore::resetUserState()
{
  sqlite3_stmt* statement = statements[RESET_USER_STATE];
  sqlite3_step(statement);
  sqlite3_reset(statement);
  checkConnectionError();
}

void SQLiteStore::storeUserServer(const char* nick, const char* server)
{
  sqlite3_stmt* statement = statements[STORE_USER];
  if ((sqlite3_bind_text(statement, 1, server, -1, SQLITE_STATIC) == SQLITE_OK) &&
      (sqlite3_bind_text(statement, 2, nick, -1, SQLITE_STATIC) == SQLITE_OK))
  {
    sqlite3_step(statement);
    sqlite3_reset(statement);
  }
  checkConnectionError();
}

void SQLiteStore::storeUser(const char* nick, const char* name, const char* address)
{
  sqlite3_stmt* statement = statements[STORE_USER];
  if ((sqlite3_bind_text(statement, 1, nick, -1, SQLITE_STATIC) == SQLITE_OK) &&
      (sqlite3_bind_text(statement, 2, name, -1, SQLITE_STATIC) == SQLITE_OK) &&
      (sqlite3_bind_text(statement, 3, address, -1, SQLITE_STATIC) == SQLITE_OK))
  {
    sqlite3_step(statement);
    sqlite3_reset(statement);
  }
  checkConnectionError();
}

void SQLiteStore::removeUser(const char* nick)
{
  sqlite3_stmt* statement = statements[REMOVE_USER];
  if (sqlite3_bind_text(statement, 1, nick, -1, SQLITE_STATIC) == SQLITE_OK)
  {
    sqlite3_step(statement);
    sqlite3_reset(statement);
  }
  checkConnectionError();
}

char* SQLiteStore::findActiveBot(const char* server)
{
  char* bot = NULL;
  sqlite3_stmt* statement = statements[FIND_ACTIVE_BOT];
  if ((sqlite3_bind_text(statement, 1, server, -1, SQLITE_STATIC) == SQLITE_OK) &&
      (sqlite3_step(statement) == SQLITE_ROW))
    bot = strdup((char*)sqlite3_column_text(statement, 0));
  sqlite3_reset(statement);
  checkConnectionError();
  return bot;
}

void SQLiteStore::getLastModifiedDate(int table, char* date)
{
  sqlite3_stmt* statement = statements[GET_LAST_MODIFIED_DATE];
  if (sqlite3_bind_int(statement, 1, table) == SQLITE_OK)
  {
    char* column = NULL;
    if ((sqlite3_step(statement) == SQLITE_ROW) &&
       (column = (char*)sqlite3_column_text(statement, 0)))
      strncpy(date, column, DDB_DATE_LENGTH);
    sqlite3_reset(statement);
  }
  checkConnectionError();
}

size_t SQLiteStore::getCountByDate(int table, const char* date)
{
  size_t count = 0;
  sqlite3_stmt* statement = statements[GET_COUNT_BY_DATE];
  if ((sqlite3_bind_int(statement, 1, table) == SQLITE_OK) &&
      (sqlite3_bind_text(statement, 2, date, -1, SQLITE_STATIC) == SQLITE_OK))
  {
    char* column = NULL;
    if (sqlite3_step(statement) == SQLITE_ROW)
       count = sqlite3_column_int(statement, 0);
    sqlite3_reset(statement);
  }
  checkConnectionError();
  return count;
}

void SQLiteStore::storeTableData(int table, const char* date, const char* call1, const char* call2)
{
  sqlite3_stmt* statement = statements[STORE_TABLE_DATA];
  if ((sqlite3_bind_int(statement, 1, table) == SQLITE_OK) &&
      (sqlite3_bind_text(statement, 2, date, -1, SQLITE_STATIC) == SQLITE_OK) &&
      (sqlite3_bind_text(statement, 3, call1, -1, SQLITE_STATIC) == SQLITE_OK) &&
      (sqlite3_bind_text(statement, 4, call2, -1, SQLITE_STATIC) == SQLITE_OK))
  {
    sqlite3_step(statement);
    sqlite3_reset(statement);
  }
  checkConnectionError();
}
