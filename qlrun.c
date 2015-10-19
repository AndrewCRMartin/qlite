/*************************************************************************

   Program:    qlrun
   File:       qlrun.c
   
   Version:    V1.0
   Date:       15.09.00
   Function:   Run queued jobs on farm machines
   
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

*************************************************************************/
/* Includes
*/
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#include <errno.h>
#ifdef __linux__
#  include <linux/limits.h>
#else
#  include <limits.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <pwd.h>

#include "qlutil.h"

/************************************************************************/
/* Defines and macros
*/
#define POLL_PAUSE 30
#define JOB_PAUSE 1
#define MAXBUFF 160
#define JOB_DIR "/tmp"
#define SHELL   "/bin/sh"
#define SENDMAIL "/usr/lib/sendmail -t"


/************************************************************************/
/* Globals
*/
int gDebug = 0;
extern char **environ;

/************************************************************************/
/* Prototypes
*/
int   main(int argc, char **argv);
void  QLRun(char *spoolDir, int maxnice, int instance, int tlimit);
BOOL  ParseCmdLine(int argc, char **argv, char *spoolDir, int *cluster,
                   int *nice, BOOL *asDaemon, int *instance, int *tlimit,
                   char *lockhost, int *port);
void  Usage(void);
char  *GetJob(ULONG jobid, char *spoolDir);
void  RunJob(char *spoolDir, char *jobname, int maxnice, int instance,
             int tlimit);
ULONG JobWaiting(char *spoolDir);
void  DeleteJob(char *jobname);
BOOL  GetJobInfo(char *jobname, uid_t *uid, gid_t *gid, int *nice,
                 char *jobfile);
BOOL GotShutdownFile(char *spoolDir);
BOOL GotSuspendFile(char *spoolDir);
void RemoveShutdownFile(char *spoolDir);
void DeleteRunFile(char *spoolDir, int instance);
void WriteRunFile(char *spoolDir, char *jobname, char *jobfile,
                  char *username, int nice, int instance);
void Email(char *username, char *jobfile);
int system_tlimit(char *command, int timelimit);


/************************************************************************/
/*>int main(int argc, char **argv)
   -------------------------------
   Input:     
   Output:    
   Returns:   

   Main program for the qlrun daemon

   15.09.00 Original  By: ACRM
*/
int main(int argc, char **argv)
{
   int cluster  = 0,
       instance = 1,
       tlimit   = 0,
       maxnice  = 0,
       port     = 0;
   char spoolDir[PATH_MAX],
        lockhost[MAXBUFF];
   BOOL asDaemon = TRUE;

   /* Get the default spool directory                                   */
   strcpy(spoolDir, DEF_SPOOLDIR);

   if(ParseCmdLine(argc, argv, spoolDir, &cluster, &maxnice, &asDaemon,
                   &instance, &tlimit, lockhost, &port))
   {
      if((!gDebug) && !RootUser())
      {
         fprintf(stderr,"qlrun must be run by root\n");
         return(1);
      }
      
      if(cluster!=0)
         UpdateSpoolDir(spoolDir, cluster);
      
      if(!CheckForSpoolDir(spoolDir))
      {
         fprintf(stderr,"Spool directory, %s, does not exist!\n",
                 spoolDir);
         return(1);
      }

      /* If there is a shutdown file, remove it                         */
      RemoveShutdownFile(spoolDir);
         
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
      
      /* Become a daemon process                                        */
      if(asDaemon)
      {
         if(!DaemonInit())
         {
            fprintf(stderr,"Unable to initialise daemon\n");
            return(1);
         }
      }

      if(InitLocks("qlite", lockhost, port))
      {
         QLRun(spoolDir, maxnice, instance, tlimit);
      }
      else
      {
         fprintf(stderr,"Unable to initialise locking\n");
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
/*>BOOL ParseCmdLine(int argc, char **argv, char *spoolDir, int *cluster,
                     int *nice, BOOL *asDaemon, int *instance, 
                     int *tlimit)
   ----------------------------------------------------------------------
   Input:     int    argc        Argument count
              char   *argv       Arguments
   Output:    char   *spoolDir   Spool directory
              int    *cluster    Cluster number
              int    *nice       Requested nice level
              BOOL   *asDaemon   Run as a daemon
              int    *instance   Run instance number
              int    *tlimit     Time limit for a job running under this
                                 daemon
   Returns:   BOOL               Success

   Parses the command line

   15.09.00 Original  By: ACRM
   02.10.00 Added instance and tlimit
*/
BOOL ParseCmdLine(int argc, char **argv, char *spoolDir, int *cluster,
                  int *nice, BOOL *asDaemon, int *instance, int *tlimit,
                  char *lockhost, int *port)
{
   argc--;
   argv++;

   lockhost[0] = '\0';

   while(argc)
   {
      if(argv[0][0] == '-')
      {
         switch(argv[0][1])
         {
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
         case 'd':
            *asDaemon = FALSE;
            gDebug = 1;
            break;
         case 'i':
            argv++;
            argc--;
            if(!argc)
               return(FALSE);
            if(!sscanf(argv[0],"%d", instance))
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
         case 't':
            argv++;
            argc--;
            if(!argc)
               return(FALSE);
            if(!sscanf(argv[0],"%d", tlimit))
               return(FALSE);
            *tlimit *= 60;
            break;
         default:
            return(FALSE);
            break;
         }
      }
      else
      {
         return(FALSE);
      }
      
      argc--;
      argv++;
   }
   
   return(TRUE);
}


/************************************************************************/
/*>void QLRun(char *spoolDir, int maxnice, int instance, int tlimit)
   -----------------------------------------------------------------
   Input:     char  *spoolDir      Spool directory
              int   maxnice        Max nice level to run a job at
              int   instance       Run instance number
              int   tlimit         Time limit for a job running under this
                                   daemon

   Main loop which looks for jobs and runs them

   15.09.00 Original  By: ACRM
   02.10.00 Added instance and tlimit
   03.10.00 Added check on GotSuspendFile()
*/
void QLRun(char *spoolDir, int maxnice, int instance, int tlimit)
{
   int   status;
   ULONG jobid;
   char  *jobname;

   for(;;)
   {
      if(GotShutdownFile(spoolDir))
         break;
      
      if(GotSuspendFile(spoolDir))
      {
         sleep(POLL_PAUSE);
      }
      else
      {
#ifdef FILE_BASED_LOCKING
         if((status=CreateLockFile(spoolDir))==0)
#else
         if((status=GetLock(instance, LOCK_TIMEOUT))==0)
#endif
         {
            if((jobid=JobWaiting(spoolDir))!=0)
            {
               jobname = GetJob(jobid, spoolDir);
#ifdef FILE_BASED_LOCKING
               DeleteLockFile(spoolDir);
#else
               ReleaseLock(instance);
#endif
               RunJob(spoolDir, jobname, maxnice, instance, tlimit);
               sleep(JOB_PAUSE);
            }
            else
            {
#ifdef FILE_BASED_LOCKING
               DeleteLockFile(spoolDir);
#else
               ReleaseLock(instance);
#endif
               sleep(POLL_PAUSE);
            }
         }
         else 
         {
            if(gDebug)
            {
               if(status==1)
               {
                  fprintf(stderr, "Timeout waiting for lock to clear \
exceeded.\n");
               }
               else
               {
                  fprintf(stderr, "Unable to perform locking.\n");
#ifndef FILE_BASED_LOCKING
                  fprintf(stderr, "   Check the lock daemon is running \
on the specified machine\n");
#endif
               }
            }
            
            sleep(POLL_PAUSE);
         }
      }
   }
}


/************************************************************************/
/*>ULONG JobWaiting(char *spoolDir)
   --------------------------------
   Input:     char   *spoolDir     Spool directory
   Returns:   ULONG                Job number (0 if nothing waiting)

   Tests whether a job is waiting and returns its ID if there is.

   15.09.00 Original  By: ACRM
*/
ULONG JobWaiting(char *spoolDir)
{
   struct dirent *dirp;
   DIR           *dp;
   char          buffer[PATH_MAX],
                 *chp;
   ULONG         jobnum = 0;

   if((dp=opendir(spoolDir)) == NULL)
   {
      if(gDebug)
      {
         fprintf(stderr,"qlrun dies! Can't read directory: %s\n", 
                 spoolDir);
      }
      exit(1);
   }

   while((dirp = readdir(dp)) != NULL)
   {
      /* Ignore files starting with a .                                 */
      if(dirp->d_name[0] != '.')
      {
         /* See if it's a .ctrl file                                    */
         strcpy(buffer, dirp->d_name);
         if((chp=strstr(buffer, ".ctrl"))!=NULL)
         {
            /* Extract the job number from the name                     */
            *chp = '\0';
            sscanf(buffer,"%ld",&jobnum);
            break;
         }
      }
   }
   
   closedir(dp);

   if(gDebug)
   {
      fprintf(stderr,"%s waiting\n", ((jobnum)?"Job":"No job"));
   }
   return(jobnum);
}


/************************************************************************/
/*>char *GetJob(ULONG jobid, char *spoolDir)
   -----------------------------------------
   Input:     ULONG   jobid       Identifier for a job
              char    *spoolDir   Spool directory
   Returns:   char *              Jobname

   Pulls a job out of the spool directory into JOB_DIR (/tmp on the
   local machine) and returns a unique name consisting of the process
   ID+jobid

   15.09.00 Original  By: ACRM
*/
char *GetJob(ULONG jobid, char *spoolDir)
{
   static char jobname[MAXBUFF];
   char        cmd[PATH_MAX+MAXBUFF];
   pid_t       pid;

   if(gDebug)
   {
      fprintf(stderr,"Getting job %ld from %s\n", jobid, spoolDir);
   }

   /* Create a unique jobname by combining the process ID and jobid     */
   pid = getpid();
   sprintf(jobname,"%ld.%ld",(ULONG)pid,jobid);

   /* Copy the .job and .ctrl files across to the working directory     */
   sprintf(cmd,"cp %s/%ld.job %s/%s.run", 
           spoolDir, jobid, JOB_DIR, jobname);
   system(cmd);
   sprintf(cmd,"cp %s/%ld.ctrl %s/%s.stat", 
           spoolDir, jobid, JOB_DIR, jobname);
   system(cmd);

   /* Delete the copies from the spool directory                        */
   sprintf(cmd,"rm -f %s/%ld.job",  spoolDir, jobid);
   system(cmd);
   sprintf(cmd,"rm -f %s/%ld.ctrl", spoolDir, jobid);
   system(cmd);
   
   return(jobname);
}


/************************************************************************/
/*>void DeleteJob(char *jobname)
   -----------------------------
   Input:     char   *jobname    The unique jobname

   Deletes a finished job file and control file from JOB_DIR

   15.09.00 Original  By: ACRM
*/
void DeleteJob(char *jobname)
{
   char cmd[MAXBUFF];
   
   /* Delete the copies from the spool directory                        */
   sprintf(cmd,"rm -f %s/%s.run",  JOB_DIR, jobname);
   system(cmd);
   sprintf(cmd,"rm -f %s/%s.stat", JOB_DIR, jobname);
   system(cmd);
}


/************************************************************************/
/*>BOOL GetJobInfo(char *jobname, uid_t *uid, gid_t *gid, int *nice,
                   char *jobfile)
   -----------------------------------------------------------------
   Input:     char  *jobname    The unique jobname
   Output:    uid_t *uid        The UID
              gid_t *gid        The GID
              int   *nice       The requested nice level
              char  *jobfile    The file submitted as the job to run
   Returns:   BOOL              Success?

   Gets the info on the job (user who submitted it and nice level)
   from a job control file in JOB_DIR

   15.09.00 Original  By: ACRM
   25.09.00 Added jobfile
*/
BOOL GetJobInfo(char *jobname, uid_t *uid, gid_t *gid, int *nice,
                char *jobfile)
{
   char  buffer[MAXBUFF],
         junk[16];
   FILE  *fp;
   ULONG tmp;
   struct stat statbuff;
   

   /* Create the name of the control file                               */
   sprintf(buffer,"%s/%s.stat", JOB_DIR, jobname);

   /* Check that the control file is owned by root                      */
   stat(buffer, &statbuff);
   if((statbuff.st_uid != (uid_t)0) || (statbuff.st_gid != (gid_t)0))
   {
      if(gDebug)
         fprintf(stderr,"Control file not owned by root! Run aborted!\n");
      
      return(FALSE);
   }

   /* Ppen it for reading                                               */
   if((fp=fopen(buffer,"r"))==NULL)
      return(FALSE);

   while(fgets(buffer, MAXBUFF, fp))
   {
      TERMINATE(buffer);
      if(buffer[0] == 'U')
      {
         sscanf(buffer,"%s %ld", junk, &tmp);
         *uid = (uid_t)tmp;
      }
      else if(buffer[0] == 'G')
      {
         sscanf(buffer,"%s %ld", junk, &tmp);
         *gid = (gid_t)tmp;
      }
      else if(buffer[0] == 'N')
      {
         sscanf(buffer,"%s %ld", junk, &tmp);
         *nice = (int)tmp;
      }
      else if(buffer[0] == 'J')
      {
         sscanf(buffer,"%s %s", junk, jobfile);
      }
   }
   fclose(fp);


   /* Make sure that we aren't trying to run anything as root           */
   if((*uid == (uid_t)0) || (*gid == (gid_t)0))
   {
      if(gDebug)
         fprintf(stderr,"Attempt to run job as root aborted!\n");
      
      return(FALSE);
   }
      

   /* Make sure that the nice value isn't trying to increase the 
      priority. i.e. make sure it is positive
   */
   if(*nice < 0)
      *nice = 0;

   /* Now we make it negative, just because the call to the nice command
      uses a - sign to introduce the value
   */
   *nice = (-(*nice));

   return(TRUE);
}


/************************************************************************/
/*>void RunJob(char *spoolDir, char *jobname, int maxnice, int instance, 
               int tlimit)
   ---------------------------------------------------------------------
   Input:     char   *spoolDir    Spool Directory
              char   *jobname     The unique job name
              int    maxnice      Maximum allowed nice level
              int    instance     Run instance number
              int    tlimit       Time limit for a job running under this
                                  daemon

   Runs a specified job using the maximum priority specified

   15.09.00 Original  By: ACRM
   25.09.00 Added spoolDir
   02.10.00 Added instance and tlimit
*/
void RunJob(char *spoolDir, char *jobname, int maxnice, int instance, 
            int tlimit)
{
   uid_t uid;
   gid_t gid;
   int   nice;
   char  cmd[MAXBUFF],
         jobfile[PATH_MAX];
   struct passwd *pwd;
   char          *username;


   if(gDebug)
   {
      fprintf(stderr,"Running job %s\n", jobname);
   }

   if(GetJobInfo(jobname, &uid, &gid, &nice, jobfile))
   {
      /* qlrun can be run to specify a maximum nice value (0 being
         highest priority, 19 lowest). We convert this to a -ve number
         and if the -ve nice number from GetJobInfo() is higher then
         we reduce the priority to maxnice
      */
      maxnice = (-maxnice);
      if(nice > maxnice)
         nice = maxnice;

      /* Get the username from the UID                                  */
      pwd = getpwuid(uid);
      username = pwd->pw_name;
      
      /* Write the file to say that a job is running                    */
      WriteRunFile(spoolDir, jobname, jobfile, username, nice, instance);

      if(gDebug)
      {
         fprintf(stderr,"Running job: %s : %s\n", jobname, jobfile);
      }
      
      /* Run the job as the requested user                              */
      sprintf(cmd, "su - %s -c \"nice %d %s %s/%s.run\"",
              username, nice, SHELL, JOB_DIR, jobname);

      if(tlimit)
      {
         if((system_tlimit(cmd, tlimit))==9)
         {
            Email(username, jobfile);
         }
      }
      else
      {
         system(cmd);
      }

      /* Delete the file which says a job is running                    */
      DeleteRunFile(spoolDir, instance);
   }

   DeleteJob(jobname);
}


/************************************************************************/
/*>BOOL GotShutdownFile(char *spoolDir)
   ------------------------------------
   Input:   char  *spoolDir      The spool directory
   Returns: BOOL                 Does a shutdown file exist?

   Checks the spool directory for the presence of a file called .killXXX
   (where XXX is the name of this node). If present returns TRUE. If not 
   returns FALSE

   18.09.00 Original   By: ACRM
*/
BOOL GotShutdownFile(char *spoolDir)
{
   char hname[MAXBUFF], 
        *chp, 
        buffer[PATH_MAX];
   
   /* Try to get the hostname - just return FALSE if we fail            */
   if(gethostname(hname,MAXBUFF))
      return(FALSE);
   
   /* Terminate the hostname at the first .                             */
   if((chp=strchr(hname,'.'))!=NULL)
      *chp = '\0';
   
   /* Create the name of the file which indicates this daemon to be
      shutdown
   */
   sprintf(buffer,"%s/.kill%s", spoolDir,hname);

   /* If it exists, create a file to say this daemon is shutdown and 
      return TRUE
   */
   if(!access(buffer,F_OK))
   {
      sprintf(buffer,"touch %s/.shutdown%s", spoolDir, hname);
      system(buffer);
      return(TRUE);
   }
   
   return(FALSE);
}


/************************************************************************/
/*>void RemoveShutdownFile(char *spoolDir)
   ---------------------------------------
   Input:   char  *spoolDir      The spool directory

   Removes a file called .killXXX (where XXX is the name of this node)
   from the spool directory.

   18.09.00 Original   By: ACRM
*/
void RemoveShutdownFile(char *spoolDir)
{
   char hname[MAXBUFF], 
        *chp, 
        buffer[PATH_MAX];
   
   /* Try to get the hostname - just return FALSE if we fail            */
   if(gethostname(hname,MAXBUFF))
      return;
   
   /* Terminate the hostname at the first .                             */
   if((chp=strchr(hname,'.'))!=NULL)
      *chp = '\0';
   
   /* Create the names of the file which indicates this daemon is to be
      shutdown and has been shutdown and remove them
   */
   sprintf(buffer,"%s/.kill%s", spoolDir,hname);
   unlink(buffer);

   sprintf(buffer,"%s/.shutdown%s", spoolDir,hname);
   unlink(buffer);
}


/************************************************************************/
/*>void WriteRunFile(char *spoolDir, char *jobname, char *jobfile, 
                     char *username, int nice, int instance)
   ---------------------------------------------------------------
   Input:   char   *spoolDir    Spool directory
            char   *jobname     Unique jobname created for the run
            char   *jobfile     The name of the file submitted
            char   *username    The user who submitted it
            int    nice         Nice level at which to run
            int    instance     Daemon instance number

   Writes a file into the spool directory to say that a job is running

   Called from RunJob() just before the job is executed.
   spoolDir needs to be passed into RunJob()
   jobname is created within RunJob()
   jobfile needs to be extracted by GetJobInfo()
   username is created within RunJob() from the UID
   nice is extracted by GetJobInfo()

   22.09.00 Original   By: ACRM
   02.10.00 Added instance
*/
void WriteRunFile(char *spoolDir, char *jobname, char *jobfile, 
                  char *username, int nice, int instance)
{
   char file[PATH_MAX];
   char nodename[MAXBUFF], *chp;
   FILE *fp;
   
   if(gethostname(nodename, MAXBUFF))
      return;

   if((chp=strchr(nodename,'.'))!=NULL)
      *chp = '\0';
   
   sprintf(file,"%s/.%s.running.%d", spoolDir, nodename, instance);
   if((fp=fopen(file,"w"))!=NULL)
   {
      fprintf(fp,"I: %s\n",jobname);
      fprintf(fp,"J: %s\n",jobfile);
      fprintf(fp,"U: %s\n",username);
      fprintf(fp,"N: %d\n",nice);

      fclose(fp);

      if(gDebug)
         fprintf(stderr,"Written run file, %s\n", file);
   }
}


/************************************************************************/
/*>void DeleteRunFile(char *spoolDir, int instance)
   ------------------------------------------------
   Input:   char  *spoolDir      Spool directory
            int   instance       Daemon instance number

   Deletes the file which indicates a job is running

   Called from RunJob() just after the job is executed.

   22.09.00 Original   By: ACRM
*/
void DeleteRunFile(char *spoolDir, int instance)
{
   char file[PATH_MAX];
   char nodename[MAXBUFF], *chp;
   
   if(gethostname(nodename, MAXBUFF))
      return;
   
   if((chp=strchr(nodename,'.'))!=NULL)
      *chp = '\0';
   
   sprintf(file,"%s/.%s.running.%d",spoolDir, nodename, instance);
   unlink(file);

   if(gDebug)
      fprintf(stderr,"Delete run file: %s\n", file);
}



/************************************************************************/
/*>void Email(char *username, char *jobfile)
   -----------------------------------------
   Input:   char   *username        Username to whom to send EMail
            char   *jobfile         The job file about which to send EMail

   Sends an EMail to the specified user to indicate that the job
   specified by jobfile ran out of time.

   02.01.00 Original   By: ACRM
*/
void Email(char *username, char *jobfile)
{
   FILE *pp;

   if((pp = popen(SENDMAIL, "w"))!=NULL)
   {
      fprintf(pp, "To: %s\n", username);
      fprintf(pp, "Subject: QLite job out of time\n\n");
      fprintf(pp, "Your QLite job:\n");
      fprintf(pp, "   %s\n", jobfile);
      fprintf(pp, "ran out of time.\n\n");
      pclose(pp);
   }
}


/************************************************************************/
/*>BOOL GotSuspendFile(char *spoolDir)
   -----------------------------------
   Input:   char  *spoolDir      The spool directory
   Returns: BOOL                 Does a suspend file exist?

   Checks the spool directory for the presence of a file called .qlsuspend
   If present returns TRUE. If not returns FALSE

   03.10.00 Original   By: ACRM
*/
BOOL GotSuspendFile(char *spoolDir)
{
   char buffer[PATH_MAX];
   
   /* Create the name of the file which indicates this daemon to be
      shutdown
   */
   sprintf(buffer,"%s/.qlsuspend", spoolDir);

   if(!access(buffer,F_OK))
   {
      return(TRUE);
   }
   
   return(FALSE);
}


/************************************************************************/
/*>int system_tlimit(char *command, int timelimit) 
   -----------------------------------------------
   Input:   char    *command     Command to be executed
            int     timelimit    Time limit (in seconds)
   Returns: int                  Exit status. 9 if it ran out of time
                                 otherwise, the exit status of the 
                                 command run

   Like system() but will only allow a process to run for at most
   timelimit seconds

   02.10.00 Original   By: ACRM
*/
int system_tlimit(char *command, int timelimit) 
{
   int pid, pidtimer, status, retval;

   if (command == NULL)
      return(1);

   /* Fork off a new process                                            */
   pid = fork();
   if (pid == -1)
      return(-1);
   
   /***                     SUBPROCESS 1 BEGINS                       ***/
   /* If it's the new process, then run the command                     */
   if (pid == 0) 
   {
      char *argv[4];
      argv[0] = "sh";
      argv[1] = "-c";
      argv[2] = command;
      argv[3] = 0;
      
      execve("/bin/sh", argv, environ);
      
      exit(127);
   }
   /***                     SUBPROCESS 1 ENDS                         ***/

   /* It's the parent, so pid is the PID of the child                   */
   if(timelimit != 0)
   {
      /* Fork off another process to wait for the specified timelimit   */
      pidtimer = fork();
      if (pidtimer == -1)
         return(-1);
      
      /***                  SUBPROCESS 2 BEGINS                       ***/
      /* If it's the child start the timer                              */
      if(pidtimer == 0)
      {
         sleep(timelimit);
         kill(pid, 9);
         exit(0);
      }
      /***                  SUBPROCESS 2 ENDS                         ***/
   }
   
   /* It's the parent, so wait for the children to finish               */
   for(;;)
   {
      if(waitpid(pid, &status, 0) == -1)  
      {
         if (errno != EINTR)
            return(-1);
      }
      else
      {
         /* WIFEXITED() tells us whether the child exited normally.
            If not then it was killed by our timer process, so
            we return(9)

            WEXITSTATUS() extracts the exit status as an unsigned
            value, the cast to char and then to int turns it back
            into a signed int < 256
         */
         if(WIFEXITED(status))
         {
            retval = ((int)(char)WEXITSTATUS(status));
         }
         else
         {
            retval = 9;
         }
         
         /* Kill the timer in case SUBPROCESS-1 completed and then wait
            for the timer to stop it from zombie-ing.
         */
         kill(pidtimer, 9);
         waitpid(pidtimer, &status, 0);
         return(retval);
      }
   }

   return(0);
}


/************************************************************************/
/*>void Usage(void)
   ----------------
   Prints a usage message

   15.09.00 Original  By: ACRM
*/
void Usage(void)
{
   fprintf(stderr, "\nqlrun V1.0 (c) 2000 University of Reading, Dr. \
Andrew C.R. Martin\n");

   fprintf(stderr, "\nUsage: qlrun [-d] [-s spooldir] [-c cluster] \
[-i pinum] [-n maxnice] \n");
   fprintf(stderr,"             [-p port] [-l lockhost] \
[-t timelimit]\n");
   fprintf(stderr, "       -d Run in interactive debug mode rather than \
as a daemon\n");
   fprintf(stderr, "       -s Specify the spool directory (Default: \
%s)\n", DEF_SPOOLDIR);
   fprintf(stderr, "       -c Specify the cluster number\n");
   fprintf(stderr, "       -i Process instance number\n");
   fprintf(stderr, "       -n Specify max allowed nice level for this \
machine\n");
   fprintf(stderr, "       -t Maximum allowed time for a process in \
minutes.\n");
   fprintf(stderr, "          (Default: 0 = unlimited)\n");

#ifndef FILE_BASED_LOCKING
   fprintf(stderr, "       -l Specify the host name running the qllockd \
lock daemon\n");
   fprintf(stderr, "       -p Specify the port used for the lock \
daemon\n");
   fprintf(stderr, "          (Default: %d, or as specified in \
/etc/services)\n", DEFAULT_QLPORT);
#endif 

   fprintf(stderr, "\nqlrun is the QLite daemon which sits on machines \
and runs jobs from the\n");
   fprintf(stderr, "queue created by qlsubmit. If you have more than \
one processor on a\n");
   fprintf(stderr, "machine, simply start the daemon once for each \
processor.\n");

   fprintf(stderr, "\nThe cluster number simple lets sets of machines \
share a common spool\n");
   fprintf(stderr, "directory. It actually uses a subdirectory in the \
spool directory\n");
   fprintf(stderr, "for each cluster. This provides a simple way to \
divide machines in\n");
   fprintf(stderr, "a farm between different tasks or users.\n");

   fprintf(stderr, "\nIf you have more than one instance of the daemon \
running on a machine\n");
   fprintf(stderr, "each should have a unique process instance number \
specified with -p\n");
   fprintf(stderr, "(This is only needed if you want qllist to be able \
to list running\n");
   fprintf(stderr, "jobs correctly.) The default process instance \
number is 1.\n");

   fprintf(stderr, "\nNice levels default to 10 (i.e. nice -10) when \
jobs are submitted, but\n");
   fprintf(stderr, "this can be changed by the -n switch to qlsubmit. \
Thus qlsubmit\n");
   fprintf(stderr, "could specify a nice level of 0. The -n switch to \
qlrun specifies a\n");
   fprintf(stderr, "maximum priority for any job running on this \
machine. Thus if you\n");
   fprintf(stderr, "specify -n 5, all jobs will get niced back down to \
5 (i.e. nice -5)\n");
   fprintf(stderr, "even if submitted with a nice level of 0.\n\n");

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

