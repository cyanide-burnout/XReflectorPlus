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

#include "MySQLStore.h"
#include "DStarTools.h"
#include <arpa/inet.h>
#include <stdarg.h>
#include <string.h>
#include <syslog.h>
#include <stdio.h>

#define HOST_ADDRESS_LENGTH     16
#define MAXIMUM_NICK_LENGTH     32

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

MySQLStore::MySQLStore(const char* file) :
  connection(NULL),
  onReport(NULL)
{
  const char* queries[] =
  {
    // FIND_ROUTE
    "SELECT Repeater, Gateway, Address FROM Routes"
    "  WHERE Call = ?",
    // VERIFY_ADDRESS
    "SELECT COUNT(Call) FROM Gateways"
    "  WHERE Address = ?",
    // RESET_USER_STATE
    "UPDATE Users SET `Enabled` = 0",
    // STORE_USER_SERVER
    "UPDATE Users SET `Server` = ?"
    "  WHERE (`Nick` = ?)",
    // STORE_USER
    "REPLACE INTO Users (`Nick`, `Name`, `Address`)"
    "  VALUES (?, ?, ?)",
    // REMOVE_USER
    "UPDATE Users"
    "  SET `Enabled` = 0 WHERE `Nick` = ?",
    // FIND_ACTIVE_BOT
    "    SELECT `Nick`, `Date`, 1 AS `Priority` FROM Users"
    "      WHERE (`Server` = ?) AND (`Nick` LIKE 's-%') AND (`Enabled` = 1)"
    "  UNION"
    "    SELECT `Nick`, `Date`, 2 AS `Priority` FROM Users"
    "      WHERE (`Nick` LIKE 's-%') AND (`Enabled` = 1)"
    "  ORDER BY `Priority` ASC, `Date` DESC"
    "  LIMIT 1",
    // GET_LAST_MODIFIED_DATE
    "SELECT MAX(`Date`) FROM Tables"
    "  WHERE `Table` = ?",
    // GET_COUNT_BY_DATE
    "SELECT COUNT(*) FROM Tables"
    "  WHERE (`Table` = ?) AND (`Date` = ?)",
    // STORE_TABLE_DATA
    "REPLACE INTO Tables (`Table`, `Date`, `Call1`, `Call2`)"
    "  VALUES (?, ?, ?, ?)"
  };

  connection = mysql_init(NULL);

  mysql_options(connection, MYSQL_READ_DEFAULT_FILE, realpath(file, NULL));
  mysql_real_connect(connection, NULL, NULL, NULL, NULL, 0, NULL, CLIENT_REMEMBER_OPTIONS);

  my_bool active = true;
  mysql_options(connection, MYSQL_OPT_RECONNECT, &active);

  for (size_t index = 0; index < PREPARED_STATEMENT_COUNT; index ++)
  {
    const char* query = queries[index];
    statements[index] = mysql_stmt_init(connection);
    mysql_stmt_prepare(statements[index], query, strlen(query));
  }
}

MySQLStore::~MySQLStore()
{
  for (size_t index = 0; index < PREPARED_STATEMENT_COUNT; index ++)
    mysql_stmt_close(statements[index]);

  mysql_close(connection);
}

void MySQLStore::report(int priority, const char* format, ...)
{
  if (onReport != NULL)
  {
    va_list arguments;
    va_start(arguments, format);
    onReport(priority, format, arguments);
    va_end(arguments);
  }
}

bool MySQLStore::checkConnectionError()
{
  const char* error = mysql_error(connection);
  if ((error != NULL) && (*error != '\0'))
  {
    report(LOG_CRIT, "MySQL error: %s\n", error);
    return true;
  }
  return false;
}

MYSQL_BIND* MySQLStore::bind(int count, va_list* arguments)
{
  MYSQL_BIND* bindings = NULL;
  if (count > 0)
  {
    bindings = (MYSQL_BIND*)calloc(count, sizeof(MYSQL_BIND));
    for (int index = 0; index < count; index ++)
    {
      int type = va_arg(*arguments, int);
      bindings[index].buffer_type = (enum_field_types)type;
      bindings[index].buffer = va_arg(*arguments, void*);
      if ((type == MYSQL_TYPE_STRING) ||
          (type == MYSQL_TYPE_DATETIME))
      {
        bindings[index].buffer_length = va_arg(*arguments, int);
        bindings[index].length = &bindings[index].buffer_length;
      }
    }
  }
  return bindings;
}

bool MySQLStore::execute(int index, int count1, int count2, ...)
{
  bool result = true;
  MYSQL_STMT* statement = statements[index];

  va_list arguments;
  va_start(arguments, count2);
  MYSQL_BIND* parameters = bind(count1, &arguments);
  MYSQL_BIND* columns = bind(count2, &arguments);
  va_end(arguments);

  if ((parameters && mysql_stmt_bind_param(statement, parameters)) ||
      (columns    && mysql_stmt_bind_result(statement, columns))   ||
                     mysql_stmt_execute(statement)                 ||
      (columns    && mysql_stmt_store_result(statement)))
  {
    const char* error = mysql_stmt_error(statement);
    report(LOG_CRIT, "MySQL error: %s\n", error);
    result = false;
  }

  if (result && columns)
  {
    result = !mysql_stmt_fetch(statement);
    mysql_stmt_free_result(statement);
  }

  free(columns);
  free(parameters);
  return result;
}

bool MySQLStore::findRoute(const char* call, struct DStarRoute* route, struct in_addr* address)
{
  bool outcome = false;

  char filter[LONG_CALLSIGN_LENGTH + 1];
  CopyDStarCall(call, filter, NULL, COPY_MAKE_STRING);

  char host[HOST_ADDRESS_LENGTH];
  memset(host, 0, sizeof(host));

  if (execute(FIND_ROUTE, 1, 3,
        MYSQL_TYPE_STRING, filter, LONG_CALLSIGN_LENGTH,
        MYSQL_TYPE_STRING, route->repeater2, LONG_CALLSIGN_LENGTH,
        MYSQL_TYPE_STRING, route->repeater1, LONG_CALLSIGN_LENGTH,
        MYSQL_TYPE_STRING, host, HOST_ADDRESS_LENGTH))
  {
    inet_aton(host, address);
    CopyDStarCall(call, route->yourCall, NULL, 0);
    route->repeater1[LONG_CALLSIGN_LENGTH - 1] = 'G';
    outcome = true;
  }

  return outcome;
}

bool MySQLStore::verifyAddress(const struct in_addr& address)
{
  int outcome = 0;
  char* filter = inet_ntoa(address);

  execute(VERIFY_ADDRESS, 1, 1,
    MYSQL_TYPE_STRING, filter, strlen(filter),
    MYSQL_TYPE_LONG, &outcome);

  return outcome;
}

void MySQLStore::resetUserState()
{
  execute(RESET_USER_STATE, 0, 0);
}

void MySQLStore::storeUserServer(const char* nick, const char* server)
{
  execute(STORE_USER_SERVER, 2, 0,
    MYSQL_TYPE_STRING, server, strlen(server),
    MYSQL_TYPE_STRING, nick, strlen(nick));
}

void MySQLStore::storeUser(const char* nick, const char* name, const char* address)
{
  execute(STORE_USER, 3, 0,
    MYSQL_TYPE_STRING, nick, strlen(nick),
    MYSQL_TYPE_STRING, name, strlen(name),
    MYSQL_TYPE_STRING, address, strlen(address));
}

void MySQLStore::removeUser(const char* nick)
{
  execute(REMOVE_USER, 1, 0,
    MYSQL_TYPE_STRING, nick, strlen(nick));
}

char* MySQLStore::findActiveBot(const char* server)
{
  char* bot = (char*)calloc(MAXIMUM_NICK_LENGTH, 1);

  if (!execute(FIND_ACTIVE_BOT, 1, 1,
        MYSQL_TYPE_STRING, server, strlen(server),
        MYSQL_TYPE_STRING, bot, MAXIMUM_NICK_LENGTH))
  {
    free(bot);
    bot = NULL;
  }

  return bot;
}

void MySQLStore::getLastModifiedDate(int table, char* date)
{
  MYSQL_TIME time;

  if (execute(GET_LAST_MODIFIED_DATE, 1, 1,
        MYSQL_TYPE_LONG, &table,
        MYSQL_TYPE_DATETIME, &time, sizeof(time)))
    sprintf(date, "%04d-%02d-%02d %02d:%02d:%02d",
      time.year, time.month, time.day, 
      time.hour, time.minute, time.second);
}

size_t MySQLStore::getCountByDate(int table, const char* date)
{
  int count = 0;

  execute(GET_COUNT_BY_DATE, 2, 1,
    MYSQL_TYPE_LONG, &table,
    MYSQL_TYPE_STRING, date, strlen(date),
    MYSQL_TYPE_LONG, &count);

  return count;
}

void MySQLStore::storeTableData(int table, const char* date, const char* call1, const char* call2)
{
  MYSQL_TIME time;
  memset(&time, 0, sizeof(time));
  sscanf(date, "%04d-%02d-%02d %02d:%02d:%02d",
    &time.year, &time.month, &time.day, 
    &time.hour, &time.minute, &time.second);

  execute(STORE_TABLE_DATA, 4, 0,
    MYSQL_TYPE_LONG, &table,
    MYSQL_TYPE_DATETIME, &time, sizeof(time),
    MYSQL_TYPE_STRING, call1, LONG_CALLSIGN_LENGTH,
    MYSQL_TYPE_STRING, call2, LONG_CALLSIGN_LENGTH);
}
