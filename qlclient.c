/*************************************************************************

   Program:    QLite
   File:       qlclient.c
   
   Version:    V1.0
   Date:       04.10.00
   Function:   Client routines for querying the qllockd daemon
   
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
   Create the spool directory as writable only by root

**************************************************************************

   Revision History:
   =================
   V1.0  04.10.00  Original   By: ACRM

*************************************************************************/
/* Includes
*/
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
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


/************************************************************************/
/* Globals
*/
static int                 sPort;
static struct in_addr      *sAddr;
static struct sockaddr_in  sAddress;


/************************************************************************/
/* Prototypes
*/
static struct in_addr *atoaddr(char *address);
static int CreateConnection(void);


/************************************************************************/
BOOL InitLocks(char *service, char *host, int port)
{
   /* Set up the port address                                           */
   if(port == 0)
   {
      if((sPort = atoport(service, "tcp")) < 0)
      {
         sPort = htons(DEFAULT_QLPORT);
      }
   }
   else
   {
      sPort = htons(port);
   }

   /* Get an address for the remote server                              */
   if((sAddr=atoaddr(host))==NULL)
      return(FALSE);

   /* Fill in an address structure for the remote server                */
   memset((char *) &sAddress, 0, sizeof(sAddress));
   sAddress.sin_family = AF_INET;
   sAddress.sin_port = sPort;
   sAddress.sin_addr.s_addr = sAddr->s_addr;

   
   return(TRUE);
}


/************************************************************************/
int GetLock(int id, int timeout)
{
   int    sock,
          i;
   char   c, 
          cmd[MAXBUFF],
          line[MAXBUFF];
   time_t startTime, nowTime;

   time(&startTime);
   
   for(;;)
   {
      if((sock = CreateConnection()) < 0)
         return(2);

      /* Send a GETLOCK command                                         */
      sprintf(cmd, "GETLOCK %d.\n", id); 
      write(sock, cmd, strlen(cmd)+1);

      /* Read the response                                              */
      i=0;
      while(read(sock, &c, 1)) 
      {
         if(c)
         {
            line[i++] = c;
            
            if (c == '.')
            {
               shutdown(sock,0); /* We've finished reading              */

#ifdef REQUIRE_ACK
               /* Send an acknowledgement                               */
               write(sock,"ACK.",4);
#endif

               line[i-1] = '\0';
               
#ifdef DEBUG
               printf("Response: %s\n", line);
#endif            
               
               if(!strncmp(line, "OK", 2))
               {
                  /* Close the socket connection                        */
                  close(sock);
                  return(0);
               }
               else if(!strncmp(line, "ERROR", 5))
               {
                  /* Close the socket connection                        */
                  close(sock);
                  return(3);
               }

               /* Should get here if the lock was denied                */
               break;
            }  /* if(c=='.')                                            */
         }  /* if(c)                                                    */
      }  /* while we read from socket                                   */

      /* Close the socket                                               */
      close(sock);
      sock = 0;

      /* If time out exceeded, then return                              */
      time(&nowTime);
      if((nowTime - startTime) > timeout)
         return(1);

      /* Pause before we try again                                      */
      sleep(1);
   }

   /* Should never get here!!                                           */
   if(sock!=0)
      close(sock);
   return(4);
}

/************************************************************************/
BOOL ReleaseLock(int id)
{
   int    sock,
          i;
   char   c, 
          cmd[MAXBUFF],
          line[MAXBUFF];
   
   if((sock = CreateConnection()) < 0)
      return(FALSE);

   /* Send a RELEASELOCK command                                        */
   sprintf(cmd, "RELEASELOCK %d.\n", id); 
   write(sock, cmd, strlen(cmd)+1);

   /* Read the response                                                 */
   i=0;
   while(read(sock, &c, 1)) 
   {
      if(c)
      {
         line[i++] = c;
         
         if (c == '.')
         {
            shutdown(sock,0); /* We've finished reading                 */
            
#ifdef REQUIRE_ACK
            /* Send an acknowledgement                                  */
            write(sock,"ACK.",4);
#endif

            line[i-1] = '\0';
            
#ifdef DEBUG
            printf("Response: %s\n", line);
#endif
            
            if(!strncmp(line, "OK", 2))
            {
               /* Close the socket connection                           */
               close(sock);
               return(TRUE);
            }
            break;
         }
      }
   }
   close(sock);

   return(FALSE);
}

/************************************************************************/
/*>static struct in_addr *atoaddr(char *address)
   ---------------------------------------------
   Input:     char     *address  host or IP                            
   Returns:   in_addr  saddr     in_addr struct for sockets code       
                                                                       
   Converts ascii text to in_addr struct.  NULL is returned if the     
   address can not be found.                                           
                                                                       
   20.01.96 Original   By: Vic Metcalfe                                
*/
static struct in_addr *atoaddr(char *address)
{
   struct hostent *host;
   static struct in_addr saddr;
   
   /* First try it as aaa.bbb.ccc.ddd.                                  */
   saddr.s_addr = inet_addr(address);

   if (saddr.s_addr != -1) 
   {
      return &saddr;
   }

   host = gethostbyname(address);

   if (host != NULL) 
   {
      return (struct in_addr *) *host->h_addr_list;
   }

   return NULL;
}

/************************************************************************/
static int CreateConnection(void)
{
   int s;

   /* Create a socket                                                   */
   if ((s=socket(AF_INET,SOCK_STREAM,0)) < 0)
      return(s);

   /* We have the address already set up, connect it to the socket      */
   if(connect(s, (struct sockaddr *)&sAddress, sizeof(sAddress)) < 0)
      return(-1);

   return(s);
}


/************************************************************************/
#ifdef DEMO
int main(int argc, char **argv)
{
   if(InitLocks("qlite", "sapc13", 5468))
   {
      if(GetLock(1, 5)==0)
         printf("Got lock!\n");
      else
         printf("Lock denied!\n");

/*
      if(GetLock(1, 5)==0)
         printf("Got lock!\n");
      else
         printf("Lock denied!\n");

      if(ReleaseLock(1))
         printf("Lock released!\n");
      else
         printf("Couldn't release lock!\n");

      if(GetLock(1, 5)==0)
         printf("Got lock!\n");
      else
         printf("Lock denied!\n");

      if(ReleaseLock(2))
         printf("Lock released!\n");
      else
         printf("Couldn't release lock!\n");
*/
   }

   return(0);
}
#endif

