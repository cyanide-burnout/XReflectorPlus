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

#ifndef MYSQLSTORE_H
#define MYSQLSTORE_H

#include <stdarg.h>
#include <mysql/mysql.h>
#include <netinet/in.h>

#include "DDBClient.h"

class MySQLStore : public DDBClient::Store
{
  public:

    typedef void (*ReportHandler)(int priority, const char* format, va_list arguments);

    MySQLStore(const char* file);
    ~MySQLStore();

    bool checkConnectionError();
    bool checkConnectionError(int status);

    bool findRoute(const char* call, struct DStarRoute* route, struct in_addr* address);
    bool verifyAddress(const struct in_addr& address);

    void resetUserState();
    void storeUser(const char* nick, const char* name, const char* address);
    void removeUser(const char* nick);

    char* findActiveBot();

    void getLastModifiedDate(int table, char* date);
    size_t getCountByDate(int table, const char* date);

    void storeTableData(int table, const char* date, const char* call1, const char* call2);

    ReportHandler onReport;

  private:

    MYSQL* connection;

    void report(int priority, const char* format, ...);
};

#endif
