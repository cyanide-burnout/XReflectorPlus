#include "AccessController.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#define TABLE_DATA     1
#define TABLE_REMOTES  2

// Import from dxrfd.cpp
void traceit(const char* fotmat, ...);
#define log traceit

AccessController::AccessController()
{

}

AccessController::~AccessController()
{

}

void AccessController::handleError(int code)
{
  if (code != 0)
  {
    const char* error = client->get_error().c_str();
    log("HandlerSocket error: %s\n", error);
  }
}

void AccessController::connect(const char* location)
{
  dena::config configuration;
  dena::socket_args arguments;

  char* parameter = strtok(const_cast<char*>(location), ":");
  configuration["host"] = std::string(parameter);
  
  parameter = strtok(NULL, ":");
  configuration["port"] = std::string(parameter);

  arguments.set(configuration);
  client = dena::hstcpcli_i::create(arguments);

  if (client.get() == NULL)
  {
    log("Error creating HandlerSocket client.");
    return;
  }

  parameter = strtok(NULL, ":");
  client->request_buf_auth(parameter, NULL);

  parameter = strtok(NULL, ":");
  client->request_buf_open_index(TABLE_DATA, parameter, "Data", "PRIMARY", "Key,Value");
  client->request_buf_open_index(TABLE_REMOTES, parameter, "Remotes", "PRIMARY", "Call,Address,NAT");

  size_t number = 3;
  int code = client->request_send();
  while ((code == 0) && (number > 0))
  {
    size_t count = 0;
    code = client->response_recv(count);
    client->response_buf_remove();
    number --;
  }

  handleError(code);
}

bool AccessController::configure(const char* key, const char* value)
{
  if (strcmp(key, "DATABASE") == 0)
  {
    connect(value);
    return true;
  }
  return false;
}

int AccessController::getAccessType(const char* call)
{
  int outcome = -1;

  const dena::string_ref operation("=", 1);
  const dena::string_ref keys[] =
  {
    dena::string_ref(call, strlen(call))
  };

  client->request_buf_exec_generic(TABLE_REMOTES, operation, keys, 1, 1, 0, dena::string_ref(), 0, 0);
  int code = client->request_send();

  if (code == 0)
  {
    size_t count = 0;
    code = client->response_recv(count);
    if (code == 0)
    {
      const dena::string_ref* row = client->get_next_row();
      if ((count == 3) && (row != NULL))
        outcome = atoi(row[2].begin());
    }
    client->response_buf_remove();
  }

  handleError(code);
  return outcome;
}

bool AccessController::validateCallSign(const char* call)
{
  bool outcome = false;

  const dena::string_ref operation("=", 1);
  const dena::string_ref keys[] =
  {
    dena::string_ref(call, strlen(call))
  };

  client->request_buf_exec_generic(TABLE_DATA, operation, keys, 1, 1, 0, dena::string_ref(), 0, 0);
  int code = client->request_send();

  if (code == 0)
  {
    size_t count = 0;
    code = client->response_recv(count);
    if (code == 0)
    {
      const dena::string_ref* row = client->get_next_row();
      outcome = (row != NULL);
    }
    client->response_buf_remove();
  }

  handleError(code);
  return outcome;
}


bool AccessController::validateConnection(const char* call, const char* address)
{
  bool outcome = false;

  const dena::string_ref operation("=", 1);
  const dena::string_ref keys[] =
  {
    dena::string_ref(call, strlen(call))
  };

  client->request_buf_exec_generic(TABLE_REMOTES, operation, keys, 1, 1, 0, dena::string_ref(), 0, 0);
  int code = client->request_send();

  if (code == 0)
  {
    size_t count = 0;
    code = client->response_recv(count);
    if (code == 0)
    {
      const dena::string_ref* row = client->get_next_row();
      if ((count == 3) && (row != NULL))
        outcome =
          (strlen(address) == row[1].size()) &&
          (strncmp(address, row[1].begin(), row[1].size()) == 0);
    }
    client->response_buf_remove();
  }

  handleError(code);
  return outcome;
}
