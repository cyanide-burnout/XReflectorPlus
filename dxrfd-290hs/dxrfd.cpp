
/* by KI4LKF */

#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdarg.h>
#include <signal.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <time.h>

#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>

/* Required for Binary search trees using C++ STL */
#include <string>
#include <map>
#include <set>
#include <utility>
using namespace std;

#include <pthread.h>

/* DB interface */
#include "db.h"

/*** version number must be x.xx ***/
#define VERSION "2.90"
#define OWNER_SIZE 8
#define IP_SIZE 15
#define MAXHOSTNAMELEN 64
#define CALL_SIZE 8

/* 
   Timeout is 30 seconds.
   If after 30 seconds, we have not received the KEEPALIVE,
   we drop that station
*/
static short TIMEOUT = 60;

static char STATUS_FILE[FILENAME_MAX + 1];

/* configuration data */
/* Put that in a structure, later */
static char OWNER[OWNER_SIZE + 1];
static char ADMIN[CALL_SIZE + 1];
static char LISTEN_IP[IP_SIZE + 1];
static int LISTEN_PORT = 30001;
static int COMMAND_PORT = 30002;

/* max number of XRF repeaters and XRF dongles */
static unsigned int MAX_USERS = 10;

/* max number of dvap/dvtool dongles */
static unsigned int MAX_OTHER_USERS=10;

static bool QSO_DETAILS = false;
static bool IP_CHECK = true;
static char USERS[FILENAME_MAX + 1];

// Input from XRF repeaters and XRF dongles
static int srv_sock = -1;

// Input from admin commands
static int cmd_sock = -1;

// Input from dvap, dvtool dongles
static int ref_sock = -1;

/* ACK back to dvap, dvtool users */
static unsigned char REF_ACK[3] = { 3, 96, 0 };

/*** for replying with DASHBOARD information ***/
#define LH_MAX_SIZE 39
typedef map<string, string> lh_type;
static lh_type lh_list;
typedef map<string, string> dt_lh_type;
static dt_lh_type dt_lh_list;

/* inbound dvap, dvtool ... */
struct inbound
{
   /* the callsign of the remote */
   char call[CALL_SIZE + 1];

   /* if true, packets from this call are dropped */
   bool isMute;

   time_t connect_time;

   /* IP and port of remote */
   struct sockaddr_in sin;

   /* if countdown expires, the connection is terminated */
   short countdown;

   /* This user talked on this module */
   char mod;  /* A B C D E*/

   bool is_ref; 
   char links[5]; 
   /* 
      The index is the local module: 0=A, 1=B, 2=C, 3=D, 4=E
      The value is the remote module
      Example:  links[1] = 'C'  
      That means that our local module B is linked to remote module C
      The remote system is identified by inbound->call
   */ 

   /* the serial number of the dongle */
   char serial[9];

};
/* the Key in this inbound_list map is the unique IP address of the remote */
typedef map<string, inbound *> inbound_type;
static inbound_type inbound_list;

/* 
   Just before we send data to dvtool/dvap users, 
   we save the header here, for re-transmit later
*/
static struct
{
   unsigned char hdr[58];
   time_t last_time;
} temp_r[5];

/* inbound XRF repeaters and XRF dongles */
struct a_user
{
   /* callsign of the connected station */
   char call[CALL_SIZE + 1];

   /* if true, packets from this call are dropped */
   bool isMute;

   /* is this a repeater connection, or a dongle user */
   bool is_repeater;

   bool is_xrf;

   time_t connect_time;

   /* The first index identifies the local reflector module
      index 0 identifies reflector module A
      index 1 identifies reflector module B
      index 2 identifies reflector module C
      index 3 identifies reflector module D
      index 4 identifies reflector module E

      The second index identifies the remote repeater band.
      from 0 to 3
      which is from A...D

      Example:
               rpt_mods[1][2] = 'C'
               rpt_mods[1][3] = 'D'

      Explanation:
               Reflector module B is linked to repeater module C
               Reflector module B is linked to repeater module D
          This means that the remote repeater 
               has linked both repeater bands C and D to our reflector module B
   */
   char rpt_mods[5][4];

   /* time link was established */
   time_t link_time[5][4];

   /* IP address and UDP port of connected station */
   /* For easy access to the connected station */
   struct sockaddr_in sin;

   /* if countdown expires, the connection is terminated */
   short countdown;

   /* This user talked on this module */
   char mod; /* A B C D E */

   char serial[9];

};
/* The BST/map of connected users */
/* The KEY is the unique IP address string of ip[IP_SIZE + 1] */
/* The data is a pointer to struct a_user */
typedef map<string, struct a_user *> a_user_list_type;
static a_user_list_type a_user_list;

/* 
   Just before we send the data to XRF repeaters and XRF dongles,
   we save the header here for re-transmit later
*/
static struct
{
   unsigned char hdr[56];
   time_t last_time;
} temp_x[5];


/* 
   The BST/set of blocked callsigns.
   The KEY is the unique blocked callsign.
   There is no data in the set.
   Blocked callsigns added by the administrator of the reflector. 
*/
typedef set<string> blocks_type;
static blocks_type blocks; 

/* socket descriptor set */
static fd_set fdset;

/* 20 ms delay */
static struct timeval tv;

static time_t tNow = 0;
/* timing for XRF users */
static time_t HBinterStart = 0;
/* timing for dvtool/dvap users */
static time_t inboundStart = 0;

/* Variables used by more than one function */
static const short READBUFFER_SIZE = 1024;
static unsigned char readBuffer[READBUFFER_SIZE];
static struct sockaddr_in fromUser;
static struct sockaddr_in fromCmd;

/* dvap, dvtool input buffer */
static unsigned char refbuf[READBUFFER_SIZE];
static struct sockaddr_in fromInbound;

/* log file for tracing data */
static FILE *logfp = NULL;

/* status file */
static FILE *statusfp = NULL;

/* DB database, keys and access flag */
static DB *dbp = NULL;
static DBT key, data;
static u_int32_t flags = 0;

/*** rcd data ***/
static pthread_t playback_thread;
pthread_attr_t attr;
/* 30 users recording at the same time */
static const unsigned short int MAX_RCD_USERS = 30;
/* each user can record 750 packets = 15 seconds of recorded audio */
static const unsigned short int MAX_RCD_DATA = 750;
struct rcd
{
   bool locked;
   time_t ts;
   struct sockaddr_in sin;
   short int recvlen;
   unsigned short idx; /*** index into data ***/
   unsigned char data[MAX_RCD_DATA][58]; /*** 10 seconds ***/
};
/* key is streamid */
typedef map<string, struct rcd *> rcd_list_type;
static rcd_list_type rcd_list;
static struct rcd *an_rcd;
static rcd_list_type::iterator rcd_pos;
static pair<rcd_list_type::iterator,bool> rcd_insert_pair;
static char an_rcd_streamid[24];
static time_t check_rcd_time = 0;

static bool keep_running = true;

/* The reflector uses these functions only */
static void send_a_pkt_to_all(uint32_t s_addr, int recvlen, bool from_ref);
static void check_heartbeat();
static void print_users();
static void print_version();
static void mute_users(bool mute);
static bool mute_call(char *call, bool mute);
static void print_blocks();
static void print_links_file();
static void print_links_screen();
static bool get_ip(char *call, char *ip);
static void handle_cmd(char *buf);
static void runit();
static void traceit(const char *fmt,...);
static int  read_config(char *);
static int  srv_open();
static int  cmd_open();
static void sigCatch(int signum);
static int  open_users(char *filename);
static void *playback(void *arg);

/* dvap, dongles, ... */
static void send_heartbeat();
static int  ref_open();
static int link_to_ref(char *call);
static int link_to_xrf(char local_mod, char *ref, char remote_mod, char *IP);

static int link_to_xrf(char local_mod, char *ref, char remote_mod, char *IP)
{
   short counter;
   char request[CALL_SIZE + 3];
   struct sockaddr_in sin;
   a_user_list_type::iterator user_pos;

   if (remote_mod == 'X')
      remote_mod = ' ';  /* unlink request */
   else
   {
      user_pos = a_user_list.find(IP);
      if (user_pos != a_user_list.end())
      {
         /* It already exists */
         traceit("Remote reflector [%s] ip=%s already linked, unlink first\n", ref, IP); 
         return 0;
      }
   }

   /* send the request to the remote XRF reflector */
   strcpy(request, OWNER);
   request[8] = local_mod;
   request[9] = remote_mod;
   request[10] = '\0';

   memset(&sin,0,sizeof(struct sockaddr_in));
   sin.sin_family = AF_INET;
   sin.sin_port = htons(LISTEN_PORT);
   sin.sin_addr.s_addr = inet_addr(IP);

   traceit("Sending request [%s] to reflector %s\n", request,ref);

   for (counter = 0; counter < 5; counter++)
      sendto(srv_sock, request, CALL_SIZE + 3,
             0,(struct sockaddr *)&sin,
             sizeof(struct sockaddr_in));

   return 0;
}

static int link_to_ref(char *call)
{
   char payload[MAXHOSTNAMELEN + 1];
   inbound_type::iterator inbound_pos;
   inbound *inbound_ptr;
   pair<inbound_type::iterator,bool> inbound_insert_pair;
   struct sockaddr_in sin;
   unsigned char queryCommand[56];
   unsigned short j;

   char local_mod = ' ';
   char ref[CALL_SIZE + 1];
   char remote_mod = ' ';

   local_mod = call[0];

   if ((local_mod != 'A') && (local_mod != 'B') && 
       (local_mod != 'C') && (local_mod != 'D')) 
   {
      traceit("Invalid local module %c for linking\n", local_mod);
      return -1;
   }

   memset(ref, ' ', sizeof(ref));
   memcpy(ref, call + 1, 6);
   ref[8] = '\0'; 

   if ((memcmp(ref, "REF", 3) != 0) && (memcmp(ref, "XRF", 3) != 0))
   {
      traceit("XRF or REF only\n");
      return -1;
   }

   if (strcmp(ref, OWNER) == 0)
   {
      traceit("Can not link to itself\n");
      return -1;
   }

   remote_mod = call[7];
   if ((remote_mod != 'A') && (remote_mod != 'B') && (remote_mod != 'C') && 
       (remote_mod != 'D') && (remote_mod != 'X'))
   {
      traceit("Invalid remote module %c\n", remote_mod);
      return -1;
   }

   payload[0] = '\0';
   /* get the IP address from database */
   if (!get_ip(ref, payload))
   {
      traceit("%s not found in database\n", ref);
      return -1;
   }

   /* No IP in db? */
   if (payload[0] == '\0')
   {
      traceit("No host or IP address for %s\n", ref);
      return -1;
   }

   if (strcmp(payload, "0.0.0.0") == 0)
   {
      traceit("Invalid IP in db\n");
      return -1;
   }

   if (memcmp(ref, "XRF", 3) == 0)
   {
      return link_to_xrf(local_mod, ref, remote_mod, payload);
   }

   /* Is it already linked? */
   inbound_pos = inbound_list.find(payload);
   if (inbound_pos != inbound_list.end())
   {
      inbound_ptr = (inbound *)inbound_pos->second;
      if (!inbound_ptr->is_ref)
      {
         traceit("This connection is not a reflector\n");
         return -1;
      }

      /* update the links */
      if (remote_mod == 'X')
         remote_mod = ' ';
      else
      if ((inbound_ptr->links[0] == remote_mod) ||
          (inbound_ptr->links[1] == remote_mod) ||
          (inbound_ptr->links[2] == remote_mod) ||
          (inbound_ptr->links[3] == remote_mod) ||
          (inbound_ptr->links[4] == remote_mod))
      {
         traceit ("Already set or duplicate assignment\n");
         return 0;
      }

      if (local_mod == 'A')
         inbound_ptr->links[0] = remote_mod;
      else
      if (local_mod == 'B')
         inbound_ptr->links[1] = remote_mod;
      else
      if (local_mod == 'C')
         inbound_ptr->links[2] = remote_mod;
      else
      if (local_mod == 'D')
         inbound_ptr->links[3] = remote_mod;
      else
         inbound_ptr->links[4] = remote_mod;

      if ((inbound_ptr->links[0] == ' ') && 
          (inbound_ptr->links[1] == ' ') &&
          (inbound_ptr->links[2] == ' ') &&
          (inbound_ptr->links[3] == ' ') &&
          (inbound_ptr->links[4] == ' '))
          
      {
         /* all links removed, disconnect from remote system */

         traceit("All links removed, disconnecting from %s\n", inbound_ptr->call);
         refbuf[0] = 5;
         refbuf[1] = 0;
         refbuf[2] = 24;
         refbuf[3] = 0;
         refbuf[4] = 0;
         sendto(ref_sock,(char *)refbuf,5,0,
                (struct sockaddr *)&(inbound_ptr->sin),
                sizeof(struct sockaddr_in));

         free(inbound_pos->second);
         inbound_pos->second = NULL;
         inbound_list.erase(inbound_pos);
      } 
      print_links_file();
      return 0;
   }

   if (remote_mod == 'X')
   {
      traceit("X can not be used, there is no connection to unlink\n");
      return -1;
   }

   if ((inbound_list.size() + 1) > MAX_OTHER_USERS)
   {
      traceit("Over the MAX_OTHER_USERS limit of %d\n", MAX_OTHER_USERS);
      return -1;
   }

   sin.sin_family = AF_INET;
   sin.sin_addr.s_addr = inet_addr(payload);
   sin.sin_port = htons(20001);
  
   /* assume that we will be ok */
   inbound_ptr = (inbound *)malloc(sizeof(inbound));
   if (inbound_ptr)
   {
      inbound_ptr->countdown = TIMEOUT;
      memcpy((char *)&(inbound_ptr->sin),(char *)&sin, sizeof(struct sockaddr_in));
      strcpy(inbound_ptr->call, ref);
      time(&(inbound_ptr->connect_time));
      inbound_ptr->isMute = false;
      inbound_ptr->mod = ' ';
      inbound_ptr->is_ref = true;
      inbound_ptr->links[0] = inbound_ptr->links[1] = inbound_ptr->links[2] = inbound_ptr->links[3] = inbound_ptr->links[4] = ' '; 
      if (local_mod == 'A')
         inbound_ptr->links[0] = remote_mod;
      else
      if (local_mod == 'B')
         inbound_ptr->links[1] = remote_mod;
      else
      if (local_mod == 'C')
         inbound_ptr->links[2] = remote_mod;
      else
      if (local_mod == 'D')
         inbound_ptr->links[3] = remote_mod;
      else
         inbound_ptr->links[4] = remote_mod;

      strcpy(inbound_ptr->serial, "REF     ");

      inbound_insert_pair = inbound_list.insert(pair<string, inbound *>(payload, inbound_ptr));
      if (inbound_insert_pair.second)
      {
         traceit("new CALL=%s,REPEATER,ip=%s,users=%d\n",
                  inbound_ptr->call,payload,inbound_list.size() + a_user_list.size());

         /* request to connect */
         queryCommand[0] = 5;
         queryCommand[1] = 0;
         queryCommand[2] = 24;
         queryCommand[3] = 0;
         queryCommand[4] = 1;
         for (j = 0; j < 1; j++)
            sendto(ref_sock,(char *)queryCommand,5,0,
                   (struct sockaddr *)&(inbound_ptr->sin),
                   sizeof(struct sockaddr_in));

         print_links_file();
      }
      else
      {
         traceit("failed to add CALL=%s,ip=%s\n",inbound_ptr->call,payload);
         free(inbound_ptr);
         inbound_ptr = NULL;
         return -1;
      }
   }
   else
   {
      traceit("malloc() failed for call=%s,ip=%s\n",ref,payload);
      return -1;
   }
   return 0;
}

/* send keepalive to dvap, dongles, ... */
static void send_heartbeat()
{
   inbound_type::iterator pos;
   inbound *inbound_ptr;
   blocks_type::iterator block_pos;
   bool a_station_was_dropped = false;
  
   for (pos = inbound_list.begin(); pos != inbound_list.end(); pos++)
   {
      inbound_ptr = (inbound *)pos->second;
 
      block_pos = blocks.find(inbound_ptr->call);
 
      if (block_pos == blocks.end())  /* not blocked */
      { 
         sendto(ref_sock,(char *)REF_ACK,3,0,
                (struct sockaddr *)&(inbound_ptr->sin),
                sizeof(struct sockaddr_in));
   
         if (inbound_ptr->countdown >= 0)
            inbound_ptr->countdown --;

         if (inbound_ptr->countdown < 0)
         { 
            traceit("call=%s timeout, removing %s, users=%d\n",
                 inbound_ptr->call, 
                 pos->first.c_str(),
                 inbound_list.size() - 1 + a_user_list.size());

            a_station_was_dropped = true;

            free(pos->second);
            pos->second = NULL;
            inbound_list.erase(pos);
         }
      }
      else
      {
         traceit("call=%s blocked, removing %s, users=%d\n",
                  inbound_ptr->call,
                  pos->first.c_str(),
                  inbound_list.size() - 1 + a_user_list.size());

         /* disconnect */
         refbuf[0] = 5;
         refbuf[1] = 0;
         refbuf[2] = 24;
         refbuf[3] = 0;
         refbuf[4] = 0;
         sendto(ref_sock,(char *)refbuf,5,0,
                (struct sockaddr *)&(inbound_ptr->sin),
                sizeof(struct sockaddr_in));

         a_station_was_dropped = true;

         free(pos->second);
         pos->second = NULL;
         inbound_list.erase(pos);
      }
   }

   if (a_station_was_dropped)
      print_links_file();
}

static int open_users(char *filename)
{
   int rc = 0;
   rc = db_create(&dbp, NULL, 0);
   if (rc != 0)
   {
      traceit("db_create() failed: error=%s\n", db_strerror(rc));
      return rc;
   }

   flags = DB_RDONLY;
   rc = dbp->open(dbp,NULL,filename,NULL,DB_BTREE,flags,0);
   if (rc != 0)
   {
      traceit("db_open() %s failed: error=%s\n", filename, db_strerror(rc));
      dbp->close(dbp, 0);
      dbp = NULL;
      return rc;
   }
   traceit("Database %s opened OK\n", filename);
   return rc;
}

/* trace data to the log */
static void traceit(const char *fmt,...)
{
   time_t ltime;
   struct tm tm;
   const short BFSZ = 256;
   char buf[BFSZ];

   time(&ltime);
   localtime_r(&ltime,&tm);

   snprintf(buf,BFSZ - 1,"%02d%02d%02d at %02d:%02d:%02d:",
            tm.tm_mon+1,tm.tm_mday,tm.tm_year % 100,
            tm.tm_hour,tm.tm_min,tm.tm_sec);

   va_list args;
   va_start(args,fmt);
   vsnprintf(buf + strlen(buf), BFSZ - strlen(buf) -1, fmt, args);
   va_end(args);

   fprintf(logfp, "%s",buf);
   return;
}

/* Search for a repeater or a user */
static bool get_ip(char *call, char *ip)
{
   int rc;
   char payload[MAXHOSTNAMELEN + 1];

   struct hostent *hp;
   struct in_addr ia;

   memset(&key, 0, sizeof(DBT));
   memset(&data, 0, sizeof(DBT));

   key.data = call;
   key.size = CALL_SIZE + 1;

   data.data = payload;
   data.size = MAXHOSTNAMELEN + 1;

   rc = dbp->get(dbp, NULL, &key, &data, 0);
   if (rc != 0)
      return false;

   if ((*((char *)data.data) != '\0') && ip)
   {
      hp = gethostbyname((char *)data.data);
      if (!hp)
         traceit("Call %s is host %s but could not resolve to IP\n",
                 call, (char *)data.data);
      else
      {
         memcpy(&ia, hp->h_addr,sizeof(struct in_addr));
         strcpy(ip, inet_ntoa(ia));
      }
   }
   return true;

}

/* Process the configuration file */
static int read_config(char *cfgFile)
{
   short int valid_params = 11;
   short int params = 0;

   FILE *cnf = NULL;
   char inbuf[1024];
   char *p;
   char *ptr = NULL;
   unsigned short int i;
   unsigned short int j;

   cnf = fopen(cfgFile, "r");
   if (!cnf)
   {
      traceit("Failed to open file %s\n", cfgFile);
      return 1;
   }

   traceit("Reading file %s\n", cfgFile);
   while (fgets(inbuf, 1020, cnf) != NULL)
   {
      if (strchr(inbuf, '#'))
         continue;

      p = strchr(inbuf, '\r');
      if (p)
         *p = '\0';
      p = strchr(inbuf, '\n');
      if (p)
         *p = '\0';

      p = strchr(inbuf, '=');
      if (!p)
         continue;
      *p = '\0';

      if (strcmp(inbuf,"OWNER") == 0)
      {
          memset(OWNER,' ', sizeof(OWNER));
          OWNER[OWNER_SIZE] = '\0';

          ptr = strchr(p + 1, ' ');
          if (ptr)
             *ptr = '\0';
          
          if (strlen(p + 1) != OWNER_SIZE - 2)
             traceit("OWNER value %s invalid, length must be exactly %d\n", 
                      p + 1, OWNER_SIZE - 2);
          else
          {
             memcpy(OWNER, p + 1, OWNER_SIZE - 2);

             for (i = 0; i < strlen(OWNER); i++)
                OWNER[i] = toupper(OWNER[i]);

             if ((memcmp(OWNER, "XRF", 3) != 0) ||
                 !isdigit(OWNER[3]) ||
                 !isdigit(OWNER[4]) ||
                 !isdigit(OWNER[5]))
                traceit("Reflector names must be XRFzzz where zzz are digits from 1 thru 9\n");
             else
             {
                traceit("OWNER=%s\n",OWNER);
                params ++;
             }
          }
      }
      else
      if (strcmp(inbuf,"ADMIN") == 0)
      {
          memset(ADMIN,' ', sizeof(ADMIN));
          ADMIN[CALL_SIZE] = '\0';

          /* no spaces after the equal sign */
          if (p[1] == ' ')
             traceit("ADMIN: no spaces after the equal sign\n");
          else
          {
             /* take up to 8 characters, throw away the rest */
             p[CALL_SIZE + 1] = '\0';

             /* valid length? */
             if ((strlen(p + 1) < 3) || (strlen(p + 1) > CALL_SIZE))
                traceit("ADMIN value [%s] invalid\n", p + 1);
             else
             {
                memcpy(ADMIN, p + 1, strlen(p + 1));

                /* uppercase it */
                for (j = 0; j < CALL_SIZE; j++)
                   ADMIN[j] = toupper(ADMIN[j]);

                traceit("ADMIN=[%s]\n",ADMIN);
                params ++;
             }
          }
      }
      else
      if (strcmp(inbuf,"LISTEN_IP") == 0)
      {
         ptr = strchr(p + 1, ' ');
         if (ptr)
            *ptr = '\0';

         if (strlen(p + 1) < 1)
            traceit("LISTEN_IP value %s invalid\n", p + 1);
         else
         {
            memset(LISTEN_IP, '\0', sizeof(LISTEN_IP));
            strncpy(LISTEN_IP, p + 1, IP_SIZE);
            traceit("LISTEN_IP=%s\n",LISTEN_IP);
            params ++;
         }
      }
      else
      if (strcmp(inbuf,"LISTEN_PORT") == 0)
      {
         LISTEN_PORT = atoi(p + 1);
         traceit("LISTEN_PORT=%d\n",LISTEN_PORT);
         params ++;
      }
      else
      if (strcmp(inbuf,"COMMAND_PORT") == 0)
      {
         COMMAND_PORT = atoi(p + 1);
         traceit("COMMAND_PORT=%d\n",COMMAND_PORT);
         params ++;
      }
      else
      if (strcmp(inbuf,"MAX_USERS") == 0)
      {
         MAX_USERS = atoi(p + 1);
         traceit("MAX_USERS=%d\n",MAX_USERS);
         params ++;
      }
      else
      if (strcmp(inbuf,"MAX_OTHER_USERS") == 0)
      {
         MAX_OTHER_USERS = atoi(p + 1); 
         traceit("MAX_OTHER_USERS=%d\n",MAX_OTHER_USERS);
         params ++; 
      }
      else
      if (strcmp(inbuf,"STATUS_FILE") == 0)
      {
         memset(STATUS_FILE, '\0', sizeof(STATUS_FILE));
         strncpy(STATUS_FILE, p + 1,FILENAME_MAX);
         traceit("STATUS_FILE=%s\n",STATUS_FILE);
         params ++;
      }
      else
      if (strcmp(inbuf,"USERS") == 0)
      {
         memset(USERS, '\0', sizeof(USERS));
         strncpy(USERS, p + 1,FILENAME_MAX);
         traceit("USERS=%s\n",USERS);
         params ++;
      }
      else
      if (strcmp(inbuf,"IP_CHECK") == 0)
      {
         if (*(p + 1) == 'Y')
            IP_CHECK = true;
         else
            IP_CHECK = false;
         traceit("IP_CHECK=%c\n", *(p + 1));
         params ++;
      }
      else
      if (strcmp(inbuf,"QSO_DETAILS") == 0)
      {
         if (*(p + 1) == 'Y')
            QSO_DETAILS = true;
         else
            QSO_DETAILS = false;
         traceit("QSO_DETAILS=%c\n", *(p + 1));
         params ++;
      }
   }
   fclose(cnf);

   if (params != valid_params)
   {
      traceit("Configuration file %s invalid\n",cfgFile);
      return 1;
   }      

   if (COMMAND_PORT == LISTEN_PORT)
   {
      traceit("Error: COMMAND_PORT must be different from LISTEN_PORT\n");
      return 1;
   }
   return 0;
}

/* Send heartbeat to repeaters, ... */
static void check_heartbeat()
{
   a_user_list_type::iterator pos;
   struct a_user *a_user_ptr;
   bool a_station_was_dropped = false;

   for (pos = a_user_list.begin(); pos != a_user_list.end(); pos++) 
   {
      a_user_ptr = (struct a_user *)pos->second;

      sendto(srv_sock, OWNER, strlen(OWNER) + 1,
             0,(struct sockaddr *)&(a_user_ptr->sin),
             sizeof(struct sockaddr_in));

      if (a_user_ptr->countdown >= 0)
         a_user_ptr->countdown --;

      if (a_user_ptr->countdown < 0)
      {
         traceit("call=%s timeout, removing %s, users=%d\n",
                 a_user_ptr->call,
                 pos->first.c_str(),
                 a_user_list.size() - 1 + inbound_list.size());

         a_station_was_dropped = true;

         free(pos->second);
         pos->second = NULL;
         a_user_list.erase(pos);

      }
   }

   if (a_station_was_dropped)
      print_links_file();

   return;
}

/* Relay packets to repeaters, ... */
/* 
      Last paramter from_ref says that the origin is from a system which 
      re-transmits headers
*/
static void send_a_pkt_to_all(uint32_t s_addr, int recvlen, bool from_ref)
{
   short i, k;

   a_user_list_type::iterator pos;
   struct a_user *a_user_ptr;

   if (recvlen == 56)
   {
      if (readBuffer[25] == 'A')
         k = 0;
      else
      if (readBuffer[25] == 'B')
         k = 1;
      else
      if (readBuffer[25] == 'C')
         k = 2;
      else
      if (readBuffer[25] == 'D')
         k = 3;
      else
         k = -1;

      if (k >= 0)
      {
         if (temp_x[k].last_time == 0)  // is it free ?
         {
            memcpy(temp_x[k].hdr, readBuffer, 56);
            temp_x[k].last_time = time(NULL);
         }
      }
   }

   for (pos = a_user_list.begin(); pos != a_user_list.end(); pos++)
   {
      a_user_ptr = (struct a_user *)pos->second;

      /* do not send data to the station that is transmitting right now */
      if (s_addr != a_user_ptr->sin.sin_addr.s_addr)
      {
         /* If the remote station is NOT a repeater, send the data as it is */
         if ( !(a_user_ptr->is_repeater) )
         {
            /* do we need to re-transmit ? */
            if ((recvlen == 27) && (!from_ref))
            {
               if ((readBuffer[24] == 0x55) && (readBuffer[25] == 0x2d) && (readBuffer[26] == 0x16))
               {               
                  for (i = 0; i < 4; i++)
                  {
                     if ((readBuffer[12] == temp_x[i].hdr[12]) &&
                         (readBuffer[13] == temp_x[i].hdr[13]))
                     {
                        sendto(srv_sock, (char *)temp_x[i].hdr, 56, 0,
                               (struct sockaddr *)&(a_user_ptr->sin), sizeof(struct sockaddr_in));
                        break;
                     }
                  }
               }
            }

            sendto(srv_sock, (char *)readBuffer, recvlen,
                   0,(struct sockaddr *)&(a_user_ptr->sin),
                   sizeof(struct sockaddr_in));
         }
         else
         {
            /*  The remote system is a REPEATER. */
            if (recvlen == 56)  
            {
               if (k >= 0)
               {
                  /* send header ONLY if module is linked */
                  if ((a_user_ptr->rpt_mods[k][0] != '\0') || 
                      (a_user_ptr->rpt_mods[k][1] != '\0') ||
                      (a_user_ptr->rpt_mods[k][2] != '\0') || 
                      (a_user_ptr->rpt_mods[k][3] != '\0'))
                     sendto(srv_sock, (char *)readBuffer, recvlen,
                            0,(struct sockaddr *)&(a_user_ptr->sin),
                            sizeof(struct sockaddr_in));          
               }
            }
            else
            {
               if ( !(a_user_ptr->is_xrf) && !from_ref)
               {
                  /* should we re-transmit header ? */
                  if ((readBuffer[24] == 0x55) && (readBuffer[25] == 0x2d) && (readBuffer[26] == 0x16))
                  {
                     for (i = 0; i < 4; i++)
                     {
                        if ((readBuffer[12] == temp_x[i].hdr[12]) &&
                            (readBuffer[13] == temp_x[i].hdr[13]) &&
                            ((a_user_ptr->rpt_mods[i][0] != '\0') || /* module is linked */ 
                             (a_user_ptr->rpt_mods[i][1] != '\0') ||
                             (a_user_ptr->rpt_mods[i][2] != '\0') ||
                             (a_user_ptr->rpt_mods[i][3] != '\0')))
                        {
                           sendto(srv_sock, (char *)temp_x[i].hdr, 56, 0,
                                  (struct sockaddr *)&(a_user_ptr->sin), sizeof(struct sockaddr_in));
                           break;
                        }
                     }
                  }
               }

               for (i = 0; i < 4; i++)
               { 
                  if ((readBuffer[12] == temp_x[i].hdr[12]) &&
                      (readBuffer[13] == temp_x[i].hdr[13]) &&
                      ((a_user_ptr->rpt_mods[i][0] != '\0') || /* module is linked */
                       (a_user_ptr->rpt_mods[i][1] != '\0') ||
                       (a_user_ptr->rpt_mods[i][2] != '\0') ||
                       (a_user_ptr->rpt_mods[i][3] != '\0')))
                  {
                     sendto(srv_sock, (char *)readBuffer, recvlen,
                            0,(struct sockaddr *)&(a_user_ptr->sin),
                            sizeof(struct sockaddr_in));
                     break;
                  }
               }
            }  
         }
      }
   }

   if (recvlen == 27)
   {
      for (i = 0; i < 4; i++)
      {
         if ((readBuffer[12] == temp_x[i].hdr[12]) &&
             (readBuffer[13] == temp_x[i].hdr[13]))
         {
            temp_x[i].last_time = time(NULL);

            if ((readBuffer[14] & 0x40) != 0)
            {
               memset(temp_x[i].hdr, 0, 56);
               temp_x[i].last_time = 0;
            }

            break;
         }
      }
   }

   return;
}

static void print_version()
{
   char buf[24];

   snprintf(buf, 23, "%s\n", VERSION);
   sendto(cmd_sock, buf, strlen(buf),
          0, (struct sockaddr *)&fromCmd,
          sizeof(struct sockaddr_in));
}

/* Print connected users */
static void print_users()
{
   a_user_list_type::iterator pos;
   struct a_user *a_user_ptr;

   inbound_type::iterator pos2;
   inbound *inbound_ptr;

   struct tm tm;

   char buf[124];
   for (pos = a_user_list.begin(); pos != a_user_list.end(); pos++)
   {
      a_user_ptr = (struct a_user *)pos->second; 
      localtime_r(&(a_user_ptr->connect_time),&tm);
      snprintf(buf, 123, "%s,%s,%s,%02d%02d%02d,%02d:%02d:%02d,%s\n", 
               a_user_ptr->call,
               pos->first.c_str(),
               a_user_ptr->is_repeater?"REPEATER":"DONGLE",
               tm.tm_mon+1,tm.tm_mday,tm.tm_year % 100,
               tm.tm_hour,tm.tm_min,tm.tm_sec,
               a_user_ptr->isMute?"Muted":"notMuted");
     
      sendto(cmd_sock, buf, strlen(buf), 
             0, (struct sockaddr *)&fromCmd,
             sizeof(struct sockaddr_in));
   }

   for (pos2 = inbound_list.begin(); pos2 != inbound_list.end(); pos2++)
   {
      inbound_ptr = (inbound *)pos2->second;
      localtime_r(&(inbound_ptr->connect_time),&tm);
      snprintf(buf, 123, "%s,%s,%s(%.8s),%02d%02d%02d,%02d:%02d:%02d,%s\n",
               inbound_ptr->call,
               pos2->first.c_str(),
               inbound_ptr->is_ref?"REPEATER":"DONGLE",
               inbound_ptr->serial,
               tm.tm_mon+1,tm.tm_mday,tm.tm_year % 100,
               tm.tm_hour,tm.tm_min,tm.tm_sec,
               inbound_ptr->isMute?"Muted":"notMuted");
    
      sendto(cmd_sock, buf, strlen(buf),
             0, (struct sockaddr *)&fromCmd,
             sizeof(struct sockaddr_in));
   }
   return;
}

/* Mute or unmute all users */
static void mute_users(bool mute)
{
   a_user_list_type::iterator pos;
   struct a_user *a_user_ptr;

   inbound_type::iterator pos2;
   inbound *inbound_ptr;

   for (pos = a_user_list.begin(); pos != a_user_list.end(); pos++)
   {
      a_user_ptr = (struct a_user *)pos->second;
      a_user_ptr->isMute = mute;
   }

   for (pos2 = inbound_list.begin(); pos2 != inbound_list.end(); pos2++)
   {
      inbound_ptr = (inbound *)pos2->second;
      inbound_ptr->isMute = mute;
   }
   return;
}


/* mute or unmute a user by callsign */
static bool mute_call(char *call, bool mute)
{
   bool found = false;
   a_user_list_type::iterator pos;
   struct a_user *a_user_ptr;

   inbound_type::iterator pos2;
   inbound *inbound_ptr;

   for (pos = a_user_list.begin(); pos != a_user_list.end(); pos++)
   {
      a_user_ptr = (struct a_user *)pos->second;
      if (strcmp(a_user_ptr->call, call) == 0)
      {
         found = true;
         a_user_ptr->isMute = mute;
      }
   }

   for (pos2 = inbound_list.begin(); pos2 != inbound_list.end(); pos2++)
   {
      inbound_ptr = (inbound *)pos2->second;
      if (strcmp(inbound_ptr->call, call) == 0)
      {
         found = true;
         inbound_ptr->isMute = mute;
      }
   }

   return found;
}

/* print the links on the screen */
static void print_links_screen()
{
   char buf[128];
   struct tm tm;

   inbound_type::iterator inbound_pos;
   inbound *inbound_ptr;

   a_user_list_type::iterator pos;
   struct a_user *a_user_ptr;
   short int i, j;
   char from_mod = ' ';

   for (pos = a_user_list.begin(); pos != a_user_list.end(); pos++)
   {
      a_user_ptr = (struct a_user *)pos->second;
      for (i = 0; i < 5; i++)
      {
         for (j = 0; j < 4; j++)
         {
            if (a_user_ptr->rpt_mods[i][j] != '\0')
            {
               if (i == 0)
                  from_mod = 'A';
               else
               if (i == 1)
                  from_mod = 'B';
               else
               if (i == 2)
                  from_mod = 'C';
               else
               if (i == 3)
                  from_mod = 'D';
               else
               if (i == 4)
                  from_mod = 'E';

               localtime_r(&(a_user_ptr->link_time[i][j]),&tm);
               snprintf(buf, 127, "%c,%s,%c,%s,%02d%02d%02d,%02d:%02d:%02d\n",
                        from_mod,
                        a_user_ptr->call,
                        a_user_ptr->rpt_mods[i][j],
                        pos->first.c_str(),
                        tm.tm_mon+1,tm.tm_mday,tm.tm_year % 100,
                        tm.tm_hour,tm.tm_min,tm.tm_sec);

               sendto(cmd_sock, buf, strlen(buf),
                      0, (struct sockaddr *)&fromCmd,
                      sizeof(struct sockaddr_in));
            }
         }
      }
   }

   for (inbound_pos = inbound_list.begin(); inbound_pos != inbound_list.end(); inbound_pos++)
   {
      inbound_ptr = (inbound *)inbound_pos->second;
      for (i = 0; i < 5; i++)
      {
         if (inbound_ptr->links[i] != ' ')
         {
            if (i == 0)
               from_mod = 'A';
            else
            if (i == 1)
               from_mod = 'B';
            else
            if (i == 2)
               from_mod = 'C';
            else
            if (i == 3)
               from_mod = 'D';
            else
               from_mod = 'E';

            localtime_r(&(inbound_ptr->connect_time), &tm);
            snprintf(buf, 127, "%c,%s,%c,%s,%02d%02d%02d,%02d:%02d:%02d\n",
                  from_mod,
                  inbound_ptr->call,
                  inbound_ptr->links[i],
                  inbound_pos->first.c_str(),
                  tm.tm_mon+1,tm.tm_mday,tm.tm_year % 100,
                  tm.tm_hour,tm.tm_min,tm.tm_sec);

            sendto(cmd_sock, buf, strlen(buf),
                   0, (struct sockaddr *)&fromCmd,
                   sizeof(struct sockaddr_in));
         }
      }
   }
}

/* Print links into the STATUS file */
static void print_links_file()
{
   inbound_type::iterator inbound_pos;
   inbound *inbound_ptr;

   a_user_list_type::iterator pos;
   struct a_user *a_user_ptr;
   struct tm tm;
   short int i, j;
   char from_mod = ' ';

   statusfp = fopen(STATUS_FILE, "w");
   if (!statusfp)
         traceit("Failed to create status file %s\n", STATUS_FILE);
   else
   {
      setvbuf(statusfp, (char *)NULL, _IOLBF, 0);
      for (pos = a_user_list.begin(); pos != a_user_list.end(); pos++)
      {
         a_user_ptr = (struct a_user *)pos->second;
         for (i = 0; i < 5; i++)
         {
            for (j = 0; j < 4; j++)
            {
               if (a_user_ptr->rpt_mods[i][j] != '\0')
               {
                  if (i == 0)
                     from_mod = 'A';
                  else
                  if (i == 1)
                     from_mod = 'B';
                  else
                  if (i == 2)
                     from_mod = 'C';
                  else
                  if (i == 3)
                     from_mod = 'D';
                  else
                     from_mod = 'E';

               localtime_r(&(a_user_ptr->link_time[i][j]),&tm);
               fprintf(statusfp, "%c,%s,%c,%s,%02d%02d%02d,%02d:%02d:%02d\n",
                       from_mod,
                       a_user_ptr->call, 
                       a_user_ptr->rpt_mods[i][j],
                       pos->first.c_str(),
                       tm.tm_mon+1,tm.tm_mday,tm.tm_year % 100,
                       tm.tm_hour,tm.tm_min,tm.tm_sec);
               }
            }
         }
      }

      for (inbound_pos = inbound_list.begin(); inbound_pos != inbound_list.end(); inbound_pos++)
      {
         inbound_ptr = (inbound *)inbound_pos->second;
         for (i = 0; i < 5; i++)
         {
            if (inbound_ptr->links[i] != ' ')
            {
               if (i == 0)
                  from_mod = 'A';
               else
               if (i == 1)
                  from_mod = 'B';
               else
               if (i == 2)
                  from_mod = 'C';
               else
               if (i == 3)
                  from_mod = 'D';
               else
                  from_mod = 'E';

               localtime_r(&(inbound_ptr->connect_time), &tm);
               fprintf(statusfp, "%c,%s,%c,%s,%02d%02d%02d,%02d:%02d:%02d\n",
                  from_mod,
                  inbound_ptr->call,
                  inbound_ptr->links[i],
                  inbound_pos->first.c_str(),
                  tm.tm_mon+1,tm.tm_mday,tm.tm_year % 100,
                  tm.tm_hour,tm.tm_min,tm.tm_sec);
            }
         }
      }
      fclose(statusfp);
   }
}

/* print blocked users */
static void print_blocks()
{
   blocks_type::iterator pos;
   char buf[32];

   for (pos = blocks.begin(); pos != blocks.end(); pos++)
   {
      snprintf(buf, 31, "[%s]\n", pos->c_str()); 
      sendto(cmd_sock, buf, strlen(buf),
             0, (struct sockaddr *)&fromCmd,
             sizeof(struct sockaddr_in));
   }
}

/* signal catching function */
static void sigCatch(int signum)
{
   /* do NOT do any serious work here */
   /* Just get out of its processing loop */

      if ((signum == SIGTERM) ||
       (signum == SIGINT))
         keep_running = false;
   return;
}

/* Open server socket for dvap, dvtool ... */
static int ref_open()
{
   struct sockaddr_in sin;

   ref_sock = socket(PF_INET,SOCK_DGRAM,0);
   if (ref_sock == -1)
   {
      traceit("Failed to create ref socket,errno=%d\n",errno);
      return 1;
   }
   memset(&sin,0,sizeof(struct sockaddr_in));
   sin.sin_family = AF_INET;
   sin.sin_port = htons(20001);
   sin.sin_addr.s_addr = inet_addr(LISTEN_IP);

   if (bind(ref_sock,(struct sockaddr *)&sin,sizeof(struct sockaddr_in)) != 0)
   {
      traceit("Failed to bind ref socket on IP %s, port 20001, errno=%d\n",
              LISTEN_IP, errno);
      close(ref_sock); ref_sock = -1;
      return 1;
   }
   fcntl(ref_sock,F_SETFL,O_NONBLOCK);

   return 0;
}

/* Open server socket for connected stations */
static int srv_open()
{
   struct sockaddr_in sin;

   srv_sock = socket(PF_INET,SOCK_DGRAM,0);
   if (srv_sock == -1)
   {
      traceit("Failed to create server socket,errno=%d\n",errno);
      return 1;
   }
   memset(&sin,0,sizeof(struct sockaddr_in));
   sin.sin_family = AF_INET;
   sin.sin_port = htons(LISTEN_PORT);
   sin.sin_addr.s_addr = inet_addr(LISTEN_IP);

   if (bind(srv_sock,(struct sockaddr *)&sin,sizeof(struct sockaddr_in)) != 0)
   {
      traceit("Failed to bind server socket on IP %s, port %d, errno=%d\n",
              LISTEN_IP, LISTEN_PORT,errno);
      close(srv_sock); srv_sock = -1;
      return 1;
   }
   fcntl(srv_sock,F_SETFL,O_NONBLOCK); 

   return 0;
}

/* 
   Open command socket for handling commands from the shell.
   Use netcat as a client to send commands to this reflector
*/
static int cmd_open()
{
   struct sockaddr_in sin;

   cmd_sock = socket(PF_INET,SOCK_DGRAM,0);
   if (cmd_sock == -1)
   {
      traceit("Failed to create cmd socket,errno=%d\n",errno);
      return 1;
   }
   memset(&sin,0,sizeof(struct sockaddr_in));
   sin.sin_family = AF_INET;
   sin.sin_port = htons(COMMAND_PORT);
   sin.sin_addr.s_addr = inet_addr(LISTEN_IP);

   if (bind(cmd_sock,(struct sockaddr *)&sin,sizeof(struct sockaddr_in)) != 0)
   {
      traceit("Failed to bind cmd socket on IP %s, port %d, errno=%d\n",
              LISTEN_IP, COMMAND_PORT,errno);
      close(cmd_sock); cmd_sock = -1;
      return 1;
   }
   fcntl(cmd_sock,F_SETFL,O_NONBLOCK);

   return 0;
}

/* Process the shell command */
static void handle_cmd(char *buf)
{
   char *p = NULL;
   char call[CALL_SIZE + 1];
   char cmd[4];
   unsigned short i;
   char nak[5];

   char *ptr_cmd = NULL;
   char *ptr_call = NULL;
   const char *delim = " ";

   blocks_type::iterator pos;

   int rc = -1;

   nak[0] = '\0';

   p = strchr(buf, '\r');
   if (p)
      *p = '\0';

   p = strchr(buf, '\n');
   if (p)
      *p = '\0';

   if (strlen(buf) < 2)
      return;

   traceit("Received command [%s] from [%s]\n", buf, inet_ntoa(fromCmd.sin_addr));

   if (strcmp(buf, "sh") == 0)  /* shutdown reflector */
      keep_running = false;
   else
   if (strcmp(buf, "mu") == 0) /* mute users */
      mute_users(true);
   else
   if (strcmp(buf, "uu") == 0) /* un-mute users */
      mute_users(false);
   else
   if (strcmp(buf, "qsoy") == 0)  /* QSO_DETAILS yes */
      QSO_DETAILS = true;
   else
   if (strcmp(buf, "qson") == 0)
      QSO_DETAILS = false;       /* QSO_DETAILS no */
   else
   if (strcmp(buf, "ipcy") == 0)  /* IP_CHECK yes */
      IP_CHECK = true;
   else
   if (strcmp(buf, "ipcn") == 0) /* IP_CHECK no */
      IP_CHECK = false;       
   else
   if (strcmp(buf, "pu") == 0)   /* print users */
      print_users();
   else
   if (strcmp(buf, "pv") == 0)   /* print version */
      print_version();
   else
   if (strcmp(buf, "pb") == 0)   /* print blocked users */
      print_blocks();
   else
   if (strcmp(buf, "pl") == 0)   /* print links */
      print_links_screen();
   else
   {
      ptr_cmd = strtok(buf, delim);
      if (ptr_cmd)
         ptr_call = strtok(NULL, delim);

      if (!ptr_cmd || !ptr_call)
      {
         traceit("Missing command parameters\n");
         strcpy(nak, "NAK\n");
      }
      else
      if (strcmp(ptr_cmd, "upd") == 0)
      {
         if (dbp != NULL)
         {
            dbp->close(dbp, 0);
            traceit("Closing database %s\n",USERS);
         }
         rc = rename(ptr_call, USERS);
         if (rc != 0)
            traceit("Failed to move %s to %s, error=%d\n", ptr_call, USERS, errno);
         else
            traceit("Moved %s over %s, OK\n", ptr_call, USERS);

         rc = open_users(USERS);
         if (rc != 0)
         {
            traceit("Failed to re-open %s\n", USERS);

            /* Empty out the status file */
            statusfp = fopen(STATUS_FILE, "w");
            if (statusfp)
               fclose(statusfp);
            else
               traceit("Failed to empty out status file %s\n", STATUS_FILE);

            traceit("dxrfd exiting\n");
            fclose(logfp);
            exit(1);
         }
      }
      else
      if (strlen(ptr_cmd) > 3)
      {
         traceit("Invalid command length\n");
         strcpy(nak, "NAK\n");
      }
      else
      {
         strcpy(cmd, ptr_cmd);
         if (strlen(ptr_call) <= CALL_SIZE)
         {
            strcpy(call, ptr_call);

            if (strlen(call) < CALL_SIZE)
            {
               for (i = strlen(call); i < CALL_SIZE; i++)
                  call[i] = ' ';
               call[CALL_SIZE] = '\0';
            }

            if (strcmp(cmd, "ab") == 0)   /* ab <CALLSIGN>    this will block the callsign */
            {
               pos = blocks.find(call);
               if (pos != blocks.end())
               {
                  traceit("Call %s already blocked\n", call);
                  strcpy(nak, "NAK\n");
               }
               else
               if (blocks.insert(call).second)
                  traceit("call %s is now blocked\n", call);
               else
               {
                  traceit("Failed to block call %s\n",call);
                  strcpy(nak, "NAK\n");
               }
            }
            else
            if (strcmp(cmd, "rb") == 0)   /* rb  <CALLSIGN>     this will remove the block on the callsign */
            {
               if (blocks.erase(call) > 0)
                  traceit("call %s is now unblocked\n", call);
               else
               {
                  traceit("Could not find blocked call %s\n", call);
                  strcpy(nak, "NAK\n");
               }
            }
            else
            if (strcmp(cmd, "mc") == 0) /* mute a user by callsign */
            {
               if (!mute_call(call, true))
                  strcpy(nak, "NAK\n");
            }
            else
            if (strcmp(cmd, "uc") == 0) /* un-mute a user by callsign */
            {
               if (!mute_call(call, false))
                  strcpy(nak, "NAK\n");
            }
            else
            if (strcmp(cmd, "lrf") == 0)  /* link to ref */
            {
               rc = link_to_ref(call);
               if (rc != 0)
                  strcpy(nak, "NAK\n");
            }
            else
            {
               traceit("Unknown command or bad parameter\n", cmd);
               strcpy(nak, "NAK\n");
            }
         }
      }
   }

   if (nak[0] == '\0')
      strcpy(nak, "OK\n");

   sendto(cmd_sock,nak,4,
          0, (struct sockaddr *)&fromCmd,
          sizeof(struct sockaddr_in));
   return;
}

/* Reflector is running here */
static void runit()
{
   short i,j,k,n;
   int recvlen;
   int recvlen2;
   int recvlen3;
   socklen_t fromlen;
   char *p = NULL;

   bool allowed_to_connect = true;
   bool deleted = false;
   char payload[MAXHOSTNAMELEN + 1];

   char a_call[CALL_SIZE + 1];
   char an_ip[IP_SIZE + 1];

   struct a_user *a_user_ptr;
   a_user_list_type::iterator user_pos;
   pair<a_user_list_type::iterator,bool> user_insert_pair;
   blocks_type::iterator block_pos;

   inbound *inbound_ptr;
   inbound_type::iterator inbound_pos;
   inbound *inbound_ptr2;
   inbound_type::iterator inbound_pos2;
   pair<inbound_type::iterator,bool> inbound_insert_pair;
   bool found = false;

   char source_mod = ' ';
   int max_nfds = 0;

   char tmp1[CALL_SIZE + 1];
   char tmp2[32]; // 8 for rpt1 + 24 for time_t in string format
   lh_type::iterator lh_pos;
   dt_lh_type::iterator dt_lh_pos;
   dt_lh_type::reverse_iterator r_dt_lh_pos;

   int rc;

   char reply_to_xrf[11]; 

   /* faster select */
   if (srv_sock > max_nfds)
      max_nfds = srv_sock;
   if (cmd_sock > max_nfds)
      max_nfds = cmd_sock;
   if (ref_sock > max_nfds)
      max_nfds = ref_sock;

   traceit("srv=%d, cmd=%d, ref=%d, MAX+1=%d\n", 
            srv_sock,cmd_sock,ref_sock, max_nfds + 1);

   time(&HBinterStart);
   time(&inboundStart);

   time(&check_rcd_time);

   while (keep_running)
   {
      /* send heartbeat to connected dvap, dvtool ... */
      time(&tNow);
      if ((tNow - inboundStart) > 0)
      {
         send_heartbeat();
         inboundStart = tNow;
      } 

      /* send a heartbeat to linked gateways */
      time(&tNow);
      if ((tNow - HBinterStart) > 0)
      {
         check_heartbeat();
         HBinterStart = tNow;
      }

      time(&tNow);
      for (i = 0; i < 4; i++)
      {
         if (temp_x[i].last_time != 0)
         {
            if ((tNow - temp_x[i].last_time) > 2)
            {
               memset(temp_x[i].hdr, 0, 56);
               temp_x[i].last_time = 0;
               traceit("Inactivity...initialized temp_x[%d]\n", i);
            }
         }

         if (temp_r[i].last_time != 0)
         {
            if ((tNow - temp_r[i].last_time) > 2)
            {
               memset(temp_r[i].hdr, 0, 58);
               temp_r[i].last_time = 0;
               traceit("Inactivity...initialized temp_r[%d]\n", i);
            }
         }
      }

      if ((tNow - check_rcd_time) > 3)
      {
         for (rcd_pos = rcd_list.begin(); rcd_pos != rcd_list.end(); rcd_pos ++)
         {
            an_rcd = (struct rcd *)rcd_pos->second;
            if (!an_rcd->locked)
            {
               if ((tNow - an_rcd->ts) > 1)
               {
                  if (an_rcd->recvlen == 56)
                     traceit("Removing playback streamid %d,%d\n", an_rcd->data[0][12], an_rcd->data[0][13]);
                  else
                     traceit("Removing playback streamid %d,%d\n", an_rcd->data[0][14], an_rcd->data[0][15]);
                  free(rcd_pos->second);
                  rcd_pos->second = NULL;
                  rcd_list.erase(rcd_pos);
               }
            }
         } 
         check_rcd_time = tNow;
      }

      /* wait 20 ms max */
      FD_ZERO(&fdset);
      FD_SET(srv_sock,&fdset);
      FD_SET(cmd_sock,&fdset);
      FD_SET(ref_sock,&fdset);
      tv.tv_sec = 0;
      tv.tv_usec = 20000; /* 20 ms */
      (void)select(max_nfds + 1,&fdset,0,0,&tv);

      /* process shell commands */
      if (FD_ISSET(cmd_sock, &fdset))
      {
         fromlen = sizeof(struct sockaddr_in);
         recvlen2 = recvfrom(cmd_sock,(char *)readBuffer,READBUFFER_SIZE,
                            0,(struct sockaddr *)&fromCmd,&fromlen);
         if (recvlen2 > 0)
         {
            readBuffer[recvlen2 - 1] = '\0';
            handle_cmd((char *)readBuffer);
         }

         FD_CLR (cmd_sock,&fdset);
      }

      /*  process input from dvap, dvtool */
      if (FD_ISSET(ref_sock, &fdset))
      {
         fromlen = sizeof(struct sockaddr_in);
         recvlen3 = recvfrom(ref_sock,(char *)refbuf,READBUFFER_SIZE,
                            0,(struct sockaddr *)&fromInbound,&fromlen);

         strncpy(an_ip, inet_ntoa(fromInbound.sin_addr),IP_SIZE);
         an_ip[IP_SIZE] = '\0';

         /* LH */
         if ((recvlen3 == 4) &&
             (refbuf[0] == 4) &&
             (refbuf[1] == 192) &&
             (refbuf[2] == 7) &&
             (refbuf[3] == 0))
         {
            inbound_pos = inbound_list.find(an_ip);
            if (inbound_pos != inbound_list.end())
            {
               inbound_ptr = (inbound *)inbound_pos->second;
               // traceit("Remote station %s %s requested LH list\n", inbound_ptr->call, an_ip);
            }

            unsigned short j_idx = 0;
            unsigned short k_idx = 0;
            unsigned char tmp[2];

            /* header is 10 bytes */

            /* reply type */
            refbuf[2] = 7;
            refbuf[3] = 0;

            /* it looks like time_t here */
            time(&tNow);
            memcpy((char *)refbuf + 6, (char *)&tNow, sizeof(time_t));

            for (r_dt_lh_pos = dt_lh_list.rbegin(); r_dt_lh_pos != dt_lh_list.rend();  r_dt_lh_pos++)
            {
               /* each entry has 24 bytes */

               /* start at position 10 to bypass the header */
               strcpy((char *)refbuf + 10 + (24 * j_idx), r_dt_lh_pos->second.c_str());
               p = strchr((char *)r_dt_lh_pos->first.c_str(), '=');
               if (p)
               {
                  memcpy((char *)refbuf + 18 + (24 * j_idx), p + 1, 8);
                  *p = '\0';
                  tNow = atol(r_dt_lh_pos->first.c_str());
                  *p = '=';
                  memcpy((char *)refbuf + 26 + (24 * j_idx), &tNow, sizeof(time_t));
               }
               else
               {
                  memcpy((char *)refbuf + 18 + (24 * j_idx), "ERROR   ", 8);
                  time(&tNow);
                  memcpy((char *)refbuf + 26 + (24 * j_idx), &tNow, sizeof(time_t));
               }

               refbuf[30 + (24 * j_idx)] = 0;
               refbuf[31 + (24 * j_idx)] = 0;
               refbuf[32 + (24 * j_idx)] = 0;
               refbuf[33 + (24 * j_idx)] = 0;

               j_idx++;

               /* process 39 entries at a time */
               if (j_idx == 39)
               {
                  /* 39 * 24 = 936 + 10 header = 946 */
                  refbuf[0] = 0xb2;
                  refbuf[1] = 0xc3;

                  /* 39 entries */
                  refbuf[4] = 0x27;
                  refbuf[5] = 0x00;

                  sendto(ref_sock,(char *)refbuf,946,0,
                         (struct sockaddr *)&fromInbound,
                         sizeof(struct sockaddr_in));

                  j_idx = 0;
               }
            }

            if (j_idx != 0)
            {
               k_idx = 10 + (j_idx * 24);
               memcpy(tmp, (char *)&k_idx, 2);
               refbuf[0] = tmp[0];
               refbuf[1] = tmp[1] | 0xc0;

               memcpy(tmp, (char *)&j_idx, 2);
               refbuf[4] = tmp[0];
               refbuf[5] = tmp[1];

               sendto(ref_sock,(char *)refbuf, k_idx, 0,
                      (struct sockaddr *)&fromInbound,
                      sizeof(struct sockaddr_in));
            }
         }
         else 
         /* linked repeaters request */
         if ((recvlen3 == 4) &&
             (refbuf[0] == 4) &&
             (refbuf[1] == 192) &&
             (refbuf[2] == 5) &&
             (refbuf[3] == 0))
         {
            inbound_pos = inbound_list.find(an_ip);
            if (inbound_pos != inbound_list.end())
            {
               inbound_ptr = (inbound *)inbound_pos->second;
               // traceit("Remote station %s %s requested linked repeaters list\n", inbound_ptr->call, an_ip);
            }
          
            unsigned short i_idx = 0;
            unsigned short j_idx = 0;
            unsigned short k_idx = 0;
            short module_idx;
            unsigned char tmp[2];
            unsigned short total = 0;

            /* header is 8 bytes */

            /* reply type */
            refbuf[2] = 5;
            refbuf[3] = 1;

            /* Contains repeater list + users, but we will only select the repeaters */
            total =  a_user_list.size();
            memcpy(tmp, (char *)&total, 2);
            refbuf[6] = tmp[0];
            refbuf[7] = tmp[1];

            for (user_pos = a_user_list.begin(), i_idx = 0; user_pos != a_user_list.end();  user_pos++, i_idx++)
            {
               /* each entry has 20 bytes */
               a_user_ptr = (struct a_user *)user_pos->second;
               if (a_user_ptr->is_repeater)
               {
                  for (module_idx = 0; module_idx < 5; module_idx++)
                  { 
                     for (k = 0; k < 4; k++)
                     {
                        if (a_user_ptr->rpt_mods[module_idx][k] != '\0')
                        {
                           if (module_idx == 0)
                              refbuf[8 + (20 * j_idx)] = 'A';
                           else
                           if (module_idx == 1)
                              refbuf[8 + (20 * j_idx)] = 'B';
                           else
                           if (module_idx == 2)
                              refbuf[8 + (20 * j_idx)] = 'C';
                           else
                           if (module_idx == 3)
                              refbuf[8 + (20 * j_idx)] = 'D';
                           else
                           if (module_idx == 4)
                              refbuf[8 + (20 * j_idx)] = 'E';   
                            
                           strcpy((char *)refbuf + 9 + (20 * j_idx), a_user_ptr->call);
                           refbuf[16 + (20 * j_idx)] = a_user_ptr->rpt_mods[module_idx][k];

                           /* dont know what these 11 butes are */
                           refbuf[17 + (20 * j_idx)] = 0;
                           refbuf[18 + (20 * j_idx)] = 0;
                           refbuf[19 + (20 * j_idx)] = 0;
                           refbuf[20 + (20 * j_idx)] = 0x50;
                           refbuf[21 + (20 * j_idx)] = 0x04;
                           refbuf[22 + (20 * j_idx)] = 0x32;
                           refbuf[23 + (20 * j_idx)] = 0x4d;
                           refbuf[24 + (20 * j_idx)] = 0x9f;
                           refbuf[25 + (20 * j_idx)] = 0xdb;
                           refbuf[26 + (20 * j_idx)] = 0x0e;
                           refbuf[27 + (20 * j_idx)] = 0;

                           j_idx++;

                           if (j_idx == 39)
                           {
                              /* 20 bytes for each user, so 39 * 20 = 780 bytes + 8 bytes header = 788 */
                              refbuf[0] = 0x14;   
                              refbuf[1] = 0xc3;  

                              k_idx = i_idx - 38;
                              memcpy(tmp, (char *)&k_idx, 2);
                              refbuf[4] = tmp[0];
                              refbuf[5] = tmp[1];

                              sendto(ref_sock,(char *)refbuf,788,0,
                                  (struct sockaddr *)&fromInbound,
                                  sizeof(struct sockaddr_in));

                              j_idx = 0;
                           }
                        }
                     }
                  }
               }
            }

            if (j_idx != 0)
            {
               k_idx = 8 + (j_idx * 20);
               memcpy(tmp, (char *)&k_idx, 2);
               refbuf[0] = tmp[0];
               refbuf[1] = tmp[1] | 0xc0;

               if (i_idx > j_idx)
                  k_idx = i_idx - j_idx;
               else
                  k_idx = 0;

               mempcpy(tmp, (char *)&k_idx, 2);
               refbuf[4] = tmp[0];
               refbuf[5] = tmp[1];

               sendto(ref_sock,(char *)refbuf, 8 + (j_idx * 20), 0,
                      (struct sockaddr *)&fromInbound,
                      sizeof(struct sockaddr_in));
            }

            i_idx = 0;
            j_idx = 0;
            k_idx = 0;
            for (inbound_pos = inbound_list.begin(), i_idx = 0; inbound_pos != inbound_list.end();  inbound_pos++, i_idx++)
            {
               /* each entry has 20 bytes */
               inbound_ptr = (inbound *)inbound_pos->second;
               if (inbound_ptr->is_ref)
               {
                  for (module_idx = 0; module_idx < 5; module_idx++)
                  {
                     if (inbound_ptr->links[module_idx] != ' ')
                     {
                        if (module_idx == 0)
                           refbuf[8 + (20 * j_idx)] = 'A';
                        else
                        if (module_idx == 1)
                           refbuf[8 + (20 * j_idx)] = 'B';
                        else
                        if (module_idx == 2)
                           refbuf[8 + (20 * j_idx)] = 'C';
                        else
                        if (module_idx == 3)
                           refbuf[8 + (20 * j_idx)] = 'D';
                        else
                        if (module_idx == 4)
                           refbuf[8 + (20 * j_idx)] = 'E';
             
                        strcpy((char *)refbuf + 9 + (20 * j_idx), inbound_ptr->call);
                        refbuf[16 + (20 * j_idx)] = inbound_ptr->links[module_idx];

                        /* dont know what these 11 butes are */
                        refbuf[17 + (20 * j_idx)] = 0;
                        refbuf[18 + (20 * j_idx)] = 0;
                        refbuf[19 + (20 * j_idx)] = 0;
                        refbuf[20 + (20 * j_idx)] = 0x50;
                        refbuf[21 + (20 * j_idx)] = 0x04;
                        refbuf[22 + (20 * j_idx)] = 0x32;
                        refbuf[23 + (20 * j_idx)] = 0x4d;
                        refbuf[24 + (20 * j_idx)] = 0x9f;
                        refbuf[25 + (20 * j_idx)] = 0xdb;
                        refbuf[26 + (20 * j_idx)] = 0x0e;
                        refbuf[27 + (20 * j_idx)] = 0;

                        j_idx++;

                        if (j_idx == 39)
                        {
                           /* 20 bytes for each user, so 39 * 20 = 780 bytes + 8 bytes header = 788 */
                           refbuf[0] = 0x14;
                           refbuf[1] = 0xc3;

                           k_idx = i_idx - 38;
                           memcpy(tmp, (char *)&k_idx, 2);
                           refbuf[4] = tmp[0];
                           refbuf[5] = tmp[1];

                           sendto(ref_sock,(char *)refbuf,788,0,
                                  (struct sockaddr *)&fromInbound,
                                  sizeof(struct sockaddr_in));

                           j_idx = 0;
                        }
                     }
                  }
               }
            }

            if (j_idx != 0)
            {
               k_idx = 8 + (j_idx * 20);
               memcpy(tmp, (char *)&k_idx, 2);
               refbuf[0] = tmp[0];
               refbuf[1] = tmp[1] | 0xc0;

               if (i_idx > j_idx)
                  k_idx = i_idx - j_idx;
               else
                  k_idx = 0;

               mempcpy(tmp, (char *)&k_idx, 2);
               refbuf[4] = tmp[0];
               refbuf[5] = tmp[1];

               sendto(ref_sock,(char *)refbuf, 8 + (j_idx * 20), 0,
                      (struct sockaddr *)&fromInbound,
                      sizeof(struct sockaddr_in));
            }
         }
         else
         /* connected user list request */
         if ((recvlen3 == 4) &&
             (refbuf[0] == 4) &&
             (refbuf[1] == 192) &&
             (refbuf[2] == 6) &&
             (refbuf[3] == 0))
         {
            inbound_pos = inbound_list.find(an_ip);
            if (inbound_pos != inbound_list.end())
            {
               inbound_ptr = (inbound *)inbound_pos->second;
               // traceit("Remote station %s %s requested connected user list\n", inbound_ptr->call, an_ip);
            }

            unsigned short i_idx = 0;
            unsigned short j_idx = 0;
            unsigned short k_idx = 0;
            unsigned char tmp[2];
            unsigned short total = 0;
          
            /* header is 8 bytes */
 
            /* reply type */
            refbuf[2] = 6;
            refbuf[3] = 0;

            /* total connected users */
            total =  a_user_list.size() + inbound_list.size();
            memcpy(tmp, (char *)&total, 2);
            refbuf[6] = tmp[0];
            refbuf[7] = tmp[1];

            for (user_pos = a_user_list.begin(), i_idx = 0; user_pos != a_user_list.end();  user_pos++, i_idx++)
            {
               /* each entry has 20 bytes */
               a_user_ptr = (struct a_user *)user_pos->second;
               if (!a_user_ptr->is_repeater)
               {
                  refbuf[8 + (20 * j_idx)] = a_user_ptr->mod;
                  strcpy((char *)refbuf + 9 + (20 * j_idx), a_user_ptr->call);

                  /* dont know what these 11 butes are */
                  refbuf[17 + (20 * j_idx)] = 0;
                  /* refbuf[18 + (20 * j_idx)] = 0; */ refbuf[18 + (20 * j_idx)] = 'X'; 
                  refbuf[19 + (20 * j_idx)] = 0; 
                  refbuf[20 + (20 * j_idx)] = 0x0d;
                  refbuf[21 + (20 * j_idx)] = 0x4d;
                  refbuf[22 + (20 * j_idx)] = 0x37;
                  refbuf[23 + (20 * j_idx)] = 0x4d;
                  refbuf[24 + (20 * j_idx)] = 0x6f;
                  refbuf[25 + (20 * j_idx)] = 0x98;
                  refbuf[26 + (20 * j_idx)] = 0x04;
                  refbuf[27 + (20 * j_idx)] = 0;

                  j_idx++;

                  if (j_idx == 39)
                  {
                     /* 20 bytes for each user, so 39 * 20 = 780 bytes + 8 bytes header = 788 */
                     refbuf[0] = 0x14; 
                     refbuf[1] = 0xc3;
                 
                     k_idx = i_idx - 38;
                     memcpy(tmp, (char *)&k_idx, 2);
                     refbuf[4] = tmp[0];
                     refbuf[5] = tmp[1];         

                     sendto(ref_sock,(char *)refbuf,788,0,
                            (struct sockaddr *)&fromInbound,
                            sizeof(struct sockaddr_in));

                     j_idx = 0;
                  }
               }
            }

            if (j_idx != 0)
            {
               k_idx = 8 + (j_idx * 20);
               memcpy(tmp, (char *)&k_idx, 2);
               refbuf[0] = tmp[0];
               refbuf[1] = tmp[1] | 0xc0;

               if (i_idx > j_idx)
                  k_idx = i_idx - j_idx;
               else
                  k_idx = 0;

               mempcpy(tmp, (char *)&k_idx, 2);
               refbuf[4] = tmp[0];
               refbuf[5] = tmp[1];

               sendto(ref_sock,(char *)refbuf, 8 + (j_idx * 20), 0,
                      (struct sockaddr *)&fromInbound,
                      sizeof(struct sockaddr_in));
            }

            i_idx = 0;
            j_idx = 0;
            k_idx = 0;
            for (inbound_pos = inbound_list.begin(), i_idx = 0; inbound_pos != inbound_list.end();  inbound_pos++, i_idx++)
            {
               /* each entry has 20 bytes */
               inbound_ptr = (inbound *)inbound_pos->second;
               if (!inbound_ptr->is_ref)
               {
                  refbuf[8 + (20 * j_idx)] = inbound_ptr->mod;
                  strcpy((char *)refbuf + 9 + (20 * j_idx), inbound_ptr->call);

                  /* dont know what these 11 butes are */
                  refbuf[17 + (20 * j_idx)] = 0;

                  /* refbuf[18 + (20 * j_idx)] = 0; */
                  if (memcmp(inbound_ptr->serial, "AP", 2) == 0)
                     refbuf[18 + (20 * j_idx)] = 'A';   /* dvap */
                  if (memcmp(inbound_ptr->serial, "DV019999", 8) == 0)
                     refbuf[18 + (20 * j_idx)] = 'H';   /* spot */
                  else
                     refbuf[18 + (20 * j_idx)] = 'D';   /* dongle */

                  refbuf[19 + (20 * j_idx)] = 0;
                  refbuf[20 + (20 * j_idx)] = 0x0d;
                  refbuf[21 + (20 * j_idx)] = 0x4d;
                  refbuf[22 + (20 * j_idx)] = 0x37;
                  refbuf[23 + (20 * j_idx)] = 0x4d;
                  refbuf[24 + (20 * j_idx)] = 0x6f;
                  refbuf[25 + (20 * j_idx)] = 0x98;
                  refbuf[26 + (20 * j_idx)] = 0x04;
                  refbuf[27 + (20 * j_idx)] = 0;

                  j_idx++;

                  if (j_idx == 39)
                  {
                     /* 20 bytes for each user, so 39 * 20 = 788 bytes + 8 bytes header = 788 */
                     refbuf[0] = 0x14; 
                     refbuf[1] = 0xc3;

                     k_idx = i_idx - 38;
                     memcpy(tmp, (char *)&k_idx, 2);
                     refbuf[4] = tmp[0];
                     refbuf[5] = tmp[1];

                     sendto(ref_sock,(char *)refbuf,788,0,
                         (struct sockaddr *)&fromInbound,
                         sizeof(struct sockaddr_in));

                     j_idx = 0;
                  }
               }
            }

            if (j_idx != 0)
            {
               k_idx = 8 + (j_idx * 20);
               memcpy(tmp, (char *)&k_idx, 2);
               refbuf[0] = tmp[0];
               refbuf[1] = tmp[1] | 0xc0;

               if (i_idx > j_idx)
                  k_idx = i_idx - j_idx;
               else
                  k_idx = 0;

               mempcpy(tmp, (char *)&k_idx, 2);
               refbuf[4] = tmp[0];
               refbuf[5] = tmp[1];

               sendto(ref_sock,(char *)refbuf, 8 + (j_idx * 20), 0,
                      (struct sockaddr *)&fromInbound,
                      sizeof(struct sockaddr_in));
            }
         }
         else
         /* date request */
         if ((recvlen3 == 4) &&
             (refbuf[0] == 4) &&
             (refbuf[1] == 192) &&
             (refbuf[2] == 8) &&
             (refbuf[3] == 0))
         {
            inbound_pos = inbound_list.find(an_ip);
            if (inbound_pos != inbound_list.end())
            {
               inbound_ptr = (inbound *)inbound_pos->second;
               // traceit("Remote station %s %s requested date\n", inbound_ptr->call, an_ip);
            }
            
            time_t ltime;
            struct tm tm;

            time(&ltime);
            localtime_r(&ltime,&tm);
      
            refbuf[0] = 34;
            refbuf[1] = 192;
            refbuf[2] = 8;
            refbuf[3] = 0;
            refbuf[4] = 0xb5;
            refbuf[5] = 0xae;
            refbuf[6] = 0x37;
            refbuf[7] = 0x4d;
            snprintf((char *)refbuf + 8, READBUFFER_SIZE - 1, 
                     "20%02d/%02d/%02d %02d:%02d:%02d %5.5s",
                     tm.tm_year % 100, tm.tm_mon+1,tm.tm_mday,
                     tm.tm_hour,tm.tm_min,tm.tm_sec, 
                     (tzname[0] == NULL)?"     ":tzname[0]);
                      
            sendto(ref_sock,(char *)refbuf,34,0,
                 (struct sockaddr *)&fromInbound,
                 sizeof(struct sockaddr_in));
       
         } 
         else 
         /* version request */
         if ((recvlen3 == 4) &&
             (refbuf[0] == 4) &&
             (refbuf[1] == 192) &&
             (refbuf[2] == 3) &&
             (refbuf[3] == 0)) 
         {
            inbound_pos = inbound_list.find(an_ip);
            if (inbound_pos != inbound_list.end())
            {
               inbound_ptr = (inbound *)inbound_pos->second;
               // traceit("Remote station %s %s requested version\n", inbound_ptr->call, an_ip);
            }
            
            refbuf[0] = 9;
            refbuf[1] = 192;
            refbuf[2] = 3;
            refbuf[3] = 0;
            strncpy((char *)refbuf + 4, VERSION, 4); 
            refbuf[8] = 0;

            sendto(ref_sock,(char *)refbuf,9,0,
                 (struct sockaddr *)&fromInbound,
                 sizeof(struct sockaddr_in));
         }
         else 
         /* dvap, dvtool disconnects */
         if ((recvlen3 == 5) &&
             (refbuf[0] == 5) &&
             (refbuf[1] == 0) &&
             (refbuf[2] == 24) &&
             (refbuf[3] == 0) &&
             (refbuf[4] == 0))
         {
            inbound_pos = inbound_list.find(an_ip);
            if (inbound_pos != inbound_list.end())
            {
               /* reply with the same DISCONNECT */
               sendto(ref_sock,(char *)refbuf,5,0,
                   (struct sockaddr *)&fromInbound,
                   sizeof(struct sockaddr_in));

               inbound_ptr = (inbound *)inbound_pos->second;
               traceit("Call %s disconnected\n", inbound_ptr->call);
               free(inbound_pos->second);
               inbound_pos->second = NULL;
               inbound_list.erase(inbound_pos);
            }
            else
              traceit("Disconnect received from %s\n", an_ip);
         }
         else
         if ((recvlen3 == 5) &&
             (refbuf[0] == 5) &&
             (refbuf[1] == 0) &&
             (refbuf[2] == 24) &&
             (refbuf[3] == 0) &&
             (refbuf[4] == 1))
         {
            /* If we already have this ip, that means that we already requested a link to ref */
            inbound_pos = inbound_list.find(an_ip);
            if (inbound_pos != inbound_list.end())
            {
               traceit("Connect request accepted from %s, sending login\n", an_ip);
               /* send a login */
               refbuf[0] = 28;
               refbuf[1] = 192;
               refbuf[2] = 4;
               refbuf[3] = 0;

               memcpy(refbuf + 4, ADMIN, CALL_SIZE);
               for (j = 11; j > 3; j--)
               {
                  if (refbuf[j] == ' ')
                     refbuf[j] = '\0';
                  else
                     break;
               }
               memset(refbuf + 12, '\0', 8);
               memcpy(refbuf + 20, "DV019999", 8);

               sendto(ref_sock,(char *)refbuf,28,0,
                      (struct sockaddr *)&fromInbound,
                      sizeof(fromInbound));
            }
            else   /* They want to connect with our reflector */
            {
               if ((inbound_list.size() + 1) > MAX_OTHER_USERS)
                  traceit("Inbound connection from %s but over the MAX_OTHER_USERS limit of %d\n",
                          an_ip, inbound_list.size());
               else
                  sendto(ref_sock,(char *)refbuf,5,0,
                         (struct sockaddr *)&fromInbound,
                         sizeof(fromInbound));
            }
         }
         else
         if ((recvlen3 == 8) &&  /* this is a response to our login request */
             (refbuf[0] == 8) &&
             (refbuf[1] == 192) &&
             (refbuf[2] == 4) &&
             (refbuf[3] == 0))
         {
            /* we should already have the ip address in our list */
            if ((refbuf[4] == 79) &&
                (refbuf[5] == 75) &&
                (refbuf[6] == 82))
               traceit("Login OK to %s\n", an_ip);
            else
               traceit("Login to %s failed\n", an_ip);
         }
         else
         if ((recvlen3 == 28) &&  /* they want to login to our reflector */
             (refbuf[0] == 28) &&
             (refbuf[1] == 192) &&
             (refbuf[2] == 4) &&
             (refbuf[3] == 0))
         {
            /* verify callsign */
            memcpy(a_call, refbuf + 4, CALL_SIZE);
            a_call[CALL_SIZE] = '\0';
            for (i = 7; i > 0; i--)
            {
               if (a_call[i] == '\0')
                  a_call[i] = ' ';
               else
                  break;
            }

            traceit("Possible new connection: %s, ip=%s, DV=%.8s\n",
                     a_call, an_ip, refbuf + 20);

            if (!plus_validate_connection(dbp, a_call, an_ip) /* !get_ip(a_call, NULL) */ || /* call not in db */
                (blocks.find(a_call) != blocks.end()) ||     /* BLOCKED a_call */
                (memcmp(a_call, OWNER, OWNER_SIZE) == 0))    /* a_call can NOT be our reflector */
            {
               traceit("Invalid CALL=%s,ip=%s\n", a_call, an_ip);

               refbuf[0] = 8;
               refbuf[4] = 70;
               refbuf[5] = 65;
               refbuf[6] = 73;
               refbuf[7] = 76;

               sendto(ref_sock,(char *)refbuf,8,0,
                      (struct sockaddr *)&fromInbound,
                      sizeof(fromInbound));
            }
            else
            {
               if ((inbound_list.size() + 1) > MAX_OTHER_USERS)
               {
                  traceit("Inbound connection from %s but over the MAX_OTHER_USERS limit of %d\n",
                          an_ip, inbound_list.size());

                  refbuf[0] = 8;
                  refbuf[4] = 70;
                  refbuf[5] = 65;
                  refbuf[6] = 73;
                  refbuf[7] = 76;

                  sendto(ref_sock,(char *)refbuf,8,0,
                         (struct sockaddr *)&fromInbound,
                         sizeof(fromInbound));
               }
               else
               {
                  /* add the dongle to the inbound list */
                  inbound_ptr = (inbound *)malloc(sizeof(inbound));
                  if (inbound_ptr)
                  {
                     inbound_ptr->countdown = TIMEOUT;
                     memcpy((char *)&(inbound_ptr->sin),(char *)&fromInbound, sizeof(struct sockaddr_in));
                     strcpy(inbound_ptr->call, a_call);
                     time(&(inbound_ptr->connect_time));
                     inbound_ptr->isMute = false;
                     inbound_ptr->mod = ' ';
                     inbound_ptr->is_ref = false;
                     inbound_ptr->links[0] = inbound_ptr->links[1] = inbound_ptr->links[2] = inbound_ptr->links[3] = inbound_ptr->links[4] = ' ';

                     memcpy(inbound_ptr->serial, refbuf + 20, 8); inbound_ptr->serial[8] = '\0';

                     inbound_insert_pair = inbound_list.insert(pair<string, inbound *>(an_ip, inbound_ptr));
                     if (inbound_insert_pair.second)
                     {
                        traceit("new CALL=%s,DONGLE,ip=%s,users=%d\n",
                                inbound_ptr->call,an_ip,inbound_list.size() + a_user_list.size());

                        refbuf[0] = 8;
                        refbuf[4] = 79;
                        refbuf[5] = 75;
                        refbuf[6] = 82;
                        refbuf[7] = 87;

                        sendto(ref_sock,(char *)refbuf,8,0,
                               (struct sockaddr *)&fromInbound,
                               sizeof(fromInbound));
                     }
                     else
                     {
                        traceit("failed to add CALL=%s,ip=%s\n",inbound_ptr->call,an_ip);
                        free(inbound_ptr);
                        inbound_ptr = NULL;

                        refbuf[0] = 8;
                        refbuf[4] = 70;
                        refbuf[5] = 65;
                        refbuf[6] = 73;
                        refbuf[7] = 76;

                        sendto(ref_sock,(char *)refbuf,8,0,
                            (struct sockaddr *)&fromInbound,
                            sizeof(fromInbound));
                     }
                  }
                  else
                  {
                     traceit("malloc() failed for call=%s,ip=%s\n",a_call,an_ip);

                     refbuf[0] = 8;
                     refbuf[4] = 70;
                     refbuf[5] = 65;
                     refbuf[6] = 73;
                     refbuf[7] = 76;

                     sendto(ref_sock,(char *)refbuf,8,0,
                         (struct sockaddr *)&fromInbound,
                         sizeof(fromInbound));
                  }
               }
            }
         }
         else
         if ((recvlen3 == 3) && (memcmp(refbuf, REF_ACK, 3) == 0))  /* keepalive */
         {
            inbound_pos = inbound_list.find(an_ip);
            if (inbound_pos != inbound_list.end())
            {
               inbound_ptr = (inbound *)inbound_pos->second;
               inbound_ptr->countdown = TIMEOUT;
            }
         }
         else
         if ( ((recvlen3 == 58) ||
               (recvlen3 == 29) ||
               (recvlen3 == 32)) &&
              (memcmp(refbuf + 2, "DSVT", 4) == 0) &&
              ((refbuf[6] == 0x10) ||
               (refbuf[6] == 0x20)) &&
              (refbuf[10] == 0x20))
         {
            found = false;
            inbound_pos = inbound_list.find(an_ip);
            if (inbound_pos != inbound_list.end())
            {
               inbound_ptr = (inbound *)inbound_pos->second;
               inbound_ptr->countdown = TIMEOUT;

               /* We drop audio from muted stations */
               if (!inbound_ptr->isMute)
                  found = true;
            }

            /*** automatically mute the user callsign if not in database ***/
            if (inbound_pos != inbound_list.end())
            {
               if (recvlen3 == 58)
               {
                  inbound_ptr = (inbound *)inbound_pos->second;

                  memcpy(a_call, refbuf + 44, CALL_SIZE);
                  a_call[CALL_SIZE] = '\0';
                  if (!get_ip(a_call, NULL))
                  {
                     traceit("User callsign [%s] not valid, audio will not be transmitted\n", a_call);
                     // inbound_ptr->isMute = true;
                     found = false;
                  }
               }
            }

            if (found)
            {
               sprintf(an_rcd_streamid,"%d%d", refbuf[14],refbuf[15]);
               if (recvlen3 == 58)
               {
                  if (QSO_DETAILS)
                  {
                     if (!inbound_ptr->is_ref)
                        source_mod = ' ';
                     else
                     {
                        if (refbuf[35] == 'G')
                           source_mod = refbuf[27];
                        else
                        if (refbuf[27] == 'G')
                           source_mod = refbuf[35];
                        else
                           source_mod = refbuf[27];
                     }
                     traceit("START user: streamID=%d,%d, my=%.8s, sfx=%.4s, ur=%.8s, rpt1=%.8s, rpt2=%.8s, %d bytes fromIP=%s, src=%.8s %c\n",
                           refbuf[14],refbuf[15],&refbuf[44],
                           &refbuf[52], &refbuf[36],
                           &refbuf[20], &refbuf[28],
                           recvlen3, an_ip,
                           inbound_ptr->call, source_mod);
                  } 

                  /* the crazy shit */

                  /* replace the rpt1 name and rpt2 name with our callsign */
                  /* leave the module of the remote system untouched */

                  if (refbuf[35] == 'G')  /* dongle,  */
                  {
                     memcpy(refbuf + 20, OWNER, 7);
                     memcpy(refbuf + 28, OWNER, 7); 
                  }
                  else
                  if (refbuf[27] == 'G') /* RF user thru local repeater ---> reflector */
                  {
                     refbuf[27] = refbuf[35];
                     refbuf[35] = 'G';
                     memcpy(refbuf + 20, OWNER, 7);
                     memcpy(refbuf + 28, OWNER, 7);
                  }
                  else /* dvap */
                  {
                     memcpy(refbuf + 20, OWNER, 7);
                     memcpy(refbuf + 28, OWNER, 7);
                     refbuf[35] = 'G';
                  }
               }
               else
               {
                  if ((refbuf[16] & 0x40) != 0)
                  {
                     if (QSO_DETAILS)
                        traceit("END user: streamID=%d,%d, %d bytes from IP=%s\n",
                                 refbuf[14],refbuf[15],recvlen3, an_ip);
                  }

                  for (i = 0; i < 4; i++)
                  {
                     if ((refbuf[14] == temp_r[i].hdr[14]) &&
                         (refbuf[15] == temp_r[i].hdr[15]))
                     {
                        temp_r[i].last_time = time(NULL);

                        if ((refbuf[16] & 0x40) != 0)
                        {
                           memset(temp_r[i].hdr, 0, 58);
                           temp_r[i].last_time = 0;
                        }

                        break;
                     }
                  }
               }

               /* send the data to repeaters */
               if (recvlen3 == 58)
               {
                  source_mod = ' ';

                  if (inbound_ptr->is_ref)
                  {
                     for (i = 0; i < 5; i++)
                     {
                        /* if the remote module matches */
                        if ((inbound_ptr->links[i] == refbuf[27]) &&
                            (refbuf[27] != ' '))
                        {
                           if (i == 0)
                              refbuf[27] = 'A';
                           else
                           if (i == 1)
                              refbuf[27] = 'B';
                           else
                           if (i == 2)
                              refbuf[27] = 'C';
                           else
                           if (i == 3)
                              refbuf[27] = 'D';
                           else
                              refbuf[27] = 'E';

                           source_mod = refbuf[27];

                           if (refbuf[27] == 'E')
                           {
                              rcd_pos = rcd_list.find(an_rcd_streamid);
                              if (rcd_pos == rcd_list.end())
                              {
                                 if (rcd_list.size() < MAX_RCD_USERS)
                                 {
                                    an_rcd = (struct rcd *)malloc(sizeof(struct rcd));
                                    if (an_rcd)
                                    {
                                       an_rcd->locked = false;
                                       time(&(an_rcd->ts)); /* stamp it */
                                       memcpy((char *)&(an_rcd->sin), (char *)&fromInbound, sizeof(struct sockaddr_in));
                                       memcpy(an_rcd->data[0], refbuf, recvlen3);
                                       an_rcd->recvlen = recvlen3;
                                       an_rcd->idx = 1;
                                       rcd_insert_pair = rcd_list.insert(pair<string, struct rcd *>(an_rcd_streamid, an_rcd));
                                       if (rcd_insert_pair.second)
                                          traceit("Started recording for user %.8s, streamid=%d,%d\n",
                                                  refbuf + 44, refbuf[14],refbuf[15]);
                                       else
                                       {
                                          traceit("failed to add user %.8s, streamid=%d,%d, for recording...\n",
                                                  refbuf + 44, refbuf[14],refbuf[15]);
                                          free(an_rcd);
                                          an_rcd = NULL;
                                       }
                                    }
                                    else
                                       traceit("Failed to allocate for recording on user %.8s, streamid=%d,%d\n",
                                               refbuf + 44, refbuf[14],refbuf[15]);
                                 }
                                 else
                                    traceit("Reached maximum concurrent users for recording\n");
                              }
                           }

                           /* This user is talking on this module */
                           inbound_ptr->mod = refbuf[27];

                           // put user into tmp1
                           memcpy(tmp1, refbuf + 44, 8); tmp1[8] = '\0';

                           // find if user exists in lh_list
                           lh_pos = lh_list.find(tmp1);
                           if (lh_pos != lh_list.end())
                           {
                              // user exists in lh_list

                              // get the data from lh_list
                              strcpy(tmp2, lh_pos->second.c_str());

                              // find item in dt_lh_list and erase it
                              dt_lh_pos = dt_lh_list.find(tmp2);
                              if (dt_lh_pos != dt_lh_list.end())
                                 dt_lh_list.erase(dt_lh_pos);

                              // update lh_list
                              time(&tNow);
                              sprintf(tmp2, "%ld=%.7s%c", tNow, inbound_ptr->call, refbuf[27]);
                              lh_list[tmp1] = tmp2;

                              // add item to dt_lh_list
                              dt_lh_list[tmp2] = tmp1;
                              if (dt_lh_list.size() > LH_MAX_SIZE)
                              {
                                 dt_lh_pos = dt_lh_list.begin();
                                 if (dt_lh_pos != dt_lh_list.end())
                                    dt_lh_list.erase(dt_lh_pos);
                              }
                           }
                           else
                           {
                              // user does NOT exist in lh_list

                              // Are we getting over the limit?
                              if (lh_list.size() ==  LH_MAX_SIZE)
                              {
                                 // get the first item in dt_lh_list and extract callsign
                                 dt_lh_pos = dt_lh_list.begin();
                                 if (dt_lh_pos != dt_lh_list.end())
                                 {
                                    // find that callsign in lh_list and delete it
                                    strcpy(tmp1, dt_lh_pos->second.c_str());
                                    lh_pos = lh_list.find(tmp1);
                                    if (lh_pos != lh_list.end())
                                       lh_list.erase(tmp1);
                                    else
                                    {
                                       lh_pos = lh_list.begin();
                                       if (lh_pos != lh_list.end())
                                          lh_list.erase(lh_pos);
                                    }
                                 }
                                 else
                                 {
                                    lh_pos = lh_list.begin();
                                    if (lh_pos != lh_list.end())
                                       lh_list.erase(lh_pos);
                                 }
                              }

                              // add item to lh_list
                              memcpy(tmp1, refbuf + 44, 8); tmp1[8] = '\0';
                                
                              time(&tNow);
                              sprintf(tmp2, "%ld=%.7s%c", tNow, inbound_ptr->call, refbuf[27]);
                              lh_list[tmp1] = tmp2;

                              // add item to dt_lh_list
                              dt_lh_list[tmp2] = tmp1;
                              if (dt_lh_list.size() > LH_MAX_SIZE)
                              {
                                 dt_lh_pos = dt_lh_list.begin();
                                 if (dt_lh_pos != dt_lh_list.end())
                                    dt_lh_list.erase(dt_lh_pos);
                              }
                           }

                           if (rcd_list.find(an_rcd_streamid) == rcd_list.end())
                           {
                              memcpy(readBuffer, refbuf + 2, 56);
                              send_a_pkt_to_all(fromInbound.sin_addr.s_addr, 56, true);
                           }
                           break;
                        }
                     }
                  }                     
                  else
                  if (refbuf[27] != ' ')
                  {
                     source_mod = refbuf[27];

                     if (refbuf[27] == 'E')
                     {
                        rcd_pos = rcd_list.find(an_rcd_streamid);
                        if (rcd_pos == rcd_list.end())
                        {
                           if (rcd_list.size() < MAX_RCD_USERS)
                           {
                              an_rcd = (struct rcd *)malloc(sizeof(struct rcd));
                              if (an_rcd)
                              {
                                 an_rcd->locked = false;
                                 time(&(an_rcd->ts)); /* stamp it */
                                 memcpy((char *)&(an_rcd->sin), (char *)&fromInbound, sizeof(struct sockaddr_in));
                                 memcpy(an_rcd->data[0], refbuf, recvlen3);
                                 an_rcd->recvlen = recvlen3;
                                 an_rcd->idx = 1;
                                 rcd_insert_pair = rcd_list.insert(pair<string, struct rcd *>(an_rcd_streamid, an_rcd));
                                 if (rcd_insert_pair.second)
                                    traceit("Started recording for user %.8s, streamid=%d,%d\n",
                                            refbuf + 44, refbuf[14],refbuf[15]);
                                 else
                                 {
                                    traceit("failed to add user %.8s, streamid=%d,%d, for recording...\n",
                                            refbuf + 44, refbuf[14],refbuf[15]);
                                    free(an_rcd);
                                    an_rcd = NULL;
                                 }
                              }
                              else
                                 traceit("Failed to allocate for recording on user %.8s, streamid=%d,%d\n",
                                         refbuf + 44, refbuf[14],refbuf[15]);
                           }
                           else
                              traceit("Reached maximum concurrent users for recording\n");
                        }
                     }

                     /* This user is talking on this module */
                     inbound_ptr->mod = refbuf[27];

                     // put user into tmp1
                     memcpy(tmp1, refbuf + 44, 8); tmp1[8] = '\0';

                     // find if user exists in lh_list
                     lh_pos = lh_list.find(tmp1);
                     if (lh_pos != lh_list.end())
                     {
                        // user exists in lh_list

                        // get the data from lh_list
                        strcpy(tmp2, lh_pos->second.c_str());

                        // find item in dt_lh_list and erase it
                        dt_lh_pos = dt_lh_list.find(tmp2);
                        if (dt_lh_pos != dt_lh_list.end())
                           dt_lh_list.erase(dt_lh_pos);

                        // update lh_list
                        time(&tNow);
                        sprintf(tmp2, "%ld=%.7s%c", tNow, inbound_ptr->call, refbuf[27]);
                        lh_list[tmp1] = tmp2;

                        // add item to dt_lh_list
                        dt_lh_list[tmp2] = tmp1;
                        if (dt_lh_list.size() > LH_MAX_SIZE)
                        {
                           dt_lh_pos = dt_lh_list.begin();
                           if (dt_lh_pos != dt_lh_list.end())
                              dt_lh_list.erase(dt_lh_pos);
                        }
                     }
                     else
                     {
                        // user does NOT exist in lh_list

                        // Are we getting over the limit?
                        if (lh_list.size() ==  LH_MAX_SIZE)
                        {
                           // get the first item in dt_lh_list and extract callsign
                           dt_lh_pos = dt_lh_list.begin();
                           if (dt_lh_pos != dt_lh_list.end())
                           {
                              // find that callsign in lh_list and delete it
                              strcpy(tmp1, dt_lh_pos->second.c_str());
                              lh_pos = lh_list.find(tmp1);
                              if (lh_pos != lh_list.end())
                                 lh_list.erase(tmp1);
                              else
                              {
                                 lh_pos = lh_list.begin();
                                 if (lh_pos != lh_list.end())
                                    lh_list.erase(lh_pos);
                              }
                           }
                           else
                           {
                              lh_pos = lh_list.begin();
                              if (lh_pos != lh_list.end())
                                 lh_list.erase(lh_pos);
                           }
                        }

                        // add item to lh_list
                        memcpy(tmp1, refbuf + 44, 8); tmp1[8] = '\0';

                        time(&tNow);
                        sprintf(tmp2, "%ld=%.7s%c", tNow, inbound_ptr->call, refbuf[27]);
                        lh_list[tmp1] = tmp2;

                        // add item to dt_lh_list
                        dt_lh_list[tmp2] = tmp1;
                        if (dt_lh_list.size() > LH_MAX_SIZE)
                        {
                           dt_lh_pos = dt_lh_list.begin();
                           if (dt_lh_pos != dt_lh_list.end())
                              dt_lh_list.erase(dt_lh_pos);
                        }
                     }

                     if (rcd_list.find(an_rcd_streamid) == rcd_list.end())
                     {
                        memcpy(readBuffer, refbuf + 2, 56);
                        send_a_pkt_to_all(fromInbound.sin_addr.s_addr, 56, false);
                     }
                  }
               }
               else
               {
                  rcd_pos = rcd_list.find(an_rcd_streamid);
                  if (rcd_pos != rcd_list.end())
                  {
                     an_rcd = (struct rcd *)rcd_pos->second;
                     time(&(an_rcd->ts)); /* stamp it */
                     if ((an_rcd->idx < MAX_RCD_DATA) && !(an_rcd->locked))
                     {
                        memcpy(an_rcd->data[an_rcd->idx], refbuf, recvlen3);
                        an_rcd->idx ++;
                     }
                     if (((refbuf[16] & 0x40) != 0) && !(an_rcd->locked))
                     {
                        traceit("Finished recording for user %.8s, streamid=%d,%d\n",
                                 &(an_rcd->data[0][44]), refbuf[14], refbuf[15]);

                        /*** start playback */
                        an_rcd->locked = true;
                        pthread_attr_init(&attr);
                        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
                        rc = pthread_create(&playback_thread,&attr,playback,(void *)rcd_pos->second);
                        if (rc != 0)
                        {
                           traceit("Failed to start the playback thread for streamid=%d,%d\n",
                                    refbuf[14], refbuf[15]);
                           free(rcd_pos->second);
                           rcd_pos->second = NULL;
                           rcd_list.erase(rcd_pos);
                        }
                        pthread_attr_destroy(&attr);
                     }
                  }
                  else
                  {
                     /* the function send_a_pkt_to_all will block the audio if header was NOT sent */
                     /* so the only thing to do is NOT send the header */
                     memcpy(readBuffer, refbuf + 2, 27);
                     send_a_pkt_to_all(fromInbound.sin_addr.s_addr, 27, inbound_ptr->is_ref);
                  }
               }

               /* At this point, if this is a header, it contains our reflector info */
               /* save the header for re-transmit */
               if (recvlen3 == 58)
               {
                  i = -1;
                  if (source_mod == 'A')
                     i = 0;
                  else
                  if (source_mod == 'B')
                     i = 1;
                  else
                  if (source_mod == 'C')
                     i = 2;
                  else
                  if (source_mod == 'D')
                     i = 3;

                  if (i >= 0)
                  {
                     if (temp_r[i].last_time == 0)
                     {
                        memcpy(temp_r[i].hdr, refbuf, 58);
                        temp_r[i].last_time = time(NULL);
                     }
                  }
               }

               /* send the data to dvap, dvtool stations */
               if (rcd_list.find(an_rcd_streamid) == rcd_list.end())
               {
               for (inbound_pos2 = inbound_list.begin(); inbound_pos2 != inbound_list.end(); inbound_pos2++)
               {
                  if (strcmp(inbound_pos2->first.c_str(), inbound_pos->first.c_str()) != 0)
                  {
                     inbound_ptr2 = (inbound *)inbound_pos2->second;
                     if (recvlen3 == 58)
                     {
                        /* if it is a reflector, we must send its own call */
                        if (inbound_ptr2->is_ref)
                        {
                           if (i >= 0)
                           {
                              if (inbound_ptr2->links[i] != ' ')
                              {
                                 memcpy(refbuf + 20, inbound_ptr2->call, 7);
                                 refbuf[27] = inbound_ptr2->links[i];
                                 memcpy(refbuf + 28, inbound_ptr2->call, 7);

                                 sendto(ref_sock, (char *)refbuf, recvlen3, 0,
                                        (struct sockaddr *)&(inbound_ptr2->sin),
                                        sizeof(struct sockaddr_in));
                              }
                           }
                        }
                        else
                        {
                           if (i >= 0)
                           {
                              memcpy(refbuf + 20, OWNER, 7);
                              refbuf[27] = source_mod;
                              memcpy(refbuf + 28, OWNER, 7);

                              sendto(ref_sock, (char *)refbuf, recvlen3, 0,
                                     (struct sockaddr *)&(inbound_ptr2->sin),
                                     sizeof(struct sockaddr_in));
                           }
                        }
                     }
                     else
                     {
                        if ((refbuf[26] == 0x55) && (refbuf[27] == 0x2d) && (refbuf[28] == 0x16))
                        {
                           if ((!inbound_ptr2->is_ref) && (!inbound_ptr->is_ref))
                           {
                              for (i = 0; i < 4; i++)
                              {
                                 if ((refbuf[14] == temp_r[i].hdr[14]) &&
                                     (refbuf[15] == temp_r[i].hdr[15]))
                                 {
                                    sendto(ref_sock, (char *)temp_r[i].hdr, 58, 0, 
                                           (struct sockaddr *)&(inbound_ptr2->sin), sizeof(struct sockaddr_in));
                                    break;
                                 }
                              }
                           }
                        }

                        /* if it is a regular dongle, send it */
                        if (!inbound_ptr2->is_ref)
                           sendto(ref_sock, (char *)refbuf, recvlen3, 0, 
                                  (struct sockaddr *)&(inbound_ptr2->sin),
                                  sizeof(struct sockaddr_in));
                        else
                        {
                           /* treat the remote reflector with resepct */
                           /* send it only if the module matches */
                           for (i = 0; i < 4; i++)
                           {
                              if ((refbuf[14] == temp_r[i].hdr[14]) &&
                                  (refbuf[15] == temp_r[i].hdr[15]) &&
                                  (inbound_ptr2->links[i] != ' '))
                              { 
                                 sendto(ref_sock, (char *)refbuf, recvlen3, 0,
                                        (struct sockaddr *)&(inbound_ptr2->sin),
                                        sizeof(struct sockaddr_in));

                                 break;
                              } 
                           }
                        }
                     }
                  }
               }
               } 
            }
         }
         FD_CLR (ref_sock,&fdset);
      }

      /* process input from stations, remote links, repeaters, PC users */
      if (FD_ISSET(srv_sock, &fdset))
      {
         fromlen = sizeof(struct sockaddr_in);
         recvlen = recvfrom(srv_sock,(char *)readBuffer,READBUFFER_SIZE,
                            0,(struct sockaddr *)&fromUser,&fromlen);

         strncpy(an_ip, inet_ntoa(fromUser.sin_addr),IP_SIZE);
         an_ip[IP_SIZE] = '\0';
         a_user_ptr = NULL;

         if ((recvlen == (CALL_SIZE + 1)) || (recvlen == (CALL_SIZE + 3)))
         {
            readBuffer[(recvlen - 1)] = '\0';

            strncpy(a_call, (char *)readBuffer,CALL_SIZE);
            a_call[CALL_SIZE] = '\0';

            /* is this a new user? */
            user_pos = a_user_list.find(an_ip);
            if (user_pos != a_user_list.end())
            {
               /* We found it */
               /* IP address is already connected, this user is already connected */

               /* assume that we will not delete this user */
               deleted = false;

               /* point to the data of the connected user */
               a_user_ptr = (struct a_user *)user_pos->second;

               /* update its countdown */
               a_user_ptr->countdown = TIMEOUT;

               /* Update ip address and port */
               memcpy((char *)&(a_user_ptr->sin),(char *)&fromUser, sizeof(struct sockaddr_in));
            
               if ((a_user_ptr->is_repeater) && (plus_get_nat_type(dbp, a_call) == 0))
                 a_user_ptr->sin.sin_port = htons(LISTEN_PORT);

               /* Same IP address, but call changed ? */
               if (memcmp(a_user_ptr->call, a_call, CALL_SIZE) != 0)
               {
                  traceit("Same IP %s but call changed from %s to %s, will remove old call\n",
                          an_ip, a_user_ptr->call, a_call);

                  strcpy(a_call,a_user_ptr->call);

                  free(user_pos->second);
                  a_user_ptr = user_pos->second = NULL;
                  a_user_list.erase(user_pos);
                  deleted = true;

                  traceit("Deleted old record %s\n", a_call);

                  print_links_file();
               }

               /* User has been blocked ?, if yes, remove */
               if (!deleted)
               {
                  block_pos = blocks.find(a_call);
                  if (block_pos != blocks.end())
                  {
                     traceit("Call %s with %s will be removed because it is blocked\n",a_call, an_ip);
                     free(user_pos->second);
                     a_user_ptr = user_pos->second = NULL;
                     a_user_list.erase(user_pos);
                     deleted = true;

                     print_links_file();
                  }
               }
              
               if (!deleted)
               { 
                  /*
                     repeater request, an un-link request arrived ? 
                     Example:  DB0SAT space space B space NULL

                               This means that repeater DB0SAT  wants to unlink its module B
                                  from reflector 
                  */
                  if ((recvlen == (CALL_SIZE + 3)) && (a_user_ptr->is_repeater))
                  {
                     traceit("Possible new link/unlink request: %.*s\n", recvlen - 1, readBuffer);

                     /* remote repeater band */
                     if ((readBuffer[8] == 'A') || (readBuffer[8] == 'B') || (readBuffer[8] == 'C') ||
                         (readBuffer[8] == 'D'))
                     {
                        /* local reflector module */
                        if (readBuffer[9] == ' ')
                        {
                           traceit("Call %s mod %c drops the link\n",
                                 a_call, readBuffer[8]);

                           /* this is the the remote repeater band */
                           if (readBuffer[8] == 'A')
                              k = 0;
                           else
                           if (readBuffer[8] == 'B')
                              k = 1;
                           else
                           if (readBuffer[8] == 'C')
                              k = 2;
                           else
                              k = 3;

                           /* for all the reflector modules, zero out the repeater band */
                           for (i = 0; i < 5; i++)
                           {
                              /*** inform the remote XRF reflector: start ***/
                              if (a_user_ptr->is_xrf)
                              {
                                 if (a_user_ptr->rpt_mods[i][k] != '\0')
                                 {
                                    memcpy(reply_to_xrf, OWNER, 8);
                                    if (i == 0)
                                       reply_to_xrf[8] = 'A';
                                    else
                                    if (i == 1)
                                       reply_to_xrf[8] = 'B';
                                    else
                                    if (i == 2)
                                       reply_to_xrf[8] = 'C';
                                    else
                                    if (i == 3)
                                       reply_to_xrf[8] = 'D';
                                    else
                                       reply_to_xrf[8] = 'E';

                                    reply_to_xrf[9] = ' ';
                                    reply_to_xrf[10] = '\0';

                                    for (j = 0; j < 5; j++)
                                       sendto(srv_sock, reply_to_xrf, CALL_SIZE + 3,
                                              0,(struct sockaddr *)&(a_user_ptr->sin),
                                              sizeof(struct sockaddr_in));
                                 }
                              }
                              /*** inform the remote XRF reflector: end ***/

                              a_user_ptr->rpt_mods[i][k] = '\0';
                              a_user_ptr->link_time[i][k] = 0;
                           }

                           print_links_file();

                           /* if this repeater has unlinked everything, disconnect it */
                           for (i = 0; i < 5; i++)
                           {
                              for (k = 0; k < 4; k++)
                              {
                                 if (a_user_ptr->rpt_mods[i][k] != '\0')
                                    break;
                              }
                              if (k < 4)
                                 break;
                           }
                           if (i == 5)
                           {
                              /* 
                                 All linked modules have been unlinked,
                                 disconnect the repeater
                              */
                              traceit("All linked modules from %s have been unlinked, disconnecting...\n",
                                       a_call);
                              free(user_pos->second);
                              a_user_ptr = user_pos->second = NULL;
                              a_user_list.erase(user_pos);
                              deleted = true;
                              traceit("User %s ip=%s  removed\n", a_call, an_ip);

                           }
                        }
                        else
                        {
                           /*
                              A link request from remote repeater
                              Example:  DB0SAT space space BC NULL

                               This means that repeater DB0SAT  wants to link its module B
                                  to our reflector module C
                           */

                           /* local reflector module */
                           if ((readBuffer[9] == 'A') || (readBuffer[9] == 'B') || (readBuffer[9] == 'C') ||
                               (readBuffer[9] == 'D') || (readBuffer[9] == 'E'))
                           {

                              /* local reflector module */
                              if (readBuffer[9] == 'A')
                                 i = 0;
                              else
                              if (readBuffer[9] == 'B')
                                 i = 1;
                              else
                              if (readBuffer[9] == 'C')
                                 i = 2;
                              else
                              if (readBuffer[9] == 'D')
                                 i = 3;
                              else
                                 i = 4;

                              /* remote repeater band */
                              if (readBuffer[8] == 'A')
                                 k = 0;
                              else
                              if (readBuffer[8] == 'B')
                                 k = 1;
                              else
                              if (readBuffer[8] == 'C')
                                 k = 2;
                              else
                                 k = 3;

                              for (j = 0; j < 5; j++)
                              {
                                 if (i == j)
                                 {
                                    /*** inform the remote XRF reflector: start ***/
                                    if (a_user_ptr->is_xrf)
                                    {
                                       if (a_user_ptr->rpt_mods[i][k] != readBuffer[8])
                                       {
                                          memcpy(reply_to_xrf, OWNER, 8);
                                          reply_to_xrf[8] = readBuffer[9];
                                          reply_to_xrf[9] = readBuffer[8];
                                          reply_to_xrf[10] = '\0';
                                          for (n = 0; n < 5; n++)
                                             sendto(srv_sock, reply_to_xrf, CALL_SIZE + 3,
                                                 0,(struct sockaddr *)&(a_user_ptr->sin),
                                                 sizeof(struct sockaddr_in)); 
                                       }
                                    }
                                    /*** inform the remote XRF reflector: end ***/

                                    a_user_ptr->rpt_mods[i][k] = readBuffer[8];
                                    time(&a_user_ptr->link_time[i][k]); 
                                 }
                                 else
                                 {
                                    a_user_ptr->rpt_mods[j][k] = '\0';
                                    a_user_ptr->link_time[j][k] = 0;
                                 }
                              }
                              
                              traceit("Link established from %s mod %c ---> mod %c\n",
                                      a_user_ptr->call, readBuffer[8], readBuffer[9]);

                              print_links_file();

                              if (!a_user_ptr->is_xrf)
                              {
                                 memcpy(readBuffer + 10, "ACK", 4);
                                 sendto(srv_sock, readBuffer, CALL_SIZE + 6,
                                     0,(struct sockaddr *)&(a_user_ptr->sin),
                                     sizeof(struct sockaddr_in));
                              }
                           }
                        }
                     }
                  }
               }
            }
            else
            {
               allowed_to_connect = true;
               traceit("Possible new connection: %.*s ip=%s\n",  recvlen - 1, readBuffer, an_ip);

               do 
               {
                  /* ONWER can not connect to itself */
                  if (memcmp(a_call,OWNER,CALL_SIZE) == 0)
                  {
                     allowed_to_connect = false;
                     break;
                  }

                  /* reached capacity */ 
                  if (a_user_list.size() + 1 > MAX_USERS)
                  {
                     traceit("Reached MAX_USERS capacity\n");
                     allowed_to_connect = false;
                     break;
                  }
              
                  /* Is this callsign blocked? */ 
                  block_pos = blocks.find(a_call);
                  if (block_pos != blocks.end())
                  {
                     traceit("Callsign is blocked\n");
                     allowed_to_connect = false;
                     break;
                  }
                  
                  if (!plus_validate_connection(dbp, a_call, an_ip))
                  {
                    allowed_to_connect = false;
                    break;
                  }
               } while (false);

               if (allowed_to_connect)
               {
                  /* Lets find out what we have in the database */
                  payload[0] = '\0';
                  found = get_ip(a_call, payload);

                  if (recvlen == (CALL_SIZE + 1))
                  {
                     /* we have a repeater in the database, because we see an IP(or hostname) in dextra.db */
                     if (payload[0] != '\0')
                     {
                        // traceit("Incoming connection from %s %s but no link information supplied\n", a_call, an_ip);
                        allowed_to_connect = false;
                     }
                     else
                     {
                        /* did we find it in the database ? */
                        if (!found)
                           allowed_to_connect = false;
                     }
                  }
                  else
                  {
                     if (payload[0] == '\0')
                     {
                        traceit("Received link information from %s %s but call does not have an IP(or host) in db\n", a_call, an_ip);
                        allowed_to_connect = false;
                     }
                     else
                     {
                        /* reflector module */
                        if ((readBuffer[9] != 'A') && (readBuffer[9] != 'B') && (readBuffer[9] != 'C') &&
                            (readBuffer[9] != 'D') && (readBuffer[9] != 'E'))
                           allowed_to_connect = false; 
                        else
                        /* repeater band */
                        if ((readBuffer[8] != 'A') && (readBuffer[8] != 'B') && (readBuffer[8] != 'C') &&
                            (readBuffer[8] != 'D'))
                           allowed_to_connect = false; 
                        else
                        if ((strcmp(an_ip, payload) != 0) && IP_CHECK)
                        {
                           traceit("call %s: incoming IP=%s, IP in db=%s: IP does not match and IP_CHECK=Y\n", a_call, an_ip, payload);
                           allowed_to_connect = false;
                        }
                     }
                  }
               }

               if (allowed_to_connect)
               {
                  /* about to add the new user to the BST/map */
                  a_user_ptr = (struct a_user *)malloc(sizeof(struct a_user));
                  if (a_user_ptr)
                  {
                     /* Initialize the data for the new user */
                     a_user_ptr->countdown = TIMEOUT;
                     memcpy((char *)&(a_user_ptr->sin), (char *)&fromUser, sizeof(struct sockaddr_in));
                     strncpy(a_user_ptr->call, a_call, CALL_SIZE);
                     a_user_ptr->call[CALL_SIZE] = '\0';

                     a_user_ptr->isMute = false;

                     time(&(a_user_ptr->connect_time));

                     if (payload[0] != '\0')
                        a_user_ptr->is_repeater = true;
                     else
                        a_user_ptr->is_repeater = false;

                     if (a_user_ptr->is_repeater)
                     {
                        if (memcmp(a_user_ptr->call, "XRF", 3) == 0)
                           a_user_ptr->is_xrf = true;
                        else
                           a_user_ptr->is_xrf = false;
                     }
                     else
                        a_user_ptr->is_xrf = false;

                     if (a_user_ptr->is_repeater)
                        a_user_ptr->sin.sin_port = htons(LISTEN_PORT);

                     if (a_user_ptr->is_repeater)
                        strcpy(a_user_ptr->serial, "REPEATER");
                     else
                        strcpy(a_user_ptr->serial, "DONGLE  ");

                     /* Initialize links */
                     for (i = 0; i < 5; i++)
                     {
                        for (k = 0; k < 4; k++)
                        {
                           a_user_ptr->rpt_mods[i][k] = '\0';
                           a_user_ptr->link_time[i][k] = 0;
                        }
                     }

                     a_user_ptr->mod = ' ';

                     if ((recvlen == (CALL_SIZE + 3)) && (a_user_ptr->is_repeater))
                     {
                        /* 
                           A repeater requesting a specific link. 
                           set the link corrrectly.
                        */

                        /* local reflector module */
                        if (readBuffer[9] == 'A')
                           i = 0;
                        else
                        if (readBuffer[9] == 'B')
                           i = 1;
                        else
                        if (readBuffer[9] == 'C')
                           i = 2;
                        else
                        if (readBuffer[9] == 'D')
                           i = 3;
                        else
                           i = 4;

                        /* remote repeater band */
                        if (readBuffer[8] == 'A')
                           k = 0;
                        else
                        if (readBuffer[8] == 'B')
                           k = 1;
                        else
                        if (readBuffer[8] == 'C')
                           k = 2;
                        else
                           k = 3;

                        a_user_ptr->rpt_mods[i][k] = readBuffer[8];
                        time(&a_user_ptr->link_time[i][k]);

                     }

                     /* Add new user to the connected list */
                      user_insert_pair = a_user_list.insert(pair<string, struct a_user *>(an_ip, a_user_ptr)); 
                     if (user_insert_pair.second) 
                     {
                        traceit("new CALL=%s,%s,ip=%s,users=%d\n",
                                a_call,(a_user_ptr->is_repeater)?"REPEATER":"DONGLE",
                                an_ip,a_user_list.size() + inbound_list.size());
                        if (a_user_ptr->is_repeater)
                        {
                           traceit("Link established from %s mod %c ---> mod %c\n",
                                   a_call, readBuffer[8], readBuffer[9]); 
                           print_links_file();

                           if (!a_user_ptr->is_xrf)
                           {
                              memcpy(readBuffer + 10, "ACK", 4);
                              sendto(srv_sock, readBuffer, CALL_SIZE + 6,
                                  0,(struct sockaddr *)&(a_user_ptr->sin),
                                  sizeof(struct sockaddr_in));                     
                           }
                           else
                           {
                              /*** inform the remote XRF reflector: start ***/
                              memcpy(reply_to_xrf, OWNER, 8);
                              reply_to_xrf[8] = readBuffer[9];
                              reply_to_xrf[9] = readBuffer[8];
                              reply_to_xrf[10] = '\0';
                              for (n = 0; n < 5; n++)
                                 sendto(srv_sock, reply_to_xrf, CALL_SIZE + 3,
                                     0,(struct sockaddr *)&(a_user_ptr->sin),
                                     sizeof(struct sockaddr_in));
                              /*** inform the remote XRF reflector: end ***/
                           }
                        }
                        else
                        {
                           /* send HEARTBEAT */
                           sendto(srv_sock, OWNER, strlen(OWNER) + 1,
                                  0,(struct sockaddr *)&(a_user_ptr->sin),
                                  sizeof(struct sockaddr_in));
                        }
                     }
                     else
                     {
                        traceit("Failed to add CALL=%s,ip=%s\n",a_call,an_ip);

                        /* If it was a link request, send back a NAK */
                        if (a_user_ptr->is_repeater)
                        {
                           memcpy(readBuffer + 10, "NAK", 4);
                           sendto(srv_sock, readBuffer, CALL_SIZE + 6,
                                  0,(struct sockaddr *)&(a_user_ptr->sin),
                                  sizeof(struct sockaddr_in));
                        }
                        free(a_user_ptr);
                        a_user_ptr = NULL;
                     }
                  }
                  else
                  {
                     traceit("malloc() failed for call=%s,ip=%s\n",a_call, an_ip);

                     /* If it was a link request, send back a NAK */
                     if (recvlen == (CALL_SIZE + 3))
                     {
                        memcpy(readBuffer + 10, "NAK", 4);

                        fromUser.sin_port = htons(LISTEN_PORT);

                        sendto(srv_sock, readBuffer, CALL_SIZE + 6,
                               0,(struct sockaddr *)&fromUser,
                               sizeof(struct sockaddr_in));
                     }
                  }
               }
               else
               {  
                  /* 
                     If it was a link request, send back a NAK.
                     If it was an unlink request, do nothing 
                  */
                  if (recvlen == (CALL_SIZE + 3))
                  {
                     if ((readBuffer[8] != ' ') && (readBuffer[9] == ' '))
                        ;
                     else
                     {
                        memcpy(readBuffer + 10, "NAK", 4);

                        fromUser.sin_port = htons(LISTEN_PORT);

                        sendto(srv_sock, readBuffer, CALL_SIZE + 6,
                               0,(struct sockaddr *)&fromUser,
                               sizeof(struct sockaddr_in));
                     }
                  }
               }
            }
         }
         else
         if ((recvlen == 56) || (recvlen == 27))
         {
            found = true;

            user_pos = a_user_list.find(an_ip);
            if (user_pos != a_user_list.end())
            {
               /* point to the connected user */
               a_user_ptr = (struct a_user *)user_pos->second;

               /* refresh timeout */
               a_user_ptr->countdown = TIMEOUT;

               if (recvlen == 56)
               {
                  memcpy(a_call, readBuffer + 42, CALL_SIZE);
                  a_call[CALL_SIZE] = '\0';
                  if (!get_ip(a_call, NULL))
                  {
                     traceit("User callsign [%s] not valid, audio will not be transmitted\n", a_call);
                     found = false;
                  }
               }

               /* If the connected user is MUTED, do not process the audio packets */
               if (!a_user_ptr->isMute)
               {
                  sprintf(an_rcd_streamid,"%d%d", readBuffer[12], readBuffer[13]);
                  if ((recvlen == 56) && found)
                  {
                     if (!a_user_ptr->is_repeater)
                        source_mod = ' ';
                     else
                     {
                        /* a remote reflector sends its own data */
                        if (a_user_ptr->is_xrf)
                           source_mod = readBuffer[25];
                        else
                        {
                           /* 
                              Anything else, sends our data.
                              rpt1 contains our reflector module, we can NOT use that,
                              we have to find the band of the repeater.
                              Since a repeater may have more than one band linked to our reflector module,
                              we will use byte 11  as incoming source band.
                              If the repeater did not use g2_link to link to our reflector,
                                then that repeater must be modified to support the new XRF features of multilinking,
                              otherwise an X wil be printed in the log as the source module of the remote band.
                           */
                           if ((readBuffer[11] >= 'A') && (readBuffer[11] <= 'D'))
                              source_mod = readBuffer[11];
                           else
                           {
                              source_mod = 'X'; /* unknown */

                              /* last attempt to set source_mod */

                              /* byte 25 is our reflector mod */
                              if (readBuffer[25] == 'A')
                                 i = 0;
                              else
                              if (readBuffer[25] == 'B')
                                 i = 1;
                              else
                              if (readBuffer[25] == 'C')
                                 i = 2;
                              else
                              if (readBuffer[25] == 'D')
                                 i = 3;
                              else
                              if (readBuffer[25] == 'E')
                                 i = 4;
                              else
                                 i = -1;

                              /* now find the first band of the remote repeater linked to our reflector mod */
                              if (i >= 0)
                              {
                                 for (k = 0; k < 4; k++)
                                 {
                                    if (a_user_ptr->rpt_mods[i][k] != '\0')
                                    {
                                       source_mod = a_user_ptr->rpt_mods[i][k];  /* we found it */
                                       break;
                                    }
                                 }
                              }
                           }
                        }
                     }

                     if (QSO_DETAILS)
                        traceit("START user: streamID=%d,%d, my=%.8s, sfx=%.4s, ur=%.8s, rpt1=%.8s, rpt2=%.8s, %d bytes fromIP=%s, src=%.8s %c\n",
                             readBuffer[12],readBuffer[13],&readBuffer[42], 
                             &readBuffer[50], &readBuffer[34],
                             &readBuffer[18], &readBuffer[26],
                             recvlen, an_ip,
                             a_user_ptr->call, source_mod);

                     /* If it is from a remote reflector, convert to our data */
                     if (a_user_ptr->is_xrf)
                     {
                        /* set rpt2 */
                        memcpy(readBuffer + 26, OWNER, OWNER_SIZE);
                        readBuffer[33] = 'G';

                        /* set rpt1 */
                        memcpy(readBuffer + 18, OWNER, 7);
                        
                        /* remote band of reflector */
                        if (readBuffer[25] == 'A')
                           k = 0;
                        else
                        if (readBuffer[25] == 'B')
                           k = 1;
                        else
                        if (readBuffer[25] == 'C')
                           k = 2;
                        else
                        if (readBuffer[25] == 'D')
                           k = 3;
                        else
                           k = -1;

                        if (k >= 0)
                        {
                           for (i = 0; i < 5; i++)
                           {
                              if (a_user_ptr->rpt_mods[i][k] == readBuffer[25])
                              {
                                 if (i == 0)
                                    readBuffer[25] = 'A';
                                 else
                                 if (i == 1)
                                    readBuffer[25] = 'B';
                                 else
                                 if (i == 2)
                                    readBuffer[25] = 'C';
                                 else
                                 if (i == 3)
                                    readBuffer[25] = 'D';
                                 else
                                    readBuffer[25] = 'E';

                                 break;
      
                              }
                           }
                           if (i == 5)
                              readBuffer[25] = 'X';  /* conversion failed */
                        }
                        else
                           readBuffer[25] = 'X';  /* conversion failed */
                     }
                     else
                     {
                        /* the crazy shit */

                        /* replace the rpt1 name and rpt2 name with our callsign */
                        /* leave the module of the remote system untouched */

                        if (readBuffer[33] == 'G')
                        {
                           memcpy(readBuffer + 18, OWNER, 7);
                           memcpy(readBuffer + 26, OWNER, 7);
                        }
                        else
                        if (readBuffer[25] == 'G')
                        {
                           readBuffer[25] = readBuffer[33];
                           readBuffer[33] = 'G';
                           memcpy(readBuffer + 18, OWNER, 7);
                           memcpy(readBuffer + 26, OWNER, 7);
                        }
                        else 
                        {
                           memcpy(readBuffer + 18, OWNER, 7);
                           memcpy(readBuffer + 26, OWNER, 7);
                           readBuffer[33] = 'G';
                        }
                     }

                     memcpy(tmp1, readBuffer + 42, 8); tmp1[8] = '\0';

                     // find if user exists in lh_list
                     lh_pos = lh_list.find(tmp1);
                     if (lh_pos != lh_list.end())
                     {
                        // user exists in lh_list

                        // get the data from lh_list
                        strcpy(tmp2, lh_pos->second.c_str());

                        // find item in dt_lh_list and erase it
                        dt_lh_pos = dt_lh_list.find(tmp2);
                        if (dt_lh_pos != dt_lh_list.end())
                           dt_lh_list.erase(dt_lh_pos);

                        // update lh_list
                        time(&tNow);
                        sprintf(tmp2, "%ld=%.6s%c%c", tNow,  a_user_ptr->call, source_mod, readBuffer[25]);
                        lh_list[tmp1] = tmp2;

                        // add item to dt_lh_list
                        dt_lh_list[tmp2] = tmp1;
                        if (dt_lh_list.size() > LH_MAX_SIZE)
                        {
                           dt_lh_pos = dt_lh_list.begin();
                           if (dt_lh_pos != dt_lh_list.end())
                              dt_lh_list.erase(dt_lh_pos);
                        }
                     }
                     else
                     {
                        // user does NOT exist in lh_list

                        // Are we getting over the limit?
                        if (lh_list.size() ==  LH_MAX_SIZE)
                        {
                           // get the first item in dt_lh_list and extract callsign
                           dt_lh_pos = dt_lh_list.begin();
                           if (dt_lh_pos != dt_lh_list.end())
                           {
                              // find that callsign in lh_list and delete it
                              strcpy(tmp1, dt_lh_pos->second.c_str());
                              lh_pos = lh_list.find(tmp1);
                              if (lh_pos != lh_list.end())
                                 lh_list.erase(tmp1);
                              else
                              {
                                 lh_pos = lh_list.begin();
                                 if (lh_pos != lh_list.end())
                                    lh_list.erase(lh_pos);
                              }
                           }
                           else
                           {
                              lh_pos = lh_list.begin();
                              if (lh_pos != lh_list.end())
                                 lh_list.erase(lh_pos);
                           }
                        }

                        // add item to lh_list
                        memcpy(tmp1, readBuffer + 42, 8); tmp1[8] = '\0';

                        time(&tNow);
                        sprintf(tmp2, "%ld=%.6s%c%c", tNow,  a_user_ptr->call, source_mod, readBuffer[25]);
                        lh_list[tmp1] = tmp2;

                        // add item to dt_lh_list
                        dt_lh_list[tmp2] = tmp1;
                        if (dt_lh_list.size() > LH_MAX_SIZE)
                        {
                           dt_lh_pos = dt_lh_list.begin();
                           if (dt_lh_pos != dt_lh_list.end())
                              dt_lh_list.erase(dt_lh_pos);
                        }
                     }

                     /* This user is talking on this module */
                     a_user_ptr->mod = readBuffer[25];

                     if (readBuffer[25] == 'E')
                     {
                        rcd_pos = rcd_list.find(an_rcd_streamid);
                        if (rcd_pos == rcd_list.end())
                        {
                           if (rcd_list.size() < MAX_RCD_USERS)
                           {
                              an_rcd = (struct rcd *)malloc(sizeof(struct rcd));
                              if (an_rcd)
                              {
                                 an_rcd->locked = false;
                                 time(&(an_rcd->ts)); /* stamp it */
                                 memcpy((char *)&(an_rcd->sin), (char *)&(a_user_ptr->sin), sizeof(struct sockaddr_in));
                                 memcpy(an_rcd->data[0], readBuffer, recvlen);
                                 an_rcd->recvlen = recvlen;
                                 an_rcd->idx = 1;
                                 rcd_insert_pair = rcd_list.insert(pair<string, struct rcd *>(an_rcd_streamid, an_rcd));
                                 if (rcd_insert_pair.second)
                                    traceit("Started recording for user %.8s, streamid=%d,%d\n",
                                            readBuffer + 42, readBuffer[12], readBuffer[13]);
                                 else
                                 {
                                    traceit("failed to add user %.8s, streamid=%d,%d, for recording...\n",
                                            readBuffer + 42, readBuffer[12], readBuffer[13]);
                                    free(an_rcd);
                                    an_rcd = NULL;
                                 }
                              }
                              else
                                 traceit("Failed to allocate for recording on user %.8s, streamid=%d,%d\n",
                                         readBuffer + 42, readBuffer[12], readBuffer[13]);
                           }
                           else
                              traceit("Reached maximum concurrent users for recording\n");
                        }
                     }
                  }
                  else
                  {
                     if ((readBuffer[14] & 0x40) != 0)
                     {
                        if (QSO_DETAILS)
                           traceit("END user: streamID=%d,%d, %d bytes from IP=%s\n",
                                    readBuffer[12],readBuffer[13],recvlen, an_ip);
                     }

                     for (i = 0; i < 4; i++)
                     {
                        if ((readBuffer[12] == temp_r[i].hdr[14]) &&
                            (readBuffer[13] == temp_r[i].hdr[15]))
                        {
                           temp_r[i].last_time = time(NULL);

                           if ((readBuffer[14] & 0x40) != 0)
                           {
                              memset(temp_r[i].hdr, 0, 58);
                              temp_r[i].last_time = 0;
                           }
                           break;
                        }
                     }

                     rcd_pos = rcd_list.find(an_rcd_streamid);
                     if (rcd_pos != rcd_list.end())
                     {
                        an_rcd = (struct rcd *)rcd_pos->second;
                        time(&(an_rcd->ts)); /* stamp it */
                        if ((an_rcd->idx < MAX_RCD_DATA) && !(an_rcd->locked))
                        {
                           memcpy(an_rcd->data[an_rcd->idx], readBuffer, recvlen);
                           an_rcd->idx ++;
                        }
                        if (((readBuffer[14] & 0x40) != 0) && !(an_rcd->locked))
                        {
                           traceit("Finished recording for user %.8s, streamid=%d,%d\n",
                                    &(an_rcd->data[0][42]), readBuffer[12], readBuffer[13]);

                           /*** start playback */
                           an_rcd->locked = true;
                           pthread_attr_init(&attr);
                           pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
                           rc = pthread_create(&playback_thread,&attr,playback,(void *)rcd_pos->second);
                           if (rc != 0)
                           {
                              traceit("Failed to start the playback thread for streamid=%d,%d\n",
                                       readBuffer[12], readBuffer[13]);
                              free(rcd_pos->second);
                              rcd_pos->second = NULL;
                              rcd_list.erase(rcd_pos);
                           }
                           pthread_attr_destroy(&attr);
                        }
                     }
                  }

                  /* prepare to send the data to dvap, dvtool stations */
                  refbuf[0] = (unsigned char)((recvlen + 2) & 0xFF);
                  refbuf[1] = (unsigned char)((recvlen + 2) >> 8 & 0x1F);
                  refbuf[1] = (unsigned char)(refbuf[1] | 0xFFFFFF80);
                  memcpy(refbuf + 2, readBuffer, recvlen);

                  /* At this point, if this is a header, it contains our reflector info */
                  /* save the header for re-transmit */
                  if (recvlen == 56)
                  {
                     /* save it */
                     source_mod = refbuf[27];

                     i = -1;
                     if (refbuf[27] == 'A')
                        i = 0;
                     else
                     if (refbuf[27] == 'B')
                        i = 1;
                     else
                     if (refbuf[27] == 'C')
                        i = 2;
                     else
                     if (refbuf[27] == 'D')
                        i = 3;

                     if (i >= 0)
                     {
                        if (temp_r[i].last_time == 0)
                        {
                           memcpy(temp_r[i].hdr, refbuf, 58);
                           temp_r[i].last_time = time(NULL);
                        }
                     }
                  }

                  /* send the data to dvap, dvtool stations */
                  if ((inbound_list.size() > 0) && (rcd_list.find(an_rcd_streamid) == rcd_list.end()))
                  {
                     for (inbound_pos2 = inbound_list.begin(); inbound_pos2 != inbound_list.end(); inbound_pos2++)
                     {
                        inbound_ptr2 = (inbound *)inbound_pos2->second;
                        if ((recvlen == 56) && found)
                        {
                           if (inbound_ptr2->is_ref)
                           {
                              if (i >= 0)
                              {
                                 if (inbound_ptr2->links[i] != ' ')
                                 {
                                    memcpy(refbuf + 20, inbound_ptr2->call, 7);
                                    refbuf[27] = inbound_ptr2->links[i];
                                    memcpy(refbuf + 28, inbound_ptr2->call, 7);

                                    sendto(ref_sock, (char *)refbuf, recvlen + 2, 0,
                                           (struct sockaddr *)&(inbound_ptr2->sin),
                                           sizeof(struct sockaddr_in));
                                 }
                              }
                           }
                           else
                           {
                              /* reset it */
                              memcpy(refbuf + 20, OWNER, 7);
                              refbuf[27] = source_mod;
                              memcpy(refbuf + 28, OWNER, 7);

                              if (i >= 0)
                                 sendto(ref_sock, (char *)refbuf, recvlen + 2, 0,
                                        (struct sockaddr *)&(inbound_ptr2->sin),
                                        sizeof(struct sockaddr_in));
                           }
                        }
                        else if (found)
                        {
                           if ((refbuf[26] == 0x55) && (refbuf[27] == 0x2d) && (refbuf[28] == 0x16))
                           {
                              if (!inbound_ptr2->is_ref)
                              {
                                 for (i = 0; i < 4; i++)
                                 {
                                    if ((refbuf[14] == temp_r[i].hdr[14]) &&
                                        (refbuf[15] == temp_r[i].hdr[15]))
                                    {
                                       sendto(ref_sock, (char *)temp_r[i].hdr, 58, 0,
                                              (struct sockaddr *)&(inbound_ptr2->sin), sizeof(struct sockaddr_in));
                                       break;
                                    }
                                 }
                              }
                           }

                           /* if it is a regular dongle, send it */
                           if (!inbound_ptr2->is_ref)
                              sendto(ref_sock, (char *)refbuf, recvlen + 2, 0,
                                     (struct sockaddr *)&(inbound_ptr2->sin),
                                     sizeof(struct sockaddr_in));
                           else
                           {
                              /* treat the remote reflector with resepct */
                              /* send it only if the module matches */
                              for (i = 0; i < 4; i++)
                              {
                                 if ((refbuf[14] == temp_r[i].hdr[14]) &&
                                     (refbuf[15] == temp_r[i].hdr[15]) &&
                                     (inbound_ptr2->links[i] != ' '))
                                 {
                                    sendto(ref_sock, (char *)refbuf, recvlen + 2, 0,
                                           (struct sockaddr *)&(inbound_ptr2->sin),
                                           sizeof(struct sockaddr_in));

                                    break;
                                 }
                              }
                           }
                        }
                     }
                  }

                  if (found)
                  {
                  /* send the data to the other repeaters */
                  if (rcd_list.find(an_rcd_streamid) == rcd_list.end())
                     send_a_pkt_to_all(fromUser.sin_addr.s_addr, recvlen, false);
                  }
               }
            }
         }
         FD_CLR (srv_sock,&fdset);
      }
   }
}

static void *playback(void *arg)
{
   struct rcd *temp_rcd = (struct rcd *)arg;
   struct sigaction act;
   unsigned short int j;
   unsigned short int i;
   struct timespec nanos;

   act.sa_handler = sigCatch;
   sigemptyset(&act.sa_mask);
   act.sa_flags = SA_RESTART;
   if (sigaction(SIGTERM, &act, 0) != 0)
   {
      traceit("sigaction-TERM failed, error=%d\n", errno);
      keep_running = false;
      pthread_exit(NULL);
   }
   if (sigaction(SIGINT, &act, 0) != 0)
   {
      traceit("sigaction-INT failed, error=%d\n", errno);
      keep_running = false;
      pthread_exit(NULL);
   }

   /* delay 5 seconds here */
   sleep(5);

   if (temp_rcd->recvlen == 56)
      traceit("playback started for user=%.8s, streamid %d,%d\n", 
            &(temp_rcd->data[0][42]), temp_rcd->data[0][12], temp_rcd->data[0][13]);
   else
      traceit("playback started for user=%.8s, streamid %d,%d\n",
            &(temp_rcd->data[0][44]), temp_rcd->data[0][14], temp_rcd->data[0][15]);


   /* first packet is header */

   /*** MUST CHANGE rpt1 and rpt2 if temp_rcd->recvlen is 58 ***/
   for (j = 0; j < 5; j++)
      sendto(ref_sock, (char *)temp_rcd->data[0], temp_rcd->recvlen, 0,
             (struct sockaddr *)&(temp_rcd->sin),
             sizeof(struct sockaddr_in));

   /*** header sent ***/
   i = 1;
   
   while ((keep_running) && (i < temp_rcd->idx))
   {
      time(&(temp_rcd->ts));
      sendto(ref_sock, (char *)temp_rcd->data[i], 
             (temp_rcd->recvlen == 56)?27:29,   /*** ? 32 ***/
             0,(struct sockaddr *)&(temp_rcd->sin),
             sizeof(struct sockaddr_in));
      i ++;
      nanos.tv_sec = 0;
      nanos.tv_nsec = 17000000;
      nanosleep(&nanos,0);
   }

   if (temp_rcd->recvlen == 56)
      traceit("Finished playback for user=%.8s, streamid %d,%d\n",
            &(temp_rcd->data[0][42]), temp_rcd->data[0][12], temp_rcd->data[0][13]);
   else
      traceit("Finished playback for user=%.8s, streamid %d,%d\n",
            &(temp_rcd->data[0][44]), temp_rcd->data[0][14], temp_rcd->data[0][15]);

   temp_rcd->locked = false;

   pthread_exit(NULL);
}

int main(int argc, char **argv)
{
   int rc = 0;
   struct sigaction act;
   inbound_type::iterator inbound_pos; 
   inbound *inbound_ptr;
   short int i;

   if (argc != 3)
   {
      printf("Usage:  dxrfd <configFile> <logFile>\n");
      printf("Example: dxrfd dxrfd.cfg dxrfd.log\n");
      return 1;
   }

   if (access(argv[2], F_OK) == 0)
   {
      printf("Log file %s exists, is dxrfd already running\n",argv[2]);
      return 1;
   }

   /* Open log file */
   logfp = fopen(argv[2], "a");
   if (!logfp)
   {
      printf("Cant create log file %s, error=%d\n",
              argv[2],errno);
      return 1;
   }

   tzset();

   /* line buffering only */
   setvbuf(logfp, (char *)NULL, _IOLBF, 0);

   act.sa_handler = sigCatch;
   sigemptyset(&act.sa_mask);
   act.sa_flags = SA_RESTART;
   if (sigaction(SIGTERM, &act, 0) != 0)
   {
      traceit("sigaction-TERM failed, error=%d\n", errno);
      return 1;
   }
   if (sigaction(SIGINT, &act, 0) != 0)
   {
      traceit("sigaction-INT failed, error=%d\n", errno);
      return 1;
   }

   do
   {
      /* process configuration file */
      rc = read_config(argv[1]);
      if (rc != 0)
      {
         traceit("Failed to process config file %s\n", argv[1]);
         break;
      }

    /* Open DB */
      rc = open_users(USERS);
      if (rc != 0)
      {
         traceit("Failed to open %s\n", USERS);
         break;
      }

      /* get ready to accept incoming connections */
      rc = srv_open();
      if (rc != 0)
      {
         traceit("srv_open() failed\n");
         break;
      }

      /* get ready to accept shell commands from netcat */
      rc = cmd_open();
      if (rc != 0)
      {
         traceit("cmd_open() failed\n");
         break;
      }

      rc = ref_open();
      if (rc != 0)
      {
         traceit("ref_open() failed\n");
         break;
      }
  
      for (i = 0; i < 5; i++)
      {
         memset(temp_x[i].hdr, 0, 56);
         temp_x[i].last_time = 0;

         memset(temp_r[i].hdr, 0, 58);
         temp_r[i].last_time = 0;
      }

      traceit("dxrfd %s initialized...entering processing loop\n", VERSION);

      print_links_file();
      runit();
      traceit("Leaving processing loop...\n");

   } while (false);

   /* Close all handles */
   if (srv_sock != -1)
      close(srv_sock);

   if (cmd_sock != -1)
      close(cmd_sock);

   /* notify dvap, dvtool ... */
   if (ref_sock != -1)
   {
      refbuf[0] = 5;
      refbuf[1] = 0;
      refbuf[2] = 24;
      refbuf[3] = 0;
      refbuf[4] = 0;
      for (inbound_pos = inbound_list.begin(); inbound_pos != inbound_list.end(); inbound_pos++)
      {
         inbound_ptr = (inbound *)inbound_pos->second;
         sendto(ref_sock,(char *)refbuf,5,0,
                (struct sockaddr *)&(inbound_ptr->sin),
                sizeof(struct sockaddr_in));
      }
      inbound_list.clear();
      close(ref_sock);
   }

   /* Close database */
   if (dbp != NULL)
   {
      dbp->close(dbp, 0);
      traceit("Closed database %s\n",USERS);
   }

   /* Empty out the status file */
   statusfp = fopen(STATUS_FILE, "w");
   if (statusfp)
      fclose(statusfp);
   else
      traceit("Failed to empty out status file %s\n", STATUS_FILE);

   traceit("dxrfd exiting\n");
   fclose(logfp);
 
   return rc;
}

