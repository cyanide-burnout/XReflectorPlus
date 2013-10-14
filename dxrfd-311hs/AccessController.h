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
