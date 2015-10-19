/*************************************************************************

   Program:    qlsubmit / qlrun
   File:       qlutil.c
   
   Version:    V1.0
   Date:       15.09.00
   Function:   Utility routines for QLite
   
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
   Create the spool directory with sticky bit set and world writable

**************************************************************************

   Revision History:
   =================

*************************************************************************/
/* Includes
*/
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#define __STRICT_ANSI__
#include <netdb.h>
#undef __STRICT_ANSI__
#ifdef __linux__
#  include <linux/limits.h>
#else
#  include <limits.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <arpa/inet.h>

#include "qlutil.h"

/************************************************************************/
/* Defines and macros
*/
#define TIMEOUT 10
#define MAXBUFF 160

/************************************************************************/
void UpdateSpoolDir(char *spoolDir, int cluster)
{
   char clusterstring[32];
   
   if(cluster!=0)
   {
      sprintf(clusterstring,"%d", cluster);
      strcat(spoolDir, "/");
      strcat(spoolDir, clusterstring);
   }
}

/************************************************************************/
/*>BOOL CheckForSpoolDir(char *spoolDir)
   -------------------------------------
   Input:     
   Output:    
   Returns:   

   14.09.00 Original   By: ACRM
*/
BOOL CheckForSpoolDir(char *spoolDir)
{
   if(access(spoolDir, F_OK))
      return(FALSE);
   
   return(TRUE);
}


/************************************************************************/
/*>void DeleteLockFile(char *spoolDir)
   -----------------------------------
   Input:     
   Output:    
   Returns:   

   14.09.00 Original   By: ACRM
*/
void DeleteLockFile(char *spoolDir)
{
   char   lockfile[MAXBUFF];
   FILE   *fp;
   ULONG  oldpid;
   
   sprintf(lockfile, "%s/.qllockfile", spoolDir);

   if((fp=fopen(lockfile,"r"))==NULL)
      return;
   fscanf(fp, "%ld\n", &oldpid);
   fclose(fp);

   if(oldpid == (ULONG)getpid())
      unlink(lockfile);
}

/************************************************************************/
/*>int CreateLockFile(char *spoolDir)
   ----------------------------------
   Input:     
   Output:    
   Returns:   int          0: OK
                           1: timed out
                           2: error in writing lock file

   14.09.00 Original   By: ACRM
   03.10.00 Puts the host name in the lock file
*/
int CreateLockFile(char *spoolDir)
{
   char   lockfile[MAXBUFF],
          hname[MAXBUFF];
   time_t startTime, nowTime;
   FILE   *fp;
   
   sprintf(lockfile, "%s/.qllockfile", spoolDir);
   time(&startTime);

   while(!access(lockfile, F_OK))
   {
      time(&nowTime);
      if((nowTime - startTime) > TIMEOUT)
         return(1);
      sleep(1);
   }

   if((fp=fopen(lockfile,"w"))==NULL)
      return(2);
   if(gethostname(hname,MAXBUFF))
      return(2);
   fprintf(fp, "%ld\n", (ULONG)getpid());
   fprintf(fp, "%s\n", hname);
   fclose(fp);
   
   return(0);
}

/************************************************************************/
void CreateFullPath(char *file)
{
   char buffer[PATH_MAX];
   
   /* Just return if it's already a full path                           */
   if(file[0] == '/')
      return;
   
   /* Get the current directory and append the file name                */
   getcwd(buffer, PATH_MAX);
   strcat(buffer,"/");
   strcat(buffer,file);
   
   /* Copy it back into the original variable                           */
   strncpy(file, buffer, PATH_MAX);
}

/************************************************************************/
/*>BOOL RootUser(void)
   -------------------
   Returns:   BOOL           Is this being run by root?

   Tests whether this is the root user?

   18.09.00 Original   By: ACRM
*/
BOOL RootUser(void)
{
   uid_t uid;
   
   uid = getuid();
   
   if(uid==(uid_t)0)
      return(TRUE);

   return(FALSE);
}


/************************************************************************/
/*>RUNFILE *ReadMachineList(char *spoolDir)
   ----------------------------------------
   Input:   char   *spoolDir      Spool Directory
   Returns: RUNFILE *             Linked list of runfiles and node names

   Reads the file .machinelist from the spool directory to get a list
   of machines in the farm

   22.09.00 Original   By: ACRM
   02.10.00 Modified to allow multiple instances 
*/
RUNFILE *ReadMachineList(char *spoolDir)
{
   char    file[PATH_MAX],
           buffer[PATH_MAX],
           machine[MAXBUFF],
           *chp;
   RUNFILE *runfiles = NULL,
           *r;
   FILE    *fp;
   int     instance;
   

   sprintf(file,"%s/.machinelist", spoolDir);
   if((fp=fopen(file,"r"))!=NULL)
   {
      while(fgets(buffer,MAXBUFF,fp)!=NULL)
      {
         if((buffer[0] != '!') && (buffer[0] != '#'))
         {
            TERMINATE(buffer);
            KILLLEADSPACES(chp, buffer);
            KILLTRAILSPACES(chp);
            if(runfiles==NULL)
            {
               INIT(runfiles, RUNFILE);
               r = runfiles;
            }
            else
            {
               ALLOCNEXT(r, RUNFILE);
            }

            if((sscanf(chp, "%s %d", machine, &instance))!=2)
            {
               fprintf(stderr,"Warning: Error in format of machine list \
file: %s\n", file);
               return(NULL);
            }
            
            sprintf(r->file, "%s/.%s.running.%d", 
                    spoolDir, machine, instance);
            strncpy(r->node, chp, MAXBUFF);
         }
      }
   }
      
   return(runfiles);
}


/************************************************************************/
/*>BOOL DaemonInit(void)
   ---------------------
   Returns:   BOOL           Success?

   Initialization to detach itself and rin as a daemon

   15.09.00 Original  By: ACRM
*/
BOOL DaemonInit(void)
{
   pid_t pid;

   if((pid=fork()) < 0)       /* Fork a new process                     */
      return(FALSE);
   else if(pid != 0)          /* Kill the parent                        */
      exit(0);

   setsid();                  /* Create a new session                   */
   chdir("/");                /* Change to the root directory           */
   umask(0);                  /* Clear the default UMASK                */

   return(TRUE);
}


/************************************************************************/
/*>int atoport(char *service, char *proto)                             
   -----------------------------------                                 
   Input:     char     *service  /etc/services entry                   
              char     *proto    protocol (tcp/udp)                    
   Returns:   int      port                                            
                                                                       
   from sockhelp.c of the usenet sockets faq examples                 
   Take a service name, and a service type, and return a port number.  
   If the service name is not found, it tries it as a decimal number.  
   The number returned is byte ordered for the network.                
                                                                       
   20.01.96 Original   By: Vic Metcalfe                                
*/
int atoport(char *service, char *proto)
{
   int port;
   long int lport;
   struct servent *serv;
   char *errpos;
   
   /* First try to read it from /etc/services                           */
   serv = getservbyname(service, proto);
   if (serv != NULL)
   {
      port = serv->s_port;
   }
   else 
   { /* Not in services, maybe a number?                                */
      lport = strtol(service,&errpos,0);
      if ( (errpos[0] != 0) || (lport < 1) || (lport > 65535) )
         return -1; /* Invalid port address                             */
      port = htons(lport);
   }

   return port;
}


void GetPortAndLockHost(char *spoolDir, int *port, char *lockhost)
{
   FILE *fp;
   char lockhostfile[PATH_MAX],
        fileLockhost[MAXBUFF];
   int  filePort;

   sprintf(lockhostfile, "%s/.qllockdaemon", spoolDir);

   if((fp=fopen(lockhostfile, "r"))!=NULL)
   {
      fscanf(fp, "%s %d", fileLockhost, &filePort);
      if(!lockhost[0])
         strcpy(lockhost, fileLockhost);
      if(*port == 0)
         *port = filePort;

      fclose(fp);
   }
}
