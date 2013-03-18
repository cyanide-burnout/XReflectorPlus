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

#include "db.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TABLE_DATA     1
#define TABLE_REMOTES  2

#define MAXIMUM_ERROR_MESSAGE_LENGTH  80

using namespace dena;

static char message[MAXIMUM_ERROR_MESSAGE_LENGTH];

void handle_error(hstcpcli_i* client, int code)
{
  if (code != 0)
  {
    strncpy(message, client->get_error().c_str(), MAXIMUM_ERROR_MESSAGE_LENGTH);
    message[MAXIMUM_ERROR_MESSAGE_LENGTH - 1] = '\0';
  }
}

int wrap_open(DB* context, const char* unknown1, const char* file, const char* unknown2, int type, int flags, int unknown3)
{
  config configuration;
  socket_args arguments;

  char* parameter = strtok(const_cast<char*>(file), ":");
  configuration["host"] = std::string(parameter);
  
  parameter = strtok(NULL, ":");
  configuration["port"] = std::string(parameter);

  arguments.set(configuration);
  hstcpcli_ptr pointer = hstcpcli_i::create(arguments);
  context->client = pointer.get();
  pointer.release();

  if (context->client == NULL)
    return -1;

  parameter = strtok(NULL, ":");
  context->client->request_buf_auth(parameter, NULL);

  parameter = strtok(NULL, ":");
  context->client->request_buf_open_index(TABLE_DATA, parameter, "Data", "PRIMARY", "Key,Value");
  context->client->request_buf_open_index(TABLE_REMOTES, parameter, "Remotes", "PRIMARY", "Call,Address,NAT");

  size_t number = 3;
  int code = context->client->request_send();
  while ((code == 0) && (number > 0))
  {
    size_t count = 0;
    code = context->client->response_recv(count);
    context->client->response_buf_remove();
    number --;
  }

  handle_error(context->client, code);
  return code;
}

int wrap_close(DB* context, int flags)
{
  if (context->client != NULL)
  {
    context->client->close();
    delete context->client;
  }
  free(context);
  return 0;
}

int wrap_get(DB* context, const char* unknown, DBT* key, DBT* data, int flags)
{
  const string_ref operation("=", 1);
  const string_ref keys[] =
  { 
    string_ref(key->data, key->size)
  };

  context->client->request_buf_exec_generic(TABLE_DATA, operation, keys, 1, 1, 0, string_ref(), 0, 0);

  int code = context->client->request_send();
  if (code == 0)
  {
    size_t count = 0;
    code = context->client->response_recv(count);
    if (code == 0)
    {
      code = ENOENT;
      const string_ref* row = context->client->get_next_row();
      if ((count == 2) && (row != NULL))
      {
        size_t length = row[1].size();
        if (length >= data->size)
          length = data->size - 1;
        memcpy(data->data, row[1].begin(), length);
        data->data[length] = 0;
        code = 0;
      }
    }
    context->client->response_buf_remove();
  }

  handle_error(context->client, code);
  return code;
}

int db_create(DB** context, const char* unknown1, int unknown2)
{
  DB* pointer = (DB*)malloc(sizeof(DB));
  pointer->open = wrap_open;
  pointer->close = wrap_close;
  pointer->get = wrap_get;
  *context = pointer;
  return 0;
}

const char* db_strerror(int code)
{
  return (code != 0) ? message : NULL;
}

int plus_get_nat_type(DB* context, const char* call)
{
  int outcome = -1;

  const string_ref operation("=", 1);
  const string_ref keys[] =
  {
    string_ref(call, strlen(call))
  };

  context->client->request_buf_exec_generic(TABLE_REMOTES, operation, keys, 1, 1, 0, string_ref(), 0, 0);

  if (context->client->request_send() == 0)
  {
    size_t count = 0;
    if (context->client->response_recv(count) == 0)
    {
      const string_ref* row = context->client->get_next_row();
      if ((count == 3) && (row != NULL))
        outcome = atoi(row[2].begin());
    }
    context->client->response_buf_remove();
  }

  return outcome;
}

int plus_validate_connection(DB* context, const char* call, const char* address)
{
  int outcome = -1;

  const string_ref operation("=", 1);
  const string_ref keys[] =
  {
    string_ref(call, strlen(call))
  };

  context->client->request_buf_exec_generic(TABLE_REMOTES, operation, keys, 1, 1, 0, string_ref(), 0, 0);

  if (context->client->request_send() == 0)
  {
    size_t count = 0;
    if (context->client->response_recv(count) == 0)
    {
      const string_ref* row = context->client->get_next_row();
      if ((count == 3) && (row != NULL))
        outcome =
          (strlen(address) == row[1].size()) &&
          (strncmp(address, row[1].begin(), row[1].size()) == 0);
    }
    context->client->response_buf_remove();
  }

  return outcome;
}
