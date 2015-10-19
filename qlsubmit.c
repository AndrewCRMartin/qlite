/*************************************************************************

   Program:    qlsubmit
   File:       qlsubmit.c
   
   Version:    V1.1
   Date:       15.09.00
   Function:   Submit jobs for farm processing
   
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
#ifdef __linux__
#  include <linux/limits.h>
#else
#  include <limits.h>
#endif
#include <sys/types.h>

#include "qlutil.h"

/************************************************************************/
/* Defines and macros
*/

/************************************************************************/
/* Globals
*/

/************************************************************************/
/* Prototypes
*/
int main(int argc, char **argv);
BOOL ParseCmdLine(int argc, char **argv, char *jobfile, BOOL *doDelete,
                  char *spoolDir, BOOL *quiet, int *cluster, int *nice,
                  char *lockhost, int *port);
ULONG SubmitJob(char *jobfile, char *spoolDir, uid_t uid, gid_t gid, 
                int nice);
void Usage(void);
BOOL WriteControlFile(char *jobfile, char *spoolDir, ULONG jobnum, 
                      uid_t uid, gid_t gid, int nice);

/************************************************************************/
/*>int main(int argc, char **argv)
   -------------------------------
   Main program for submitting jobs to a farm

   14.09.00 Original   By: ACRM
*/
int main(int argc, char **argv)
{
   uid_t uid;
   gid_t gid;
   char  jobfile[PATH_MAX],
         *env;
   BOOL  doDelete = FALSE,
         quiet = FALSE;
   int   status,
         cluster = 0,
         nice    = 10,
         port    = 0;
   ULONG jobnum;
   char  spoolDir[PATH_MAX],
         lockhost[MAXBUFF];
   
   /* Get the default spool directory from the environment variable if
      this has been set
   */
   if((env=getenv("QLSPOOLDIR"))!=NULL)
      strcpy(spoolDir, env);
   else
      strcpy(spoolDir, DEF_SPOOLDIR);

   /* Get the default cluster from the environment variable if this has 
      been set
   */
   if((env=getenv("QLCLUSTER"))!=NULL)
      sscanf(env,"%d", &cluster);

   if(ParseCmdLine(argc, argv, jobfile, &doDelete, spoolDir, &quiet,
                   &cluster, &nice, lockhost, &port))
   {
      CreateFullPath(jobfile);
      if(cluster!=0)
         UpdateSpoolDir(spoolDir, cluster);
      
      if(!CheckForSpoolDir(spoolDir))
      {
         fprintf(stderr,"Spool directory, %s, does not exist!\n",
                 spoolDir);
         return(1);
      }

      /* If either the port of host has not been specified get the values
         from a file
      */
      if((port==0) || (!lockhost[0]))
      {
         GetPortAndLockHost(spoolDir, &port, lockhost);
         if(!lockhost[0])
         {
            fprintf(stderr,"You must specify a host running the \
qllockd locking daemon\n");
            fprintf(stderr,"   Either use the .qllockdaemon file in \
the spool directory or the -l flag\n");
            return(1);
         }
      }

      InitLocks("qlite", lockhost, port);
      
#ifdef FILE_BASED_LOCKING
      if((status=CreateLockFile(spoolDir))==0)
#else
      if((status=GetLock(0,LOCK_TIMEOUT))==0)
#endif
      {
         /* Find the IDs for the person running the submit command      */
         uid = getuid();
         gid = getgid();

         /* Reject jobs submitted by root                               */
         if((uid == (uid_t)0) || (gid == (gid_t)0))
         {
            fprintf(stderr,"Jobs may not be submitted by root!\n");
            return(1);
         }
      
         /* Actually queue the job                                      */
         if((jobnum=SubmitJob(jobfile, spoolDir, uid, gid, nice))==0L)
         {
            fprintf(stderr,"Unable to queue the job\n");
#ifdef FILE_BASED_LOCKING
            DeleteLockFile(spoolDir);
#else
            ReleaseLock(0);
#endif
            return(1);
         }
         else if(!quiet)
         {
            printf("Submitted job number %ld\n", jobnum);
         }

         /* Delete the job file if requested to do so                   */
         if(doDelete)
            unlink(jobfile);

         /* ...and remove the lock file                                 */
#ifdef FILE_BASED_LOCKING
         DeleteLockFile(spoolDir);
#else
         ReleaseLock(0);
#endif
      }
      else if(status==1)
      {
         fprintf(stderr,
                 "Timeout waiting for lock to clear exceeded. \
Job not submitted\n");
         return(1);
      }
      else
      {
         fprintf(stderr,
                 "Unable to create lock. Job not submitted\n");
         return(1);
      }
   }
   else
   {
      Usage();
   }
   
   return(0);
}


/************************************************************************/
/*>BOOL ParseCmdLine(int argc, char **argv, char *jobfile, BOOL *doDelete,
                     char *spoolDir, BOOL *quiet, int *cluster, int *nice,
                     char *lockhost, int *port)
   -----------------------------------------------------------------------
   Input:     int   argc         Argument count
              char  **argv       Arguments
   Output:    char  *jobfile     The job file to be submitted
              BOOL  *doDelete    Delete the job file when queued? 
              char  *spoolDir    Spool directory
              int   *cluster     Cluster number
              int   *nice        Requested nice level
              char  *lockhost    Lock host name
              int   *port        Port number for qllockd
   Returns:   BOOL               Success?

   Parses the command line

   14.09.00 Original   By: ACRM
   04.10.00 Added lockhost and port
*/
BOOL ParseCmdLine(int argc, char **argv, char *jobfile, BOOL *doDelete,
                  char *spoolDir, BOOL *quiet, int *cluster, int *nice,
                  char *lockhost, int *port)
{
   argc--;
   argv++;

   jobfile[0]  = '\0';
   lockhost[0] = '\0';
   
   while(argc)
   {
      if(argv[0][0] == '-')
      {
         switch(argv[0][1])
         {
         case 'd':
            *doDelete = TRUE;
            break;
         case 'q':
            *quiet = TRUE;
            break;
         case 's':
            argv++;
            argc--;
            if(!argc)
               return(FALSE);
            strncpy(spoolDir,argv[0],PATH_MAX);
            break;
         case 'l':
            argv++;
            argc--;
            if(!argc)
               return(FALSE);
            strncpy(lockhost,argv[0],MAXBUFF);
            break;
         case 'c':
            argv++;
            argc--;
            if(!argc)
               return(FALSE);
            if(!sscanf(argv[0],"%d", cluster))
               return(FALSE);
            break;
         case 'p':
            argv++;
            argc--;
            if(!argc)
               return(FALSE);
            if(!sscanf(argv[0],"%d", port))
               return(FALSE);
            break;
         case 'n':
            argv++;
            argc--;
            if(!argc)
               return(FALSE);
            if(!sscanf(argv[0],"%d", nice))
               return(FALSE);
            if(*nice < 0)
               *nice = 0;
            break;
         default:
            return(FALSE);
            break;
         }
      }
      else
      {
         /* Check that there is 1 argument left                         */
         if(argc != 1)
            return(FALSE);
         
         /* Copy the filename to jobfile                                */
         strcpy(jobfile, argv[0]);
         
         return(TRUE);
      }
      
      argc--;
      argv++;
   }
   
   return(FALSE);
}


/************************************************************************/
/*>ULONG SubmitJob(char *jobfile, char *spoolDir, uid_t uid, gid_t gid, 
                   int nice)
   -------------------------------------------------------------------
   Input:     char    *jobfile    The job file to be queued
              char    *spoolDir   The directory to queue it to
              uid_t   uid         The UID of the submitter
              gid_t   gid         The GID of the submitter
              int     nice        Requested nice level
   Returns:   ULONG               Job number

   Actually sends a job into the queue and creates a control file for it

   14.09.00 Original   By: ACRM
*/
ULONG SubmitJob(char *jobfile, char *spoolDir, uid_t uid, gid_t gid, 
                int nice)
{
   char cmd[PATH_MAX+MAXBUFF],
        spoolfile[MAXBUFF],
        infofile[MAXBUFF];
   ULONG jobnum = 0L;
   FILE  *fp;

   /* Find a number for this job                                        */
   sprintf(infofile,"%s/.qllastjob", spoolDir);
   if((fp=fopen(infofile, "r"))!=NULL)
   {
      fscanf(fp,"%lu", &jobnum);
      fclose(fp);
   }
   if(++jobnum == 0L)
      jobnum = 1L;

   if((fp=fopen(infofile, "w"))!=NULL)
   {
      fprintf(fp,"%ld", jobnum);
      fclose(fp);
      chown(infofile, 0, 0);            /* Owned by root                */
   }
   else
   {
      return(0);
   }
   
   /* Copy the job file across to the spool directory                   */
   sprintf(spoolfile,"%s/%ld.job", spoolDir, jobnum);
   sprintf(cmd,"cp %s %s", jobfile, spoolfile);
   if(system(cmd))
      return(0);
   chown(spoolfile, uid, gid);          /* Owned by submitting user     */

   /* Create a control file                                             */
   if(!WriteControlFile(jobfile, spoolDir, jobnum, uid, gid, nice))
      return(0);
   
   return(jobnum);
}


/************************************************************************/
/*>BOOL WriteControlFile(char *jobfile, char *spoolDir, ULONG jobnum, 
                         uid_t uid, gid_t gid, int nice)
   -------------------------------------------------------------
   Input:     char  *jobfile     The original job file
              chat  *spoolDir    The spool directory
              ULONG jobnum       The job number
              uid_t uid          The UID
              gid_t gid          The GID
              int   nice         The requested nice value
   Returns:   BOOL               Success?

   Writes a control file for a job containing the UID/GID of the 
   submitter, the name of the submitted file and the requested nice level

   14.09.00 Original   By: ACRM
*/
BOOL WriteControlFile(char *jobfile, char *spoolDir, ULONG jobnum, 
                      uid_t uid, gid_t gid, int nice)
{
   char ctrlfile[MAXBUFF];
   FILE *fp;
   
   sprintf(ctrlfile,"%s/%ld.ctrl", spoolDir, jobnum);
   if((fp=fopen(ctrlfile,"w"))==NULL)
      return(FALSE);
   
   fprintf(fp,"J: %s\n", jobfile);
   fprintf(fp,"U: %ld\n", (ULONG)uid);
   fprintf(fp,"G: %ld\n", (ULONG)gid);
   fprintf(fp,"N: %d\n", nice);
   
   fclose(fp);

   chown(ctrlfile, 0, 0);               /* Owned by root                */

   return(TRUE);
}

/************************************************************************/
/*>void Usage(void)
   ----------------
   Prints a usage message

   14.09.00 Original   By: ACRM
*/
void Usage(void)
{
   fprintf(stderr,"\nqlsubmit V1.0 (c) 2000 University of Reading, \
Dr. Andrew C.R. Martin\n");

   fprintf(stderr,"\nUsage: qlsubmit [-d] [-q] [-s spooldir] \
[-c cluster] [-n niceval]\n");
   fprintf(stderr,"                [-p portnum] [-l lockhost] jobfile\n");
   fprintf(stderr,"       -d Delete jobfile after submission\n");
   fprintf(stderr,"       -q Run quietly\n");
   fprintf(stderr,"       -s Specify the directory for spooling\n");
   fprintf(stderr,"          (Default: %s)\n", DEF_SPOOLDIR);
   fprintf(stderr,"       -c Specify the cluster to process this job \
(Default: 0) \n");
   fprintf(stderr,"       -n Nice level to run the job at \
(Default: 10)\n");
#ifndef FILE_BASED_LOCKING
   fprintf(stderr, "       -l Specify the host name running the qllockd \
lock daemon\n");
   fprintf(stderr, "       -p Specify the port used for the lock \
daemon\n");
   fprintf(stderr, "          (Default: %d, or as specified in \
/etc/services)\n", DEFAULT_QLPORT);
#endif 

   fprintf(stderr,"\nqlsubmit submits a job for processing on a cluster \
of machines using\n");
   fprintf(stderr,"QLite.\n");

   fprintf(stderr,"\nThe cluster number simple lets sets of machines \
share a common spool\n");
   fprintf(stderr,"directory. It actually uses a subdirectory in the \
spool directory\n");
   fprintf(stderr,"for each cluster. This provides a simple way to \
divide machines in\n");
   fprintf(stderr,"a farm between different tasks or users.\n");

   fprintf(stderr,"\nNice levels default to 10 (i.e. nice -10) when \
jobs are submitted, but\n");
   fprintf(stderr,"this can be changed by the -n switch. Thus you could \
specify a nice \n");
   fprintf(stderr,"level of 0. However, qlrun which runs the jobs can \
be specified with\n");
   fprintf(stderr,"a maximum priority for any job. Thus even if you use \
-n 0 to ask for\n");
   fprintf(stderr,"a priority of 0, if qlrun has been run with a maximum \
nice of 5, jobs\n");
   fprintf(stderr,"will be run at nice 5 (i.e. with the command nice \
-5)\n\n");

#ifndef FILE_BASED_LOCKING
   fprintf(stderr, "The lock host daemon runs on one host in the \
cluster. qlrun will try to\n");
   fprintf(stderr, "obtain the host name and port number from the \
.qllockdaemon file in\n");
   fprintf(stderr, "the spool/cluster directory. If they cannot be read \
from there, a default\n");
   fprintf(stderr, "value (%d) is used for the port (or as specified \
in /etc/services\n", DEFAULT_QLPORT);
   fprintf(stderr, "but no default exists for the hostname. It must \
therefore be specified\n");
   fprintf(stderr, "on the command line. Any port number specified on \
the command line will\n");
   fprintf(stderr, "take precedence over defaults or values read from \
the spool directory\n\n");
#endif
}


