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
#include <string.h>
#include <syslog.h>
#include <stdio.h>

MySQLStore::MySQLStore(const char* file) :
  connection(NULL),
  onReport(NULL)
{
  connection = mysql_init(NULL);
  
  mysql_options(connection, MYSQL_READ_DEFAULT_FILE, realpath(file, NULL));
  mysql_real_connect(connection, NULL, NULL, NULL, NULL, 0, NULL, CLIENT_REMEMBER_OPTIONS);
  
  my_bool active = true;
  mysql_options(connection, MYSQL_OPT_RECONNECT, &active);
}

MySQLStore::~MySQLStore()
{
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

bool MySQLStore::checkConnectionError(int status)
{
  if (status != 0)
  {
    const char* error = mysql_error(connection);
    report(LOG_CRIT, "MySQL error: %s\n", error);
    return true;
  }
  return false;
}

bool MySQLStore::findRoute(const char* call, struct DStarRoute* route, struct in_addr* address)
{
  bool outcome = false;
  char filter[LONG_CALLSIGN_LENGTH + 1];
  CopyDStarCall(call, filter, NULL, COPY_MAKE_STRING);

  char* query = NULL;
  asprintf(&query,
    "SELECT Repeater, Gateway, Address FROM Routes"
    "  WHERE Call = '%s'", filter);

  int status = mysql_query(connection, query);
  checkConnectionError(status);

  MYSQL_RES* result = mysql_store_result(connection);
  if (result != NULL)
  {
    MYSQL_ROW row = mysql_fetch_row(result);
    if (row != NULL)
    {
      CopyDStarCall(call, route->yourCall, NULL, 0);
      CopyDStarCall(row[0], route->repeater2, NULL, 0);
      CopyDStarCall(row[1], route->repeater1, NULL, 0);
      inet_aton(row[2], address);
      route->repeater1[LONG_CALLSIGN_LENGTH - 1] = 'G';
      outcome = true;
    }
    mysql_free_result(result);
  }
  checkConnectionError();

  free(query);
  return outcome;
}

bool MySQLStore::verifyAddress(const struct in_addr& address)
{
  bool outcome = false;
  char* filter = inet_ntoa(address);
  char* query = NULL;
  asprintf(&query,
    "SELECT Call FROM Gateways"
    "  WHERE Address = '%s'", filter);

  int status = mysql_query(connection, query);
  checkConnectionError(status);

  MYSQL_RES* result = mysql_store_result(connection);
  if (result != NULL)
  {
    MYSQL_ROW row = mysql_fetch_row(result);
    outcome = (row != NULL);
    mysql_free_result(result);
  }
  checkConnectionError();

  free(query);
  return outcome;
}

void MySQLStore::resetUserState()
{
  const char* query = "UPDATE Users SET `Enabled` = 0";
  int status = mysql_query(connection, query);
  checkConnectionError(status);
}

void MySQLStore::storeUser(const char* nick, const char* name, const char* address)
{
  char* query = NULL;
  asprintf(&query,
    "REPLACE INTO Users (`Nick`, `Name`, `Address`)"
    "  VALUES ('%s', '%s', '%s')", nick, name, address);
  int status = mysql_query(connection, query);
  checkConnectionError(status);
  free(query);
}

void MySQLStore::removeUser(const char* nick)
{
  char* query = NULL;
  asprintf(&query,
    "UPDATE Users"
    "  SET `Enabled` = 0 WHERE `Nick` = '%s'", nick);
  int status = mysql_query(connection, query);
  checkConnectionError(status);
  free(query);
}

char* MySQLStore::findActiveBot()
{
  char* bot = NULL;
  const char* query =
    "SELECT `Nick` FROM Users"
    "  WHERE (`Nick` LIKE 's-%') AND (`Enabled` = 1)"
    "  ORDER BY `Date` DESC LIMIT 1";

  int status = mysql_query(connection, query);
  checkConnectionError(status);

  MYSQL_RES* result = mysql_store_result(connection);
  if (result != NULL)
  {
    MYSQL_ROW row = mysql_fetch_row(result);
    if ((row != NULL) && (row[0] != NULL))
      bot = strdup(row[0]);
    mysql_free_result(result);
  }
  checkConnectionError();
  return bot;
}

void MySQLStore::getLastModifiedDate(int table, char* date)
{
  char* query = NULL;
  asprintf(&query,
    "SELECT MAX(`Date`) FROM Tables"
    "  WHERE `Table` = %d", table);

  int status = mysql_query(connection, query);
  checkConnectionError(status);

  MYSQL_RES* result = mysql_store_result(connection);
  if (result != NULL)
  {
    char* column = NULL;
    MYSQL_ROW row = mysql_fetch_row(result);
    if ((row != NULL) && (row[0] != NULL))
      strncpy(date, row[0], DDB_DATE_LENGTH);
    mysql_free_result(result);
  }
  checkConnectionError();

  free(query);
}

size_t MySQLStore::getCountByDate(int table, const char* date)
{
  size_t count = 0;
  char* query = NULL;
  asprintf(&query,
    "SELECT COUNT(*) FROM Tables"
    "  WHERE (`Table` = %d) AND (`Date` = '%s')", table, date);

  int status = mysql_query(connection, query);
  checkConnectionError(status);

  MYSQL_RES* result = mysql_store_result(connection);
  if (result != NULL)
  {
    char* column = NULL;
    MYSQL_ROW row = mysql_fetch_row(result);
    if (row != NULL)
       count = atoi(row[0]);
    mysql_free_result(result);
  }
  checkConnectionError();

  free(query);
  return count;
}

void MySQLStore::storeTableData(int table, const char* date, const char* call1, const char* call2)
{
  char* query = NULL;
  asprintf(&query,
    "REPLACE INTO Tables (`Table`, `Date`, `Call1`, `Call2`)"
    "  VALUES (%d, '%s', '%s', '%s')", table, date, call1, call2);
  int status = mysql_query(connection, query);
  checkConnectionError(status);
  free(query);
}
