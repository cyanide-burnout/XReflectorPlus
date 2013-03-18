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

#ifndef AUTOPOINTER_H
#define AUTOPOINTER_H

template <typename Type>
  class AutoPointer
{
  public:
    typedef Type element_type;

    explicit AutoPointer<Type>(Type* pointer = 0)
    {
      value = pointer;
    }

    AutoPointer<Type>(AutoPointer<Type>& pointer)
    {
      value = pointer.value;
    }

    AutoPointer<Type>& operator=(AutoPointer<Type>& pointer)
    {
      return value;
    }

    ~AutoPointer<Type>()
    {
      if (value)
        delete value;
    }

    Type& operator*() const
    {
      return *value;
    }

    Type* operator->() const
    {
      return value;
    }

    Type* get() const
    {
      return value;
    }

    Type* release()
    {
      Type* pointer = value;
      value = 0;
      return pointer;
    }

    void reset(Type* pointer = 0)
    {
      if (value)
        delete value;
      value = pointer;
    }

  private:
    Type* value;

};

#endif // AUTOPOINTER_H
