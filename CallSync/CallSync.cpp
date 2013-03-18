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

#include <sys/select.h>
#include <execinfo.h>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>
#include <syslog.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include <libutil.h>
#include <libconfig.h>
#include <libircclient/libircclient.h>

#include "AutoPointer.h"

#include "DStar.h"
#include "DDBClient.h"
#include "MySQLStore.h"
#include "SQLiteStore.h"
#include "StoreFactory.h"
#include "ReportTools.h"
#include "StringTools.h"

#define VERSION              "CallSync:20120920"

#define OPTION_DAEMON_MODE   1

#define POLL_INTERVAL        2

#define READ_SET             0
#define WRITE_SET            1

#define SET_COUNT            2
#define TRACE_DEPTH          10

volatile bool running = false;

void handleSignal(int signal)
{
  running = false;
}

void handleFault(int signal)
{
  void* buffer[TRACE_DEPTH];
  size_t count = backtrace(buffer, TRACE_DEPTH);
  char** symbols = backtrace_symbols(buffer, count);
  report(LOG_CRIT, "Segmentation fault, %d addresses\n", count);
  if (symbols != NULL)
    for (size_t index = 0; index < count; index ++)
      report(LOG_CRIT, "  at: %s\n", symbols[index]);

  exit(EXIT_FAILURE);
}

void handleReport(int priority, const char* format, va_list arguments)
{
  doReport(priority, format, arguments);
  if (priority < LOG_ERR)
    running = false;
}

int main(int argc, const char* argv[])
{
  sigset(SIGSEGV, handleFault);

  printf("\n");
  printf("CallSync ircDDB downloader\n");
  printf("Copyright 2012 Artem Prilutskiy (R3ABM, cyanide.burnout@gmail.com)\n");
  printf("\n");

  struct option options[] =
  {
    { "config", required_argument, NULL, 'c' },
    { "pidfile", required_argument, NULL, 'p' },
    { "daemon", no_argument, NULL, 'd' },
    { NULL, 0, NULL, 0 }
  };

  char* file = NULL;
  char* identity = NULL;
  int option = 0;

  int selection = 0;
  while ((selection = getopt_long(argc, const_cast<char* const*>(argv), "c:p:d", options, NULL)) != EOF)
    switch (selection)
    {
      case 'c':
        file = optarg;
        break;

      case 'p':
        identity = optarg;
        break;

      case 'd':
        option |= OPTION_DAEMON_MODE;
        break;
    }

  if (file == NULL)
  {
    printf("Usage: %s --config <path to configuration file> [--pidfile <path to PID file>] [--daemon]\n", argv[0]);
    return EXIT_FAILURE;
  }

  if (option & OPTION_DAEMON_MODE)
  {
    setReportOutput(LOGGER_OUTPUT_SYSLOG);
    if (daemon(-1, -1) < 0)
    {
      report(LOG_ERR, "Error launching daemon");
      return EXIT_FAILURE;
    }
  }

  config_t configuration;
  config_init(&configuration);

  if (config_read_file(&configuration, file) == CONFIG_FALSE)
  {
    report(LOG_ERR, "Configuration error at %s:%d: %s\n",
      file,
      config_error_line(&configuration),
      config_error_text(&configuration));
    config_destroy(&configuration);
    return EXIT_FAILURE;
  }

  const char* server = NULL;
  const char* password = NULL;
  const char* name = NULL;
  const char* type = NULL;
  const char* path = NULL;
  long port = DDB_DEFAULT_PORT;

  config_setting_t* setting = config_root_setting(&configuration);
  config_setting_lookup_string(setting, "server", &server);
  config_setting_lookup_string(setting, "password", &password);
  config_setting_lookup_string(setting, "name", &name);
  config_setting_lookup_string(setting, "type", &type);
  config_setting_lookup_string(setting, "file", &path);
  config_setting_lookup_int(setting, "port", &port);

  if ((server == NULL) ||
      (password == NULL) ||
      (name == NULL) ||
      (type == NULL) ||
      (file == NULL))
  {
    report(LOG_ERR, "Configuration file is incorrect\n");
    config_destroy(&configuration);
    return EXIT_FAILURE;
  }

  AutoPointer<DDBClient::Store> store(StoreFactory::createStore(type, path, handleReport));

  if (store.get() == NULL)
  {
    report(LOG_ERR, "Configuration file has incorrect store setting\n");
    config_destroy(&configuration);
    return EXIT_FAILURE;
  }

  DDBClient client(store.get(), server, port, name, password, VERSION);
  client.onReport = (DDBClient::ReportHandler)handleReport;

  struct pidfh* handle = NULL;
  if (identity != NULL)
  {
    handle = pidfile_open(identity, 0644, NULL);
    if (handle == NULL)
    {
      report(LOG_ERR, "Can not create PID file");
      config_destroy(&configuration);
      return EXIT_FAILURE;
    }
    pidfile_write(handle);
  }

  config_destroy(&configuration);

  running = true;

  sigset(SIGHUP, handleSignal);
  sigset(SIGINT, handleSignal);
  sigset(SIGTERM, handleSignal);

  fd_set sets[SET_COUNT];
  timeval timeout;

  report(LOG_INFO, "Started\n");

  while (running)
  {

    for (size_t index = 0; index < SET_COUNT; index ++)
      FD_ZERO(&sets[index]);

    int descriptors = FD_SETSIZE;
    irc_add_select_descriptors(client.getSession(), &sets[READ_SET], &sets[WRITE_SET], &descriptors);

    timeout.tv_sec = POLL_INTERVAL;
    timeout.tv_usec = 0;

    int count = select(FD_SETSIZE, &sets[READ_SET], &sets[WRITE_SET], NULL, &timeout);

    if (count == EOF)
      break;

    irc_process_select_descriptors(client.getSession(), &sets[READ_SET], &sets[WRITE_SET]);

    if (!client.isConnected())
    {
      client.disconnect();
      client.connect();
    }

  }

  report(LOG_INFO, "Stopped\n");

  running = false;

  if (handle != NULL)
    pidfile_remove(handle);

  return EXIT_SUCCESS;
}
