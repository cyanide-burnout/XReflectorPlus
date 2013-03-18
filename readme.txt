
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

  dxrfd-290hs - modified version of KI4LKF's dxrfd 2.90. This is most stable package recomended to use.

  dxrfd-310 - modified version of KI4LKF's dxrfd 3.10. This version still in investigation.
    KI4LKF made changes in DExtra keep-alive format that is not supported by most software.
    Fixed version works unstable in production environment. We not yet recomend use it.
    REMOVED FROM DISTRIBUTION!

  CallSync - ircDDB data collector. It is required for authorization feature of XReflectorPlus.
    Software based on code of my BorderGate so it's code quality not best but that works.

  XReflectorPlus.Sql - MySQL / HandlerSocket database for XReflectorPlus

  Data model:

  CallSync (ircDDB data replica)

  * Tables - persistent store of ircDDB tables data
  * Users - IRC (ircDDB) user sessions

  Self-Care Web Portal

  * Spots - self-registered hot-spots

  Dashboard

  * Gateways - materialized view of registered gateways (represented data of Users table)

  dxrfd

  * Data - Key-Value materialized view for BTREE emulation
  * Remotes - materialized view of Gateways and Spots table, required for dxrfd 3.x and dxrfd 2.90
