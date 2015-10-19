
/*************************************************************************

   Program:    RasMol2
   File:       rascon.c
   
   Version:    2.6.4a
   Date:       29.04.99
   Function:   BSD sockets based RasMol console
   
   Copyright:  
   Author:     Robert Miller
   Address:    
               
   Phone:      
   EMail:      
               
**************************************************************************

   CVS Tags:
   =========

   Last modified by:    $Author: andrew $
   Tag:                 $Name:  $
   Revision:            $Revision: 1.1 $

**************************************************************************

   This program is copyright. Any copying without permission is illegal.

**************************************************************************

   Description:
   ============
 program to talk to rasmol through sockets rtm 22.I.99 
 much of this hacked mercilessly from the usenet socket faq examples code 

**************************************************************************

   Usage:
   ======

**************************************************************************

   Notes:
   ======

**************************************************************************

   Revision History:
   =================
   2.6.4 22.01.99 Original   By: RTM 

*************************************************************************/
/* Includes
*/
#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h> 
#include <signal.h>
#include <termio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>

/************************************************************************/
/* Defines
*/

/************************************************************************/
/* Globals
*/
struct termio savetty;

/************************************************************************/
/* Prototypes
*/

/***********************************************************************/
/*>int atoport(char *service, char *proto)                             */
/* -----------------------------------                                 */
/* Input:     char     *service  /etc/services entry                   */
/*            char     *proto    protocol (tcp/udp)                    */
/* Returns:   int      port                                            */
/*                                                                     */
/* Preconditions:      PRECON0                                         */
/*                     PRECON1                                         */
/* Postconditions:     POSTCON0                                        */
/*                     POSTCON1                                        */
/*                                                                     */
/*  from sockhelp.c of the usenet sockets faq examples                 */
/* Take a service name, and a service type, and return a port number.  */
/* If the service name is not found, it tries it as a decimal number.  */
/* The number returned is byte ordered for the network.                */
/*                                                                     */
/* 20.01.96 Original   By: Vic Metcalfe                                */
/*                                                                    <*/

int atoport(char *service, char *proto)
{
  int port;
  long int lport;
  struct servent *serv;
  char *errpos;

  /* First try to read it from /etc/services */
  serv = getservbyname(service, proto);
  if (serv != NULL)
    port = serv->s_port;
  else { /* Not in services, maybe a number? */
    lport = strtol(service,&errpos,0);
    if ( (errpos[0] != 0) || (lport < 1) || (lport > 65535) )
      return -1; /* Invalid port address */
    port = htons(lport);
  }
  return port;
}

/***********************************************************************/
/*>struct in_addr *atoaddr(char *address)                              */
/* -----------------------------------                                 */
/* Input:     char     *address  host or IP                            */
/* Returns:   in_addr  saddr     in_addr struct for sockets code       */
/*                                                                     */
/* Preconditions:      *address valid string                           */
/*                                                                     */
/* Converts ascii text to in_addr struct.  NULL is returned if the     */
/* address can not be found.                                           */
/*                                                                     */
/* 20.01.96 Original   By: Vic Metcalfe                                */
/*                                                                    <*/

struct in_addr *atoaddr(char *address)
{
  struct hostent *host;
  static struct in_addr saddr;

  /* First try it as aaa.bbb.ccc.ddd. */
  saddr.s_addr = inet_addr(address);
  if (saddr.s_addr != -1) {
    return &saddr;
  }
  host = gethostbyname(address);
  if (host != NULL) {
    return (struct in_addr *) *host->h_addr_list;
  }
  return NULL;
}

/***********************************************************************/
/*>int make_connection(char *service,int type,char *netaddress)        */
/* -----------------------------------                                 */
/* Input:     char     *service  /etc/services entry                   */
/*            int      type      STREAM or DGRAM                       */
/*            char     *netaddress                                     */
/* Returns:   int      socket                                          */
/*                                                                     */
/* This is a generic function to make a connection to a given          */
/* server/port.  service is the port name/number, type is either       */
/* SOCK_STREAM or SOCK_DGRAM, and netaddress is the host name to       */
/* connect to.  The function returns the socket, ready for action.     */
/*                                                                     */
/* 20.01.96 Original   By: Vic Metcalfe                                */
/*                                                                    <*/

int make_connection(char *service,int type,char *netaddress)
{
  /* First convert service from a string, to a number... */
  int port = -1;
  struct in_addr *addr;
  int sock, connected;
  struct sockaddr_in address;

  if (type == SOCK_STREAM) 
    port = atoport(service, "tcp");
  if (type == SOCK_DGRAM)
    port = atoport(service, "udp");
  if (port == -1) {
    fprintf(stderr,"make_connection:  Invalid socket type.\n");
    return -1;
  }
  addr = atoaddr(netaddress);
  if (addr == NULL) {
    fprintf(stderr,"make_connection:  Invalid network address.\n");
    return -1;
  }
 
  memset((char *) &address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_port = (port);
  address.sin_addr.s_addr = addr->s_addr;

  sock = socket(AF_INET, type, 0);

  printf("Connecting to %s on port %d.\n",inet_ntoa(*addr),htons(port));

  if (type == SOCK_STREAM) {
    connected = connect(sock, (struct sockaddr *) &address, 
      sizeof(address));
    if (connected < 0) {
      perror("connect");
      return -1;
    }
    return sock;
  }
  /* Otherwise, must be for udp, so bind to address. */
  if (bind(sock, (struct sockaddr *) &address, sizeof(address)) < 0) {
    perror("bind");
    return -1;
  }
  return sock;
}

/** end sockhelp.c extracts (usenet sockets faq examples)  **/



/***********************************************************************/
/*>void sigcatch( void )                                               */
/* -----------------------------------                                 */
/* Input:     void                                                     */
/* Output:                       exit(0)                               */
/* Returns:   void                                                     */
/*                                                                     */
/* Postconditions:     restore tty, exit(0)                            */
/*                                                                     */
/* SIGINT handler                                                      */
/*                                                                     */
/* 08.02.99 Original   By: RTM                                       */
/*                                                                    <*/

void sigcatch( int arg ) 
{
   ((arg)=(arg));
  ioctl(0,TCSETAF,&savetty);
  exit(0);
}

/***********************************************************************/
/*>int main(int argc, char **argv)                                     */
/* -----------------------------------                                 */
/* Input:     int      argc      arg count                             */
/*            char     **argv    arg strings                           */
/* Returns:   int                                                      */
/*                                                                     */
/* main() for rascon.  open connection to host and port as specified   */
/* on command line, loop sending stdin to port, port data to stdout.   */
/*                                                                     */
/* 08.02.99 Original   By: RAS                                         */
/*                                                                    <*/


int main(int argc, char **argv)
{
  int sock;
  int connected = 1;
  fd_set rfds;
  /*  struct timeval tv; */
  struct termio newtty;

  if (argc != 3) {
    fprintf(stderr,"Usage:  tcpclient host port\n");
    fprintf(stderr,"where host is the machine which is running the\n");
    fprintf(stderr,"tcpserver program, and port is the port it is\n");
    fprintf(stderr,"listening on.\n");

    exit(EXIT_FAILURE);
  }



  sock = make_connection(argv[2], SOCK_STREAM, argv[1]);
  if (sock == -1) {
    fprintf(stderr,
	    "make_connection failed for host %s  port %s.\n",argv[1],argv[2]);
    return -1;
  }
  signal(SIGPIPE,SIG_IGN);
  signal(SIGINT,sigcatch);
  if (ioctl(0,TCGETA,&savetty) < 0) {
    fprintf(stderr,"this is not a tty\n");
    exit(EXIT_FAILURE);
  }

  newtty=savetty;
  newtty.c_iflag |= IGNBRK|IGNPAR;
  newtty.c_iflag &= ~(BRKINT|PARMRK|INPCK|IXON|IXOFF);
  newtty.c_lflag &= ~(ICANON|ECHO|ECHOE|ECHOK|ECHONL|NOFLSH);
  newtty.c_lflag |= ISIG;
  
  newtty.c_cc[VMIN] = 1;
  newtty.c_cc[VTIME] = 0;

  if (ioctl(0,TCSETA,&newtty) < 0) {
    perror("set no echo");
    exit(EXIT_FAILURE);
  }


  if (fcntl(sock, F_SETFL, O_NONBLOCK) < 0) {
    perror("fcntl socket F_SETFL, O_NONBLOCK"); 
    return(-1);
  } 


  if (fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK) < 0) {
    perror("fcntl stdin F_SETFL, O_NONBLOCK"); 
    return(-1);
  } 

  while (connected) {
    int rv=0;

    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);
    FD_SET(sock, &rfds); 

    rv = select((sock+1),&rfds,NULL,NULL,NULL);

    if (rv >0) {
      ssize_t rc,wc;

      if(FD_ISSET(STDIN_FILENO, &rfds)) {
	int inchar;

	while (((inchar = getchar()) != EOF) 
	       && (wc = write(sock,(char *) &inchar,1)));

	if (wc != 1) {
	  fprintf(stderr,"close on socket write failure : %d != %d\n",wc,rc);
	  connected = 0;
	}

      } 
      if(FD_ISSET(sock, &rfds)) {
	char inchar;

	rc = read(sock,&inchar,1); 
	wc = write(STDOUT_FILENO,&inchar,1);
	if (rc <= 0 ) {
	  fprintf(stderr,"close on socket read failure : %d \n",rc);
	  connected = 0;
	}
      } 
    }  else if (rv < 0) perror("select");

  }

  printf("<CLOSED>\n");
  close(sock);
  sigcatch(1);
  exit(EXIT_SUCCESS);
}


