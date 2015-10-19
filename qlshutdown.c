/*************************************************************************

   Program:    qlshutdown
   File:       qlshutdown.c
   
   Version:    V1.1
   Date:       18.09.00
   Function:   Shuts down the QLite daemon on a specified machine giving
               the current job time to finish
   
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
#ifdef __linux__
#  include <linux/limits.h>
#else
#  include <limits.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>

#include "qlutil.h"

/************************************************************************/
/* Defines and macros
*/
#define MAXBUFF 160

/************************************************************************/
/* Globals
*/

/************************************************************************/
/* Prototypes
*/
int  main(int argc, char **argv);
BOOL ParseCmdLine(int argc, char **argv, char *node, char *spoolDir, 
                  int *cluster, BOOL *waitForJob);
void Usage(void);
BOOL CreateShutdownFile(char *node, char *spoolDir, BOOL waitForJob);

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
   char spoolDir[PATH_MAX],
         node[MAXBUFF],
        *env;
   BOOL waitForJob = FALSE;
   int  cluster = 0;

   /* Get the default spool directory from the environment variable if
      this has been set
   */
   if((env=getenv("QLSPOOLDIR"))!=NULL)
      strcpy(spoolDir, env);
   else
      strcpy(spoolDir, DEF_SPOOLDIR);

   if(ParseCmdLine(argc, argv, node, spoolDir, &cluster, &waitForJob))
   {
      if(!RootUser())
      {
         fprintf(stderr,"qlshutdown must be run as root\n");
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
         
      if(!CreateShutdownFile(node, spoolDir, waitForJob))
      {
         fprintf(stderr,"Unable to create shutdown file. You will have \
to kill qlrun manually\n");
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
/*>BOOL ParseCmdLine(int argc, char **argv, char *node, char *spoolDir, 
                     int *cluster, BOOL *waitForJob)
   --------------------------------------------------------------------
   Input:     int    argc        Argument count
              char   *argv       Arguments
   Output:    char   *spoolDir   Spool directory
              int    *cluster    Cluster number
              BOOL   *waitForJob Wait for Job to finish before returning
   Returns:   BOOL               Success

   Parses the command line

   15.09.00 Original  By: ACRM
*/
BOOL ParseCmdLine(int argc, char **argv, char *node, char *spoolDir,
                  int *cluster, BOOL *waitForJob)
{
   argc--;
   argv++;

   node[0] = '\0';

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
         case 'w':
            *waitForJob = TRUE;
            break;
         case 'c':
            argv++;
            argc--;
            if(!argc)
               return(FALSE);
            if(!sscanf(argv[0],"%d", cluster))
               return(FALSE);
            break;
         default:
            return(FALSE);
            break;
         }
      }
      else
      {
         if(argc>1)
            return(FALSE);

         strcpy(node, argv[0]);
         return(TRUE);
      }
      
      argc--;
      argv++;
   }
   
   return(TRUE);

}

/************************************************************************/
/*>BOOL CreateShutdownFile(char *hname, char *spoolDir, BOOL waitForJob)
   ---------------------------------------------------------------------
   Input:   char   *hname       Host name or zero-length string
            char   *spoolDir    Spool directory
            BOOL   waitForJob   Wait for the daemon to finish?
   Returns: BOOL                Created OK?

   Creates a file which indicates to the qlrun daemon that it should
   shutdown.

   18.09.00 Original   By: ACRM
*/
BOOL CreateShutdownFile(char *hname, char *spoolDir, BOOL waitForJob)
{
   char *chp, 
        buffer[PATH_MAX];
   FILE *fp;
   
   if(hname[0] == '\0')
   {
      /* Try to get the hostname - just return FALSE if we fail         */
      if(gethostname(hname,MAXBUFF))
         return(FALSE);
   }
   
   /* Terminate the hostname at the first .                             */
   if((chp=strchr(hname,'.'))!=NULL)
      *chp = '\0';
   
   /* Create the name of the file which indicates this daemon to be
      shutdown
   */
   sprintf(buffer,"%s/.kill%s", spoolDir,hname);

   /* Create the file                                                   */
   if((fp=fopen(buffer, "w"))==NULL)
      return(FALSE);
   fclose(fp);

   if(waitForJob)
   {
      sprintf(buffer,"%s/.shutdown%s", spoolDir,hname);
      while(!access(buffer,F_OK))
      {
         sleep(1);
      }
   }
   
   return(TRUE);
}


/************************************************************************/
/*>void Usage(void)
   ----------------
   Prints a usage message

   18.09.00 Original  By: ACRM
*/
void Usage(void)
{
   fprintf(stderr, "\nqlshutdown V1.0 (c) 2000 University of Reading, \
Dr. Andrew C.R. Martin\n");

   fprintf(stderr, "\nUsage: qlshutdown [-s spooldir] [-c cluster] [-w] \
[node]\n");
   fprintf(stderr, "       -s Specify the spool directory\n");
   fprintf(stderr, "       -c Specify the cluster number\n");
   fprintf(stderr, "       -w Wait for the node to finish\n");

   fprintf(stderr,"\nqlshutdown creates a flag file to tell the daemon \
on a node to shutdown.\n");
   fprintf(stderr,"The currently running job is always allowed to \
finish first. If the -w\n");
   fprintf(stderr,"flag is given then the qlshutdown program waits for \
the daemon to\n");
   fprintf(stderr,"shutdown. This is useful in the shutdown script for \
a computer.\n");

   fprintf(stderr,"\nIf the node is unspecified, then the daemon on the \
current node is\n");
   fprintf(stderr,"shutdown.\n");

   fprintf(stderr,"\nqlshutdown must be run as root\n\n");
}

