<?php

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

  function getConnectedUsers()
  {
    $outcome = array();
    $handle = fsockopen("udp://127.0.0.1", 30002);
    stream_set_timeout($handle, 1);
    fwrite($handle, "pu\n");
    do
    {
      $line = trim(fread($handle, 80));
      if (strlen($line) > 8)
      {
        $parameters = explode(",", $line);
        $call = trim($parameters[0]);
        $outcome[$call] = $parameters;
      }
    }
    while (($line != "OK") && ($line != ""));
    fclose($handle);
    return $outcome;
  }

?>
