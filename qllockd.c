/*************************************************************************

   Program:    qllockd
   File:       qllockd.c
   
   Version:    V1.0
   Date:       04.10.00
   Function:   Daemon to handle locking between machines in the cluster
   
   Copyright:  (c) University of Reading / Dr. Andrew C. R. Martin 2000
   Author:     Dr. Andrew C. R. Martin
   Address:    School of Animal and Microbial Sciences,
               The University of Reading,
               Whiteknights,
               P.O. Box 228,
               Reading RG6 6AJ.
               England.
   Phone:      +44 (0)118 987 5123 Extn. 7022
   Fax:        +44 (0)118 931 0180
   EMail:      andrew@bioinf.org.uk
               andrew@stagleys.demon.co.uk
               
**************************************************************************

   This program is not in the public domain.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

   See the file COPYING.DOC for details of what you may and may not
   do with this program.

   In particular, you may not distribute this code without express
   permission from the author; it must be obtained directly from the 
   author.

**************************************************************************

   Description:
   ============

**************************************************************************

   Usage:
   ======

**************************************************************************

   Revision History:
   =================
   V1.0  04.10.00  Original   By: ACRM

*************************************************************************/
/* Includes
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#define __STRICT_ANSI__
#include <netdb.h>
#undef __STRICT_ANSI__
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#ifdef __linux__
#  include <linux/limits.h>
#else
#  include <limits.h>
#endif

#include "qlutil.h"

/************************************************************************/
/* Defines and macros
*/
#define QUELEN 5000
#define SERVICENAME "qlite"

#define STATUS_UNLOCKED 0
#define STATUS_LOCKED   1

/************************************************************************/
/* Globals
*/
char gLockholder[MAXBUFF];
int  gClientID = 0,
     gDebug    = 0;


/************************************************************************/
/* Prototypes
*/
int main(int argc, char **argv);
void error(void);
BOOL ParseCmdLine(int argc, char **argv, char *spooldir, int *port);
int CreateBoundListeningSocket(char *service, int port);
BOOL ValidMachine(RUNFILE *runfiles, int socket, 
                  struct sockaddr_in client, char *hostname);
int AcceptConnections(RUNFILE *runfiles, int s);
void HandleCommand(int sock, char *line, char *clientHostname);
BOOL CorrectMachine(char *clientHostname, int clientID);
void Usage(void);
void WaitForOtherChars(int sock);

/************************************************************************/
int main(int argc, char **argv)
{
   int port = 0, 
      s;
   char spoolDir[PATH_MAX];
   RUNFILE *runfiles = NULL;

   strcpy(spoolDir, DEF_SPOOLDIR);

   if(ParseCmdLine(argc, argv, spoolDir, &port))
   {
#ifndef DEBUG
      if(!RootUser())
      {
         fprintf(stderr,"qllockd must be run by the root user\n");
         return(1);
      }
      else
#endif
      {
         if(!gDebug)
            DaemonInit();
         
         runfiles = ReadMachineList(spoolDir);
         
         if((s = CreateBoundListeningSocket(SERVICENAME, port)) < 0)
            error();
         
         AcceptConnections(runfiles, s);
      }
   }
   else
   {
      Usage();
   }

   return(0);
   
}


/************************************************************************/
int CreateBoundListeningSocket(char *service, int port)
{
   int s;
   struct sockaddr_in sockin;
   
   /* Step 1 - create a socket                                          */
   if ((s=socket(AF_INET,SOCK_STREAM,0)) < 0)
      return(s);

   /* Step 2 - Set up the address                                       */
   if(port == 0)
   {
      if((port = atoport(service, "tcp")) < 0)
      {
         port = DEFAULT_QLPORT;
         port = htons(port);
      }
   }
   else
   {
      port = htons(port);
   }
   sockin.sin_family=AF_INET;
   sockin.sin_port=port;
   sockin.sin_addr.s_addr=htonl(INADDR_ANY);
   
   /* Step 3 - bind address to the port                                 */
   if (bind(s, &sockin, sizeof(sockin)) < 0)
      return(-1);

   /* Step 4 - Listen for connections                                   */
   if (listen(s,QUELEN) < 0) 
      return(-1);
   
   return(s);
}

/************************************************************************/
int AcceptConnections(RUNFILE *runfiles, int s)
{
   struct sockaddr_in client;
   unsigned int       len;
   int                g, i;
   char               c, 
                      line[MAXBUFF],
                      clientHostname[MAXBUFF];
   
   
   for (;;) 
   {
      if ((g=accept(s,&client,&len)) < 0) 
         return(-1);
      
      if(ValidMachine(runfiles, g, client, clientHostname))
      {
         i=0;
         while(read(g, &c, 1)) 
         {
            if(isalnum(c) || (c == '.') || (c == ' '))
            {
               line[i++] = c;
            
               if(c == '.')
               {
                  line[i-1] = '\0';
                  
                  if(gDebug)
                     printf("Command: %s\n", line);
                  
                  HandleCommand(g, line, clientHostname);
                  WaitForOtherChars(g);
                  break;
               }
            }
         }
      }

      /* Close the (parent) socket connection                           */
      close(g);
   }
}



/************************************************************************/
BOOL GetClientID(int socket, struct sockaddr_in client, char *hostname)
{
   unsigned int   len;
   struct hostent *host;
   char           *chp;

   len = sizeof client;
   
   if (getpeername(socket, (struct sockaddr *) &client, &len) < 0)
   {
      return(FALSE);
   }
   else 
   {
      if ((host = gethostbyaddr((char *) &client.sin_addr,
                                sizeof client.sin_addr,
                                AF_INET)) == NULL)
      {
         return(FALSE);
      }
      else 
      {
         if(gDebug)
            printf("Got connection from remote host is '%s'\n", 
                   host->h_name);

         strcpy(hostname, host->h_name);
         if((chp=strchr(hostname,'.'))!=NULL)
            *chp = '\0';
      }
   }
   return(TRUE);
}


/************************************************************************/
BOOL ValidMachine(RUNFILE *runfiles, int socket, 
                  struct sockaddr_in client, char *hostname)
{
   RUNFILE *r;
   
   
   if(GetClientID(socket, client, hostname))
   {
      for(r=runfiles; r!=NULL; NEXT(r))
      {
         char allowedhost[MAXBUFF];
         int  hostid;
         
         sscanf(r->node,"%s %d", allowedhost, &hostid);
         if(!strcmp(allowedhost, hostname))
         {
            if(gDebug)
               printf("remote host is allowed to talk!\n");
            return(TRUE);
         }
      }
   }
   
   return(FALSE);
}

/************************************************************************/
void HandleCommand(int sock, char *line, char *clientHostname)
{
   static int status = STATUS_UNLOCKED;
   int id;
   
   if(!strncmp(line,"STATUS",6))
   {
      char buffer[80];
      sprintf(buffer,"Status = %d\n", status);
      printf("%s",buffer);
      write(sock,buffer,strlen(buffer)+1);
   }
   else if(!strncmp(line,"GETLOCK",7))
   {
      if((sscanf(line,"%*s %d", &id))!=1)
      {
         if(gDebug)
            printf("Error in command: %s\n",line);
         write(sock,"ERROR.\n",8);
      }
      else
      {
         if(status == STATUS_LOCKED)
         {
            if(gDebug)
               printf("Already locked\n");
            write(sock,"DENIED.\n",9);
         }
         else
         {
            status = STATUS_LOCKED;
            strcpy(gLockholder, clientHostname);
            gClientID = id;
            if(gDebug)
               printf("Granted lock\n");
            write(sock,"OK.\n",5);
         }
      }
   }
   else if(!strncmp(line,"RELEASELOCK",11))
   {
      if((sscanf(line,"%*s %d", &id))!=1)
      {
         if(gDebug)
            printf("Error in command: %s\n",line);
         write(sock,"ERROR.\n",8);
      }
      else
      {
         if(status == STATUS_LOCKED)
         {
            if(CorrectMachine(clientHostname, id))
            {
               status = STATUS_UNLOCKED;
               
               if(gDebug)
                  printf("Released lock\n");
               write(sock,"OK.\n",5);
            }
            else
            {
               if(gDebug)
                  printf("Lock release denied\n");
               write(sock,"DENIED.\n",9);
            }
         }
         else
         {
            if(gDebug)
               printf("Lock release requested, but no lock\n");
            write(sock,"ERROR.\n",8);
         }
      }
   }
}

/************************************************************************/
BOOL CorrectMachine(char *clientHostname, int clientID)
{
   if(!strcmp(clientHostname, gLockholder) && (clientID == gClientID))
      return(TRUE);
   return(FALSE);
}

/************************************************************************/
/*>BOOL ParseCmdLine(int argc, char **argv, char *spooldir, int *port)
   -------------------------------------------------------------------
   Input:     int   argc         Argument count
              char  **argv       Arguments
   Output:    char  *spooldir    Spool directory
              int   *port        Cluster number
   Returns:   BOOL               Success?

   Parses the command line

   04.10.00 Original   By: ACRM
*/
BOOL ParseCmdLine(int argc, char **argv, char *spooldir, int *port)
{
   argc--;
   argv++;

   *port = 0;

   while(argc)
   {
      if(argv[0][0] == '-')
      {
         switch(argv[0][1])
         {
         case 'd':
            gDebug = 1;
            break;
         case 'p':
            argv++;
            argc--;
            if(!argc)
               return(FALSE);
            if(!sscanf(argv[0],"%d", port))
               return(FALSE);
            break;
         default:
            return(FALSE);
            break;
         }
      }
      else
      {
         if(argc > 0)
            return(FALSE);
      }
      
      argc--;
      argv++;
   }
   
   return(TRUE);
}


/************************************************************************/
void WaitForOtherChars(int sock)
{
   char c;
#ifdef REQUIRE_ACK
   char line[MAXBUFF],
   int  i=0;
#endif
   
   while(read(sock, &c, 1))
   {
#ifdef REQUIRE_ACK
      if(isalnum(c) || (c=='.') || (c == ' '))
      {
         line[i++] = c;
         
         if(c == '.')
         {
            line[i-1] = '\0';
            
            if(gDebug)
               printf("WaitForOtherChars() got: %s\n", line);
            
            if(strstr(line,"ACK") ||
               strstr(line,"QUIT"))
               break;

            i=0;
         }
      }
#endif
   }
}

/************************************************************************/
void error(void)
{
   fprintf(stderr,"Died!\n");
   exit(1);
}


/************************************************************************/
void Usage(void)
{
   fprintf(stderr,"\nqllockd V1.0 (c) Dr. Andrew C.R. Martin, University \
of Reading\n");

   fprintf(stderr,"\nUsage: qllockd [-p portnum] [-s spooldir]\n");
   fprintf(stderr,"       -p Specify the port number to listen on\n");
   fprintf(stderr,"       -s Specify the spool directory\n");

   fprintf(stderr,"\nqllockd listens on the specified port for requests \
for locks by qlrun\n");

   fprintf(stderr,"\nBy default, it listens on port %d. 'qlite' may be \
specified as a service\n", DEFAULT_QLPORT);
   fprintf(stderr,"in /etc/services and a different port may be \
specified there. The port\n");
   fprintf(stderr,"specified with -p takes priority over either of \
those.\n");

   fprintf(stderr,"\nqllockd only grants locks to machines listed in \
.machinelist in the\n");
   fprintf(stderr,"spool directory.\n");

   fprintf(stderr,"\nqllockd should only be run on one machine in a \
cluster (in fact it\n");
   fprintf(stderr,"may be run on a machine not in the cluster \
itself)\n\n");
}
