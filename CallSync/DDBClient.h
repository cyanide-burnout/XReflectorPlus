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

#ifndef DDBCLIENT_H
#define DDBCLIENT_H

#include <time.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include <netinet/in.h>
#include <libircclient/libircclient.h>

#include "DStar.h"

#define DDB_DEFAULT_PORT    9007

#define DDB_TABLE_0         0
#define DDB_TABLE_1         1
#define DDB_TABLE_2         2
#define DDB_TABLE_COUNT     3

#define DDB_DATE_LENGTH     19

#define DDB_TOUCH_INTERVAL  300

class DDBClient
{
  public:

    typedef void (*ReportHandler)(int priority, const char* format, va_list arguments);
    typedef void (*UpdateCommandHandler)(DDBClient* client, int table, const char* date, const char* call1, const char* call2);

    class Store
    {
      public:

        virtual bool findRoute(const char* call, struct DStarRoute* route, struct in_addr* address) = 0;

        virtual void resetUserState() = 0;
        virtual void storeUser(const char* nick, const char* name, const char* address) = 0;
        virtual void removeUser(const char* nick) = 0;

        virtual char* findActiveBot() = 0;

        virtual void getLastModifiedDate(int table, char* date) = 0;
        virtual size_t getCountByDate(int table, const char* date) = 0;

        virtual void storeTableData(int table, const char* date, const char* call1, const char* call2) = 0;
    };

    DDBClient(Store* store,
      const char* server, int port,
      const char* name, const char* password,
      const char* version);
    ~DDBClient();

    void setUpdateCommandHandler(UpdateCommandHandler handler, void* data);

    void connect();
    void disconnect();

    void publishHeard(const struct DStarRoute& route, const char* addressee, const char* text);
    void publishHeard(const struct DStarRoute& route, uint16_t number);
    void touch(time_t now);

    inline Store* getStore()            { return store; };
    inline irc_session_t* getSession()  { return session; };

    inline bool isConnected()           { return irc_is_connected(session); };
    inline bool isReadOnly()            { return strchr(name, '-'); };

    ReportHandler onReport;
    UpdateCommandHandler onUpdateCommand;

    void* userData;

  private:

    Store* store;

    char* server;
    char* name;
    char* password;
    char* version;
    int attempt;
    int number;
    int port;

    pthread_mutex_t lock;
    irc_session_t* session;
    irc_callbacks_t handlers;

    char* volatile bot;
    int state;

    void report(int priority, const char* format, ...);
    time_t parseData(const char* value);

    char* generateNick();
    void sendCommand(const char* command);

    void storeUser(const char* nick, const char* name, const char* address);
    void removeUser(const char* nick);

    void requestForUpdate();
    void preventDataLoop(int table, char* date);
    void storeTableData(int table, const char* date, const char* call1, const char* call2);

    static void handleConnect(irc_session_t* session, const char* event, const char* origin, const char** parameters, unsigned int count);
    static void handleJoin(irc_session_t* session, const char* event, const char* origin, const char** parameters, unsigned int count);
    static void handlePrivateMessage(irc_session_t* session, const char* event, const char* origin, const char** parameters, unsigned int count);
    static void handleEventCode(irc_session_t* session, unsigned int event, const char* origin, const char** parameters, unsigned int count);
};

#endif
