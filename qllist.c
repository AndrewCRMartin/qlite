/*************************************************************************

   Program:    qllist
   File:       qllist.c
   
   Version:    V1.0
   Date:       18.09.00
   Function:   List queued jobs on farm machines
   
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
   If you want to be able to list currently running jobs, you need a file
   $(SPOOLDIR)/.machinelist which contains entries of the form:
      machine_name instance
   where machine_name is the local name (i.e. sapc13 rather than
   sapc13.rdg.ac.uk) of a node in the cluster and instance is 1 where
   only one instance of qlrun is running on the machine. Separate lines
   with instance set to 2, 3, 4, etc are used where multiple instances
   of the qlrun daemon are running.


**************************************************************************

   Revision History:
   =================

*************************************************************************/
/* Includes
*/
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#ifdef __linux__
#  include <linux/limits.h>
#else
#  include <limits.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>

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
void  Usage(void);
int DisplayAllClusters(char *spoolDir, BOOL totalOnly);
int DisplayJobs(char *spoolDir, BOOL totalOnly);
void PrintJob(ULONG jobnum, char *jobfile, uid_t uid, gid_t gid, 
              int nice, char *username);
BOOL ParseCmdLine(int argc, char **argv, char *spoolDir, int *cluster,
                  BOOL *totalOnly, BOOL *runningOnly);
BOOL GetJobDetails(char *spoolDir, ULONG jobnum, char *jobfile,
                   uid_t *uid, gid_t *gid, int *nice,
                   char *username);
void PrintRunningJobs(FILE *out, RUNFILE *runfiles);


/************************************************************************/
/*>int main(int argc, char **argv)
   -------------------------------
   Input:     
   Output:    
   Returns:   

   Main program for the qllist

   18.09.00 Original  By: ACRM
*/
int main(int argc, char **argv)
{
   char    spoolDir[PATH_MAX],
           spoolDirOrig[PATH_MAX],
           *env;
   BOOL    totalOnly   = FALSE,
           runningOnly = FALSE;
   RUNFILE *runfiles;
   int     cluster = (-1), njobs;

   /* Get the default spool directory from the environment variable if
      this has been set
   */
   if((env=getenv("QLSPOOLDIR"))!=NULL)
      strcpy(spoolDir, env);
   else
      strcpy(spoolDir, DEF_SPOOLDIR);

   if(ParseCmdLine(argc, argv, spoolDir, &cluster, &totalOnly,
                   &runningOnly))
   {
      if(!runningOnly)
      {
         strcpy(spoolDirOrig, spoolDir);
         
         if(cluster==(-1))
         {
            njobs = DisplayAllClusters(spoolDir, totalOnly);
            printf("%d total jobs waiting\n", njobs);
         }
         else if(cluster==0)
         {
            njobs = DisplayJobs(spoolDir, totalOnly);
            printf("%d jobs waiting on default cluster\n", njobs);
         }
         else
         {
            UpdateSpoolDir(spoolDir, cluster);
            njobs = DisplayJobs(spoolDir, totalOnly);
            printf("%d jobs waiting on cluster %d\n", njobs, cluster);
         }
      }

      runfiles = ReadMachineList(spoolDir);
      PrintRunningJobs(stdout, runfiles);
   }
   else
   {
      Usage();
   }
   return(0);
}


/************************************************************************/
/*>int DisplayAllClusters(char *spoolDir, BOOL quiet)
   --------------------------------------------------
   Input:   char    *spoolDir    Spool directory
            BOOL    quiet        Run quietly
   Returns: int                  Number of jobs in all clusters

   Displays information for each cluster. If quiet is set then just
   returns the number of waiting jobs, printing nothing.

   18.09.00 Original   By: ACRM
*/
int DisplayAllClusters(char *spoolDir, BOOL quiet)
{
   char buffer[PATH_MAX];
   int  njobs, cluster, njobsTotal = 0;
   
   if(!quiet)
      printf("Default cluster:\n----------------\n");
      
   njobs = DisplayJobs(spoolDir, quiet);
   njobsTotal += njobs;
   if(!quiet)
      printf("%d jobs waiting on default cluster\n\n", njobs);

   for(cluster=1; cluster <= MAXCLUSTER; cluster++)
   {
      strcpy(buffer, spoolDir);
      UpdateSpoolDir(buffer, cluster);

      if(!access(buffer, F_OK))
      {
         if(!quiet)
            printf("Cluster %3d:\n------------\n", cluster);
         
         njobs = DisplayJobs(buffer, quiet);
         if(!quiet)
            printf("%d jobs waiting on cluster %d\n\n", njobs, cluster);
         
         njobsTotal += njobs;
      }
      
   }

   return(njobsTotal);
}


/************************************************************************/
/*>int DisplayJobs(char *spoolDir, BOOL quiet)
   -------------------------------------------
   Input:   char   *spoolDir    Spool directory
            BOOL   quiet        Run quietly
   Returns: int                 Number of waiting jobs

   Shows the jobs for a specific cluster. If quiet is set then doesn't
   actually print anything, but just returns the number of jobs waiting

   18.09.00 Original   By: ACRM
*/
int DisplayJobs(char *spoolDir, BOOL quiet)
{
   struct dirent *dirp;
   DIR           *dp;
   char          buffer[PATH_MAX],
                 jobfile[PATH_MAX],
                 username[MAXBUFF],
                 *chp;
   ULONG         jobnum = 0;
   int           njobs = 0,
                 nice;
   uid_t         uid;
   gid_t         gid;
   
   
   if(!CheckForSpoolDir(spoolDir))
   {
      fprintf(stderr,"Spool directory, %s, does not exist!\n",
              spoolDir);
   }
   else
   {
      if((dp=opendir(spoolDir)) == NULL)
      {
         fprintf(stderr,"Can't read spool directory: %s\n", 
                 spoolDir);
         return(0);
      }
      
      while((dirp = readdir(dp)) != NULL)
      {
         /* Ignore files starting with a .                              */
         if(dirp->d_name[0] != '.')
         {
            /* See if it's a .ctrl file                                 */
            strcpy(buffer, dirp->d_name);
            if((chp=strstr(buffer, ".ctrl"))!=NULL)
            {
               /* Extract the job number from the name                  */
               *chp = '\0';;
               sscanf(buffer,"%ld",&jobnum);
               
               if(GetJobDetails(spoolDir, jobnum, jobfile, &uid, &gid, 
                                &nice, username))
               {
                  if(!quiet)
                     PrintJob(jobnum, jobfile, uid, gid, nice, username);
                  njobs++;
               }
            }
         }
      }
      closedir(dp);
   }
   
   return(njobs);
}
   

/************************************************************************/
/*>void PrintJob(ULONG jobnum, char *jobfile, uid_t uid, gid_t gid, 
                 int nice, char *username)
   -----------------------------------------------------------------
   Input:   ULONG   jobnum    The job number
            char    *jobfile  The name of the original file submitted
            uid_t   uid       The UID
            gid_t   gid       The GID
            int     nice      Nice value at which the job is to run
            char    *username Username:groupname of submitter

   Prints information about a queued job

   18.09.00 Original   By: ACRM
*/
void PrintJob(ULONG jobnum, char *jobfile, uid_t uid, gid_t gid, 
              int nice, char *username)
{
   printf("Job number: %ld : %s\n", jobnum, jobfile);
   printf("Run for:    %s (%ld:%ld)\n", username, (ULONG)uid, (ULONG)gid);
   printf("Nice value: %d\n\n", nice);
}

/************************************************************************/
/*>BOOL ParseCmdLine(int argc, char **argv, char *spoolDir, int *cluster,
                     BOOL *totalOnly, BOOL *runningOnly)
   ----------------------------------------------------------------------
   Input:     int    argc          Argument count
              char   *argv         Arguments
   Output:    char   *spoolDir     Spool directory
              int    *cluster      Cluster number
              BOOL   *totalOnly    Report only the number of jobs
              BOOL   *runningOnly  Report only the running jobs
   Returns:   BOOL                 Success

   Parses the command line

   18.09.00 Original  By: ACRM
*/
BOOL ParseCmdLine(int argc, char **argv, char *spoolDir, int *cluster,
                  BOOL *totalOnly, BOOL *runningOnly)
{
   argc--;
   argv++;

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
            strncpy(spoolDir,argv[0],MAXBUFF);
            break;
         case 'c':
            argv++;
            argc--;
            if(!argc)
               return(FALSE);
            if(!sscanf(argv[0],"%d", cluster))
               return(FALSE);
            break;
         case 't':
            *totalOnly = TRUE;
            break;
         case 'r':
            *runningOnly = TRUE;
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
/*>BOOL GetJobDetails(char *spoolDir, ULONG jobnum, char *jobfile,
                      uid_t *uid, gid_t *gid, int *nice,
                      char *username)
   ---------------------------------------------------------------
   Input:     char  *spoolDir   The spool directory
              ULONG jobnum      A job number
   Output:    char  *jobfile    The submitted job file
              uid_t *uid        The UID
              gid_t *gid        The GID
              int   *nice       The requested nice level
              char  *username   Username derived from the UID
   Returns:   BOOL              Success?

   Gets the info on the job (user who submitted it, nice level and
   username:groupname) from a job control file in the spool directory

   18.09.00 Original  By: ACRM
*/
BOOL GetJobDetails(char *spoolDir, ULONG jobnum, char *jobfile,
                   uid_t *uid, gid_t *gid, int *nice,
                   char *username)
{
   char  buffer[MAXBUFF],
         jobname[PATH_MAX],
         junk[16];
   FILE  *fp;
   ULONG tmp;
   struct stat statbuff;
   struct passwd *pwd;
   struct group  *grp;
   
   
   /* Create the name of the control file                               */
   sprintf(buffer,"%s/%ld.ctrl", spoolDir, jobnum);

   /* Check that the control file is owned by root                      */
   stat(buffer, &statbuff);
   if((statbuff.st_uid != (uid_t)0) || (statbuff.st_gid != (gid_t)0))
   {
      fprintf(stderr,"Warning: Control file not owned by root! %s\n",
              buffer);
   }

   /* Open it for reading                                               */
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
      sprintf(buffer,"%s/%s.ctrl", spoolDir, jobname);
      fprintf(stderr,"Warning: Attempt to run job as root! %s\n",
              buffer);
   }

   /* Get the username:groupname from the numeric versions              */
   pwd = getpwuid(*uid);
   grp = getgrgid(*gid);
   sprintf(username,"%s:%s", pwd->pw_name, grp->gr_name);
      
   return(TRUE);
}


/************************************************************************/
/*>void PrintRunningJobs(FILE *out, RUNFILE *runfiles)
   ---------------------------------------------------
   Input:   FILE    *out       Output file pointer
            RUNFILE *runfiles  Linked list of .running files and nodes

   Prints a list of the running jobs

   22.09.00 Original   By: ACRM
*/
void PrintRunningJobs(FILE *out, RUNFILE *runfiles)
{
   RUNFILE *r;
   char    junk[8],
           jobname[64],
           jobfile[PATH_MAX],
           username[64],
           buffer[PATH_MAX + 8];
   ULONG   nice;
   FILE    *fp;
   BOOL    first = TRUE;
   

   for(r=runfiles; r!=NULL; NEXT(r))
   {
      if((fp=fopen(r->file,"r"))!=NULL)
      {
         while(fgets(buffer, MAXBUFF, fp))
         {
            TERMINATE(buffer);
            if(buffer[0] == 'I')
            {
               sscanf(buffer,"%s %s", junk, jobname);
            }
            else if(buffer[0] == 'J')
            {
               sscanf(buffer,"%s %s", junk, jobfile);
            }
            else if(buffer[0] == 'N')
            {
               sscanf(buffer,"%s %ld", junk, &nice);
            }
            else if(buffer[0] == 'U')
            {
               sscanf(buffer,"%s %s", junk, username);
            }
         }
         fclose(fp);
         
         if(first)
         {
            first = FALSE;
            fprintf(out,"\nRunning jobs:\n-------------\n");
         }
         

         fprintf(out, "%s: %s %s (for %s, nice %ld)\n",
                 r->node, jobname, jobfile, username, (-1)*nice);
      }
   }
   if(runfiles != NULL)
      fprintf(out,"\n");
}




/************************************************************************/
/*>void Usage(void)
   ----------------
   Prints a usage message

   18.09.00 Original  By: ACRM
   02.10.00 Added -r
*/
void Usage(void)
{

   fprintf(stderr,"\nqllist V1.0 (c) 2000 University of Reading, Dr. \
Andrew C.R. Martin\n");

   fprintf(stderr,"\nUsage: qllist [-t] [-s spooldir] [-c cluster]\n");
   fprintf(stderr,"              -t Print totals only\n");
   fprintf(stderr,"              -s Specify the spool directory\n");
   fprintf(stderr,"                 (Default: %s)\n", DEF_SPOOLDIR);
   fprintf(stderr,"              -c Specify the cluster number\n");
   fprintf(stderr,"              -r View only running jobs\n");

   fprintf(stderr,"\nqllist lists jobs in the QLite queues. By default \
it will list jobs for\n");
   fprintf(stderr,"all clusters of machines. To list jobs for a single \
cluster, specify \n");
   fprintf(stderr,"the cluster using -c. If you simply want to know how \
many jobs are\n");
   fprintf(stderr,"outstanding, use -t\n");

   fprintf(stderr,"\nUse 0 for the cluster number if you are only \
interested in the default\n");
   fprintf(stderr,"cluster.\n\n");
}


