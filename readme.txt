
  XReflectorPlus

  DExtra secure reflector system of D-STAR Russia network based on code of KI4LKF
  Copyright 2012-2013 by Artem Prilutskiy, R3ABM (r3abm-at-dstar.su)

  Features:
  - automatic access control of operators based on ircDDB's callsign data
  - authorization of repeaters and nodes by IP based on data of ircDDB logons
  - Hot-Spot authorization controlled via self-care portal (not published here)
  - multiple NAT types support for repeaters and Hot-Spots access

  Requirements:
  - MySQL with HandlerSocket plugin
  - libircclient-dev, libconfig-dev, libmysql-dev, hsclient

  Package contents:

  dxrfd-290 - parts of original code of dxrfd 2.90

  dxrfd-290hs - modified version of KI4LKF's dxrfd 2.90. This version uses HandlerSocket to
    access MySQL database.

  dxrfd-311 - full original version of KI4LKF's dxrfd 3.11. This version supports only new
    type of Keep-Alives (10 bytes).

  dxrfd-311dm - modified version of KI4LKF's dxrfd 3.11 (Double Mode). This version supports
    two verions of Keep-Alive messages (9 bytes and 10 bytes). KI4LKF made changes in DExtra
    keep-alive format that is not supported by most software.

  dxrfd-311hs - modified version of KI4LKF's dxrfd 3.11 based on code of dxrfd-311dm.
    This version uses HandlerSocket to access MySQL database.

  CallSync - ircDDB data collector. It is required for authorization feature of XReflectorPlus.
    Software based on code of my BorderGate so it's code quality not best but that works.

  XReflectorPlus.Sql - MySQL / HandlerSocket database for XReflectorPlus

  Data model:

  Global and Local databses (ircDDB data replica)
  Please deploy this schemas from Data/DDB-MySQL.sql

  * Tables - persistent store of ircDDB tables data
  * Users - IRC (ircDDB) user sessions
  * Gateways - materialized view of registered gateways (represented data of Users table)

  XReflectorPlus database
  Please deploay this schema from Data/XReflectorPlus-MySQL.sql

  * Spots - self-registered hot-spots
  * Gateways - materialized view of registered gateways (represented data of Users table)

  * Data - Key-Value materialized view for BTREE emulation
  * Remotes - materialized view of Gateways and Spots table, required for dxrfd 3.x and dxrfd 2.90
