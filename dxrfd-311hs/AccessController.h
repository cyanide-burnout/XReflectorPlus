#ifndef ACCESSCONTROLLER_H
#define ACCESSCONTROLLER_H

#include <handlersocket/hstcpcli.hpp>

#define CONTROLLER_PARAMETERS_COUNT  1

class AccessController
{
  public:

    AccessController();
    ~AccessController();

    bool configure(const char* key, const char* value);

    int getAccessType(const char* call);
    bool validateCallSign(const char* call);
    bool validateConnection(const char* call, const char* address);

  private:

    dena::hstcpcli_ptr client;

    void handleError(int code);
    void connect(const char* location);

};

#endif
