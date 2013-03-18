Open Source G2 DSTAR Reflectors(XRF reflectors)
==============================================
dxrfd is the software to use to create
a D-Star reflector. It will run on any Linux box.
It communicates with dstar Gateways/repeaters
and it also communicates with dvap,dvtool,... users.
Add udp port 30001 to your router.
Add udp port 20001 to your router.

Note: You can NOT run both the Reflector and the G2 linking software on the same Linux box.
That is true for all dstar networks and dstar gateways.
The reflector must run on a different box.

BUILDING dxrfd
==============
All instructions are to be executed as user ID root.
We will assume that the installation is on a CentOS Linux box

1)
Download dxrfd.zip

Create an empty directory under the root directory:   /root/dxrfd
Unpack zrf.zip into /root/dxrfd directory

    unzip  dxrfd.zip

2)
Install/update the db4-devel package off the CentOS network,
by executing the command:  

     yum install db4-devel

And also install/update the C++ compiler with the command:

     yum install gcc-c++

3)
Go to /root/dxrfd directory and execute the following commands:

  chmod  +x  *.doit

  ./dxrfd.doit

  ./dextra_create_db.doit

  ./dextra_access_db.doit

  ./xrf_lh.doit


You should have these binary programs at this point:

dxrfd
dextra_create_db
dextra_access_db
xrf_lh

SETUP 
=====
To setup the Dstar reflector, edit dxrfd.cfg configuration file.
Set the OWNER value. The OWNER value is the callsign of the DSTAR Reflector.
It must start with the letters XRF and followed by 3 digits which identify the
Reflector number.

Set the ADMIN value to be your personal callsign.

The file users.txt contains gateway callsigns and user callsigns.
Each line in users.txt is either a gateway callsign or a user callsign.
If it is a gateway callsign, then it is followed by a vertical bar(|) and then an IP.
It does not matter what the value for the IP is.
If it is a user callsign, only the user callsign is listed, no ip address.

A gateway is listed with its gateway callsign followed by the vertical bar
and followed by the IP address(Host name is OK too).
The IP address does not have to be the current IP address of the repeater.
But if you set: IP_CHECK=Y  (in dxrfd.cfg)
then the reflector will check the IP address of the incoming repeater
to make sure that the incoming IP address matches the IP address in the file.
Most of the time we do not care if the IP address does not match,
because some repeaters change IP address every day.

All user callsigns are allowed to connect to the reflector, 
but only user callsigns listed in users.txt will be allowed to transmit.

If you want to block repeaters from connecting to your reflector,
then remove the gateway callsign from users.txt

Now execute the command:  ./dextra_create_db  dextra.db  users.txt
This will create the database file dextra.db

To verify that you have a good database, you can execute this:
./dextra_access_db  dextra.db  p

Now you will configure the dxrfd Linux service:
Execute these commands:

    chmod +x dxrfd.SVC
    mv dxrfd.SVC /etc/init.d/dxrfd
    chkconfig --add dxrfd
    chkconfig dxrfd on

Now, start the Linux service dxrfd with the command:   service  dxrfd  start

The log file is /var/log/dxrfd.log

NOTE:
   Once dxrfd starts, it uses the btree file dextra.db
   to validate incoming G2 Gateways.
   Do NOT delete or move or edit the file dextra.db
     and do NOT change it in any way while dxrfd is running.

Check that it is running with the command:  service  dxrfd  status

Now your database has been populated and your
DSTAR reflector is ready to accept links from remote G2 Gateways
or connections from remote users.

If you want to add or remove data from dextra.db while the XRF is running,
you can use the tool dextra_access_db

CONTROLLING dxrfd using shell commands
==============================================================
dxrfd server software can be controlled
from the shell using netcat(nc) commands on Linux

In dxrfd.cfg there is a
COMMAND_PORT that is used to gain access to either software.

The default COMMAND_PORT for dxrfd is COMMAND_PORT=30002
as listed in dxrfd.cfg

The following commands can be sent to dxrfd from 
within netcat(nc) for Linux.

First start netcat by executing this command:   nc  -u  127.0.0.1  30002

Note: In the above netcat command, 
we use local IP address 127.0.0.1  because in dxrfd.cfg, we have set: LISTEN_IP=0.0.0.0

After you executed the above netcat command, nc will wait there for you to type a specific sub-command.
The sub-commands that you can use are listed below:

pu                   "print users"
mu                   "mute users"
uu                   "unmute users"
pl                   "print links"
sh                   "shut it down"
pb                   "print blocked callsigns"
ab KI4LKF            "add a block on KI4LKF"
rb KI4LKF            "remove the block on KI4LKF"
mc KI4LKF            "mute the callsign KI4LKF"
uc KI4LKF            "unmute the callsign KI4LKF"
qsoy                 "qso details set to YES"
qson                 "qso details set to NO"
pv                   "print the current software version"


Here is an example of the output of the command pu

nc -u  127.0.0.1  30002
pu
KJ4NHF  ,1.2.3.4,REPEATER,062909,22:43:23,notMuted

Notes:
The above 3 lines are explaied as follows:
The first 2 lines, is what you typed.
The third line is the reply back from your XRF reflector.

After you get the reply from the XRF reflector, you can choose to type another sub-command
or you can choose to hit Control-C on the keyboard to exit the netcat and return back to the Linux prompt.

The third line says that the connected item is a LINKED dstar Repeater and it was linked at 062909,22:43:23  

The status file for dxrfd is XRF_STATUS.txt
which lists all the links for the 5 reflector modules if any module is linked.

DISPLAYING the dstar reflector activity on your web site
=========================================================
First go to the HTML directory of your web server.
That directory is usually /var/www/html/
Note:  On some Web servers, the HTML directory is /var/www/
Under the HTML directory of your web server, create a sub-directory g2_link
Under that sub-directory g2_link, place the files:  mm_spacer.gif and mm_training.css

Now go to the directory where you installed dxrfd.
Use the program xrf_lh as follows, by executing it like this:
   ./xrf_lh yourPersonalCallsign ReflectorCallsign description 127.0.0.1 > status.html

Example: (We use a local XRF reflector XRF999 for testing )
   ./xrf_lh  KI4LKF  XRF999  "dxrfd 2.89"  127.0.0.1  > status.html

You can take the generated status.html file and place it under the HTML directory
of your web server.
To make things fast and simple, you can add this line to your CRONTAB (for root)
I use my callsign KI4LKF and the local XRF reflector XRF999

*/2  *  *  *  *  /root/dxrfd/xrf_lh   KI4LKF  XRF999  "dxrfd v2.89"  127.0.0.1  >/var/www/html/XRF999_status.html   2>/var/log/xrf_lh.log

the above line tells your CRON Linux service to execute the xrf_lh every 2 minutes.
That means that your dashboard for the XRF999 reflector will be created every 2 minutes.

73
KI4LKF
