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

#include "DDBClient.h"
#include "DStarTools.h"
#include "StringTools.h"
#include <syslog.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#define DDB_IRC_CHANNEL      "#dstar"

#define DDB_DATA_LENGTH      (DDB_DATE_LENGTH + 2 * LONG_CALLSIGN_LENGTH + 2)
#define DDB_UPDATE_LENGTH    (DDB_PUSH_LENGTH + 9)

#define DDB_SYNC_MARK        0x80

#define DDB_MAXIMUM_ENTRIES  30

DDBClient::DDBClient(Store* store,
    const char* server, int port,
    const char* name, const char* password,
    const char* version) :
  onUpdateCommand(NULL),
  onReport(NULL),
  store(store),
  port(port),
  attempt(0),
  number(0),
  bot(NULL),
  state(0)
{
  this->server = strdup(server);
  this->name = strdup(name);
  this->password = strdup(password);
  this->version = strdup(version);

  pthread_mutex_init(&lock, NULL);
  memset(&handlers, 0, sizeof(handlers));

  handlers.event_connect = handleConnect;
  handlers.event_join = handleJoin;
  handlers.event_quit = handleJoin;
  handlers.event_privmsg = handlePrivateMessage;
  handlers.event_channel = handlePrivateMessage;
  handlers.event_numeric = handleEventCode;

  session = irc_create_session(&handlers);
  irc_set_ctx(session, this);
}

DDBClient::~DDBClient()
{
  disconnect();
  irc_destroy_session(session);
  pthread_mutex_destroy(&lock);
  free(server);
  free(name);
  free(password);
  free(version);
  free(bot);
}

void DDBClient::report(int priority, const char* format, ...)
{
  if (onReport != NULL)
  {
    va_list arguments;
    va_start(arguments, format);
    onReport(priority, format, arguments);
    va_end(arguments);
  }
}

char* DDBClient::generateNick()
{
  attempt ++;
  char* nick = NULL;
  asprintf(&nick, "%s-%d", name, attempt);
  attempt &= 3;
  return nick;
}

void DDBClient::connect()
{
  char* delimeter = strchr(name, '-');
  if (delimeter == NULL)
  {
    char* nick = generateNick();
    irc_connect(session, server, port, password, nick, name, version);
    report(LOG_INFO, "%s: connecting as %s\n", server, nick);
    free(nick);
  }
  else
  {
    char* user = delimeter + 1;
    irc_connect(session, server, port, password, name, user, version);
    report(LOG_INFO, "%s: connecting as %s\n", server, name);
  }
}

void DDBClient::disconnect()
{
  irc_disconnect(session);
  store->resetUserState();
}

void DDBClient::sendCommand(const char* command)
{
  pthread_mutex_lock(&lock);
  if (bot != NULL)
    irc_cmd_msg(session, bot, command);
  pthread_mutex_unlock(&lock);
}

void DDBClient::publishHeard(const struct DStarRoute& route, const char* addressee, const char* text)
{
  struct DStarRoute buffer;
  memcpy(&buffer, &route, sizeof(DStarRoute));
  replace(buffer.yourCall, ' ', '_', LONG_CALLSIGN_LENGTH);
  replace(buffer.ownCall1, ' ', '_', LONG_CALLSIGN_LENGTH);
  replace(buffer.ownCall2, ' ', '_', SHORT_CALLSIGN_LENGTH);
  replace(buffer.repeater1, ' ', '_', LONG_CALLSIGN_LENGTH);
  replace(buffer.repeater2, ' ', '_', LONG_CALLSIGN_LENGTH);

  char call[LONG_CALLSIGN_LENGTH];
  CopyDStarCall(addressee, call, NULL, 0);
  replace(call, ' ', '_', LONG_CALLSIGN_LENGTH);

  char date[DDB_DATE_LENGTH + 1];
  time_t now = ::time(NULL);
  struct tm* time = gmtime(&now);
  strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", time);

  char topic[SLOW_DATA_TEXT_LENGTH];
  strncpy(topic, text, SLOW_DATA_TEXT_LENGTH);
  clean(topic, '_', SLOW_DATA_TEXT_LENGTH);

  char* command = NULL;
  asprintf(&command,
    "UPDATE %s %.8s %.8s 0 %.8s %.8s %02X %02X %02X %.4s 00 %.8s %.20s",
    date, buffer.ownCall1, buffer.repeater1, buffer.repeater2, buffer.yourCall,
    buffer.flags[0], buffer.flags[1], buffer.flags[2], buffer.ownCall2, call, topic);
  sendCommand(command);
  free(command);
}

void DDBClient::publishHeard(const struct DStarRoute& route, uint16_t number)
{
  struct DStarRoute buffer;
  memcpy(&buffer, &route, sizeof(DStarRoute));
  replace(buffer.yourCall, ' ', '_', LONG_CALLSIGN_LENGTH);
  replace(buffer.ownCall1, ' ', '_', LONG_CALLSIGN_LENGTH);
  replace(buffer.ownCall2, ' ', '_', SHORT_CALLSIGN_LENGTH);
  replace(buffer.repeater1, ' ', '_', LONG_CALLSIGN_LENGTH);
  replace(buffer.repeater2, ' ', '_', LONG_CALLSIGN_LENGTH);

  char date[DDB_DATE_LENGTH + 1];
  time_t now = ::time(NULL);
  struct tm* time = gmtime(&now);
  strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", time);

  char* command = NULL;
  asprintf(&command,
    "UPDATE %s %.8s %.8s %.8s %.8s %02X %02X %02X %.4s # %04x________________",
    date, buffer.ownCall1, buffer.repeater1, buffer.repeater2, buffer.yourCall,
    buffer.flags[0], buffer.flags[1], buffer.flags[2], buffer.ownCall2, number);
  sendCommand(command);
  free(command);
}

void DDBClient::touch(time_t now)
{
  char date[DDB_DATE_LENGTH + 1];
  struct tm* time = gmtime(&now);
  strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", time);

  number ++;

  char* command = NULL;
  asprintf(&command, "IRCDDB WATCHDOG: %s %s %d", date, version, number);
  sendCommand(command);
  free(command);
}

void DDBClient::storeUser(const char* nick, const char* name, const char* address)
{
  store->storeUser(nick, name, address);

  if (strncmp(nick, "s-", 2) == 0)
  {
    pthread_mutex_lock(&lock);
    free(bot);
    bot = strdup(nick);
    pthread_mutex_unlock(&lock);
  }

  report(LOG_DEBUG, "%s: JOIN %s\n", server, nick);
}

void DDBClient::removeUser(const char* nick)
{
  store->removeUser(nick);

  if ((bot != NULL) && (strcmp(nick, bot) == 0))
  {
    pthread_mutex_lock(&lock);
    free(bot);
    bot = store->findActiveBot();
    pthread_mutex_unlock(&lock);
  }

  report(LOG_DEBUG, "%s: QUIT %s\n", server, nick);
}

void DDBClient::requestForUpdate()
{
  if (state < DDB_TABLE_COUNT)
  {
    char date[] = "2001-01-01 00:00:00";
    store->getLastModifiedDate(state, date);
    preventDataLoop(state, date);

    char* command = NULL;
    asprintf(&command, "SENDLIST %d %s", state, date);
    sendCommand(command);
    free(command);
  }
}

void DDBClient::preventDataLoop(int table, char* date)
{
  if (store->getCountByDate(state, date) >= DDB_MAXIMUM_ENTRIES)
  {
    struct tm time1;
    time_t stamp = time(NULL);
    if (strptime(date, "%Y-%m-%d %H:%M:%S", &time1) != NULL)
    {
      stamp = timegm(&time1);
      stamp ++;
    }
    struct tm* time2 = gmtime(&stamp);
    strftime(date, DDB_DATE_LENGTH + 1, "%Y-%m-%d %H:%M:%S", time2);
  }
}

void DDBClient::storeTableData(int table, const char* date, const char* call1, const char* call2)
{
  if ((table & DDB_SYNC_MARK) || (table < state))
  {
    table &= ~DDB_SYNC_MARK;

    store->storeTableData(table, date, call1, call2);

    if (onUpdateCommand != NULL)
      onUpdateCommand(this, table, date, call1, call2);

    report(LOG_DEBUG, "%s: UPDATE %d %s %s %s\n", server, table, date, call1, call2);
  }
}

void DDBClient::handleConnect(irc_session_t* session, const char* event, const char* origin, const char** parameters, unsigned int count)
{
  DDBClient* self = (DDBClient*)irc_get_ctx(session);

  self->report(LOG_INFO, "%s: connected\n", self->server);
  self->number = 0;

  irc_cmd_join(session, DDB_IRC_CHANNEL, NULL);
}

void DDBClient::handleJoin(irc_session_t* session, const char* event, const char* origin, const char** parameters, unsigned int count)
{
  DDBClient* self = (DDBClient*)irc_get_ctx(session);
  char* nick = const_cast<char*>(origin);
  char* separator1 = strchr(nick, '!');
  char* separator2 = strchr(nick, '@');
  if ((separator1 != NULL) && (separator2 != NULL))
  {
    *separator1 = '\0';
    *separator2 = '\0';
    char* name = separator1 + 1;
    char* address = separator2 + 1;

    if (strcmp(event, "JOIN") == 0)
      self->storeUser(nick, name, address);
    if (strcmp(event, "QUIT") == 0)
      self->removeUser(nick);
  }
}

void DDBClient::handlePrivateMessage(irc_session_t* session, const char* event, const char* origin, const char** parameters, unsigned int count)
{
  DDBClient* self = (DDBClient*)irc_get_ctx(session);
  if ((count > 1) && (strncmp(origin, "s-", 2) == 0))
  {
    if (strncmp(parameters[1], "IRCDDB", 6) == 0)
    {
      // Do nothing
      return;
    }

    if (strcmp(parameters[1], "LIST_MORE") == 0)
    {
      self->requestForUpdate();
      return;
    }

    if (strcmp(parameters[1], "LIST_END") == 0)
    {
      self->state ++;
      self->requestForUpdate();
      return;
    }

    int table = 0;
    char* pointer = const_cast<char*>(parameters[1]);

    if (strncmp(pointer, "UPDATE", 6) == 0)
    {
      table += DDB_SYNC_MARK;
      pointer += 6;

      while (*pointer == ' ')
        pointer ++;
    }

    if ((pointer[0] >= '0') &&
        (pointer[0] <= '9') &&
        (pointer[1] == ' '))
    {
      table += *pointer - '0';
      pointer += 2;
    }

    if (strlen(pointer) >= DDB_DATA_LENGTH)
    {
      char* date = extract(&pointer, DDB_DATE_LENGTH);
      char* call1 = extract(&pointer, LONG_CALLSIGN_LENGTH);
      char* call2 = extract(&pointer, LONG_CALLSIGN_LENGTH);
      replace(call1, '_', ' ', -1);
      replace(call2, '_', ' ', -1);
      self->storeTableData(table, date, call1, call2);
    }
  }
}

void DDBClient::handleEventCode(irc_session_t* session, unsigned int event, const char* origin, const char** parameters, unsigned int count)
{
  DDBClient* self = (DDBClient*)irc_get_ctx(session);
  switch (event)
  {
    case LIBIRC_RFC_RPL_NAMREPLY:
      break;

    case LIBIRC_RFC_RPL_ENDOFNAMES:
      irc_send_raw(session, "WHO " DDB_IRC_CHANNEL);
      break;

    case LIBIRC_RFC_RPL_WHOREPLY:
      if (count > 6)
        self->storeUser(parameters[5], parameters[2], parameters[3]);
      break;

    case LIBIRC_RFC_RPL_ENDOFWHO:
      self->requestForUpdate();
      break;
  };
}
