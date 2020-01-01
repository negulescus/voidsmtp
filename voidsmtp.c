/* VOIDSMTP server
 *
 * Copyright 2009 Sorin C. Negulescu
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
*/

/*****************************************************************************/
/*** voidsmtp.c                                                            ***/
/***                                                                       ***/
/*** A SMTP server using processes for multiple, simultaneous connections  ***/
/*** that runs a shell script/command (on the Linksys Router WRT54G/GL)	   ***/
/*** 									   ***/
/*** ATTENTION: Because this server accepts connections from any client it ***/
/*** may be suspected as being an Open SMTP Relay Server, but this is not  ***/
/*** true because it doesn't actually send any message to the inbox of the ***/
/*** user; it discards the message and just execute a command/shell script ***/
/*** (e.g. light a led on the WRT54G/GL router)				   ***/
/*** Filters may be implemented to accept connections only from certain    ***/
/*** servers, but this would make the program larger.			   ***/
/*****************************************************************************/

#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <resolv.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <getopt.h>


//-----------------------------------------------------------------------------
// Global variables
//-----------------------------------------------------------------------------
#define NAME	"VoidSMTP"
#define VERSION	"1.0.0a"
char client_ip[16];	// The IP of the currently connected client
int client_port;	// The tcp/ip port of the currently connected client
#define MAX_ARG_SIZE 50		// Maximum length of the command-line options
int tcpip_listen_port=25;	// The def. port the VoidSMTP server listens on
int log_level=0;		// The default log level [0 .. 3]
char mail_address[MAX_ARG_SIZE]="";	// The email address that VoidSMTP
					// accepts
char mail_command[MAX_ARG_SIZE]="";	// the shell script/command that
					// VoidSMTP executes when an email for
					// the above address arrives
char reset_address[MAX_ARG_SIZE]="";	// The filter (or email address) that
					// VoidSMTP accepts
char reset_command[MAX_ARG_SIZE]="";	// The shell script/command that
					// VoidSMTP executes when the reset
					// address (filter) is accepted

/*****************************************************************************/
/* Signal capture function: SIGCHLD					     */
/*****************************************************************************/
void sig_handler(int sig)
{
	if (sig == SIGCHLD)
	{
		int retval;
		wait(&retval);
	}
}

/*****************************************************************************/
/* Child - SMTP servlet							     */
/*****************************************************************************/
void Child(void* arg)
{   
	// 2 DO
	// IMPLEMENT timeout per-command of 5 - 10 minutes - see RFC 2821
	// IMPLEMENT max number of childrens (clients)

	// From RFC2821 - SMTP
	// The maximum total length of a command line is 512 characters
	// including <CRLF>
	// The maximum total length of a reply line is 512 characters
	// including <CRLF>

	// The total length of a text line is 1000 characters
	// but as we discard the text because we don't care about the
	// content of the message we can use 512 characters.

	//---------------------------------------------------------------------
	// Local variables
	//---------------------------------------------------------------------
	#define MAX_BUF 512
	char line[MAX_BUF];	// (partial) line received from client socket
				// used both for receiving and sending messages
	char echo[MAX_BUF+1];	// used for copying the line(s) received from
				// client
				// Let an extra char for string terminator '\0'
   	int client = *(int *)arg;	// thread id
	int bytes_read;		// number of bytes received from client socket
	int stop;		// condition for exiting the Child SMTP servlet
	int i,r;		// general use
	int data_mode;		// determines if the client sends COMMANDS or
				// DATA (the email message)
	int command_ok;		// determines if a command is correct
	const char data_exit[]="\r\n.\r\n";	// RFC2821 - SMTP DATA command
						// terminator
	const char line_exit[]="\r\n";		// RFC2821 - SMTP line end
	int retval;		// The returned value of the mail command or
				// reset command (retval=system(...))

	// RFC2821 - SMTP COMMANDS definition - RCPT and DATA commands are
	// defined twice because we treat them as being special
	#define TOTAL_COMMANDS 17
	const char commands[TOTAL_COMMANDS][4]={
		"HELO",
		"EHLO",
		"MAIL",
		"RCPT",
		"RCPT",
		"DATA",
		"DATA",
		"RSET",
		"SEND",
		"SOML",
		"SAML",
		"VRFY",
		"EXPN",
		"NOOP",
		"QUIT",
		"TURN",
		"HELP"
	};
	// RFC2821 - SMTP COMMAND REPLIES - RCPT and DATA commands are
	// defined twice because we treat them as being special
	const char replies[TOTAL_COMMANDS][40]={
		"250 Hi there\r\n",				// HELO
		"250 VoidSMTP server @ your service\r\n",	// EHLO
		"250 ok\r\n",					// MAIL
		"250 ok\r\n",					// RCPT
		"550 Invalid mailbox\r\n",			// RCPT2
		"354 Go ahead. End with <CRLF>.<CRLF>\r\n",	// DATA
		"250 ok 1234567890 qp 777\r\n",			// DATA2
		"250 ok\r\n",					// RSET
		"250 ok\r\n",					// SEND
		"250 ok\r\n",					// SOML
		"250 ok\r\n",					// SAML
		"250 ok\r\n",					// VRFY
		"250 ok\r\n",					// EXPN
		"250 ok\r\n",					// NOOP
		"221 Bye 4 now. Hope 2 see u again\r\n",	// QUIT
		"250 ok\r\n",					// TURN
		"214 HELP yourself\r\n"				// HELP
	};

	// Greet the client
	snprintf (line, MAX_BUF, "220 Void SMTP server @ your service\r\n");
        send(client, line, strlen(line), 0);
	// Initially not in data mode (no DATA command received)
	data_mode=0;
	do
	{
		// Initially no need to stop
		stop=0;
		// Pepare the echo buffer
		memset (echo, 0, sizeof(echo));
		do
		{
			// Fill the incoming buffer with 0s
			memset (line, 0, sizeof (line));
			// Set the 3rd param @ sizeof (line)-1 because we want
			// that last byte to be 0 always
			// (becuase we use strcat afterwards)
			bytes_read = recv(client, line, sizeof(line)-1, 0);

			// Check if everything was sent ok
			if ( bytes_read < 0 )
			{
				perror("Read socket");
			};
			
			if (bytes_read == 0)
			{
				// This means that the client has closed
				// the connection
				stop=-1;
				if (log_level <= 2) { syslog(LOG_WARNING,"Conncetion closed by client %s:%d", client_ip, client_port); };
			}
			else
			{
				// Append the received line to the echo buffer
				// but check to see if it fits in MAX_BUF
				// This assures that no buffer overflow will
				// ever happen
				if (strlen(echo)+strlen(line)>MAX_BUF)
				{
					// It doesn't fit in MAX_BUF
					// Rotate left the echo to accommodate
					// the new line
					r=strlen(line)-(MAX_BUF-strlen(echo));
					for (i=0; i<=strlen(echo)-r; i++)
					{
						echo[i]=echo[i+r];
					}
				}
				// Append the received line
				strcat(echo, line);

				// If not in DATA mode then wait for the
				// <CRLF> terminator (line_exit)
				if (data_mode==0)
				{
					if (strstr(echo, line_exit)!=NULL)
					{
						stop=1;
					}
				}
				else
				{
					// In DATA mode - wait for
					// <CRLF>.<CRLF> terminator (data_exit)
					if (strstr(echo, data_exit)!=NULL)
					{
						stop=1;
					}
				}
			}
		} while (stop==0);

		if (stop==1)
		{
			if (data_mode==0)
			{
				// Not in DATA mode so we check for
				// standard commands

				// Convert the echo (SMTP command) to UPPERCASE
				// (only the first 4 chars - to gain speed)
				for (i=0; ((i<4)&&(i<strlen(echo))); i++)
				{
					if ((echo[i]>='a') && (echo[i]<='z'))
					{
						echo[i]=echo[i]-'a'+'A';
					}
				}

				// Log the received SMTP command
				if (log_level==0) { syslog(LOG_INFO, "[%s:%d]->%s", client_ip, client_port, echo); };

				// Assume that the command is ok
				command_ok=0;
				// Cycle through the list of commands we know
				for (i=0;(i<TOTAL_COMMANDS) ;i++)
				{
					// Is the command in the list?
					if (strncmp(echo, commands[i], 4) == 0)
					{
						// The command is in the list of known (defined above) commands

						// Treat DATA command as being special (i=5 => DATA)
						if (i==5)
						{
							// Enter in data mode
							data_mode=1;
						}
						
						// Treat RCPT as being special (index 3 and 4)
						if ((i!=3) && (i!=4))
						{
							// All other commands
							snprintf (line, MAX_BUF, "%s", replies[i]);
						}
						else
						{
							// This is a RCPT command
							// Is this command containing a correct mail address?
							if (strstr(echo, mail_address)!=NULL)
							{
								// Valid mailbox (address/filter)

								// Set the index for the replies array
								i=3;
								// Execute the command that the user asked for
								// when an email arrives and get the exit code
								retval = system(mail_command);
								// Log this event
								if (log_level <= 1) { syslog(LOG_NOTICE, "Valid mail address. Command returned code '%d'", retval); };
							}
							else
							{
								// Is this command containing a correct reset filter?
								if ((strstr(echo, reset_address)!=NULL) && (strlen(reset_address)!=0))
								{
									// The filter is ok but we send a 550 response
									// because we don't want the client to go on further
									// (a good security policy too)

									// Set the index for the replies array
									i=4;
									// Execute the command that the user asked for
									// when a reset filter arrives and get the exit code
									retval = system(reset_command);
									// Log this event
									if (log_level <= 1) { syslog(LOG_NOTICE, "Valid reset address. Command returned code '%d'", retval); };
								}
								else
								{
									// Invalid mailbox (neither of the filters were ok)
									// Set the index for the replies array
									i=4;
								};
							};

							// Prepare the reply to the client with the correct answer
							// the 'i' variable was set above
							snprintf (line, MAX_BUF, "%s", replies[i]);
						}

						// Respond to client with the prepared line
						send(client, line, strlen(line), 0);
						// Log this event
						if (log_level==0) { syslog(LOG_INFO, "[%s:%d]<-%s", client_ip, client_port, line); };
						// The command was ok
						command_ok=1;
						break;
					};
				}; // end for

				if (!command_ok)
				{
					// VoidSMTP doesn't recognize the command as being valid
					// Reply with a 550 code
					snprintf (line, MAX_BUF, "500 Syntax error, command unrecognised.\r\n");
					send(client, line, strlen(line), 0);
					// Log this event
					if (log_level==0) { syslog(LOG_INFO,"[%s:%d]<-%s", client_ip, client_port, line); };
				}
			}
			else
			{
				// In DATA mode
				// Set the index for the replies array
				// (i=6 => DATA2 reply)
				i=6;
				snprintf (line, MAX_BUF, "%s", replies[i]);
				send(client, line, strlen(line), 0);
				data_mode=0;
				// Log this event
				if (log_level==0) { syslog(LOG_INFO,"[%s:%d]<-%s", client_ip, client_port, line); };
			}
		} // end if (stop==1)
	}
	while ((strncmp(echo, "QUIT", 4) != 0)  && (stop==1) );

	// Close the connection
	close(client);

	//Determine if the connection was closed using QUIT command
	if (strncmp(echo, "QUIT", 4)==0)
	{
		if (log_level <= 1) { syslog(LOG_NOTICE,"Conncetion gracefully closed"); };
	}
	// Should exit be here?
	// exit(0);
}


/*****************************************************************************/
/* Print command-line arguments help					     */
/*****************************************************************************/
void print_help(int exitval) {
	printf("%s Server [v%s] help\n", NAME, VERSION); 
	printf("voidsmtp [-p PORT] -m eMAIL_ADDRESS -M eMAIL_COMMAND [-r RESET_ADDRESS] [-R RESET_COMMAND] [-l LOG_LEVEL] [-h] [-v]\n");
	printf(" -p PORT\t\tTCP/IP port (default 25)\n");
	printf(" -m eMAIL_ADDRESS\tset email address (filter)\n");
	printf(" -M eMAIL_COMMAND\tcommand/script to execute\n");
	printf(" -r RESET_ADDRESS\tset email reset address (filter)\n");
	printf(" -R RESET_COMMAND\tcommand/script to execute\n");
	printf(" -l LOG_LEVEL\t\tsyslog verbose level (default 0)\n");	
	printf("    (0 = INFO, 1 = NOTICE, 2 = WARNING, 3 = ERROR)\n");	
	printf(" -h\t\t\tthis help\n");
	exit(exitval);
}

/*****************************************************************************/
/* main - set up VOIDSMTP server and wait for connections		     */
/*****************************************************************************/
int main(int argc, char *argv[])
{
	// Get the command-line arguments
	int opt;
	while((opt = getopt(argc, argv, "p:m:M:r:R:l:hv")) != -1)
	{
		switch(opt)
		{
			case 'h':
				print_help(0);
				break;
			case 'l':
				log_level=atoi(optarg);
				break;
			case 'p':
				tcpip_listen_port=atoi(optarg);
				break;
			case 'm':
				snprintf(mail_address, MAX_ARG_SIZE, "%s", optarg);
				break;
			case 'M':
				snprintf(mail_command, MAX_ARG_SIZE, "%s", optarg);
				break;
			case 'r':
				snprintf(reset_address, MAX_ARG_SIZE, "%s", optarg);
				break;
			case 'R':
				snprintf(reset_command, MAX_ARG_SIZE, "%s", optarg);
				break;
			case ':':
				//printf("%s: Error - Option `%c' needs a value\n\n", NAME, optopt);
				//print_help(1);
				exit(EXIT_FAILURE);
				break;
			case '?':
				//printf("%s: Error - No such option: '%c'\n\n", NAME, optopt);
				//print_help(1);
				exit(EXIT_FAILURE);
				break;
		}
	}

	// Check the -m and -M mandatory arguments (and both -r and -R)
	if (
		(strlen(mail_address)==0) ||	// no mail address
		(strlen(mail_command)==0) ||	// no mail command
		((strlen(reset_address)==0) && (strlen(reset_command)!=0)) ||	// reset address and no reset command
		((strlen(reset_address)!=0) && (strlen(reset_command)==0))	// no reset address and reset command
	)
	{
		print_help(EXIT_FAILURE);
	}

	// Done parsing arguments - initialize the TCP-IP server

	int sd;
	struct sigaction act;
	struct sockaddr_in addr;

	bzero(&act, sizeof(act));
	act.sa_handler = sig_handler;
	act.sa_flags = SA_NOCLDSTOP;
	sigaction(SIGCHLD, &act, 0);
	if ( (sd = socket(PF_INET, SOCK_STREAM, 0)) < 0 )
	{
		syslog(LOG_ERR,"Socket error: %s",strerror(errno));
		exit(EXIT_FAILURE);
	}

	// Set the maximum buffer size
	int window_size = 4; // Should this be 4?
	if (setsockopt(sd, SOL_SOCKET, SO_RCVBUF, (char *) &window_size, sizeof(window_size)) == -1)
	{
		syslog(LOG_ERR,"Socket error: *Cannot set buffer size");
		exit(EXIT_FAILURE);
	}

	// Get rid of the "address already in use" error message
	char yes='1';
	if (setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
	{
		syslog(LOG_ERR,"Socket error:* Cannot reuse socket");
		exit(EXIT_FAILURE);
	}

	// Bind to socket
	addr.sin_family = AF_INET;
	addr.sin_port = htons(tcpip_listen_port);
	addr.sin_addr.s_addr = INADDR_ANY;
	if ( bind(sd, (struct sockaddr*)&addr, sizeof(addr)) != 0 )
	{
		syslog(LOG_ERR,"Bind error: %s",strerror(errno));
		exit(EXIT_FAILURE);
	}

	// Enter in listening state. Maximum of 10 clients @ a given time
	// - don't know if this realy works
	if ( listen(sd, 10) != 0 )
	{
		syslog(LOG_ERR,"Listen error: %s",strerror(errno));
		exit(EXIT_FAILURE);
	}

	// Log the Server Started event
	syslog(LOG_INFO,"VoidSMTP Server started using port %d",tcpip_listen_port);

	while (1)
	{
		int client;
		socklen_t addr_size = sizeof(addr);

		client = accept(sd, (struct sockaddr*)&addr, &addr_size);
		if ( client < 0 )
		{
			if (log_level <= 1) { syslog(LOG_NOTICE,"ACCEPT: %s",strerror(errno)); };
		}
		else
		{
			// Store client IP and port
			snprintf (client_ip, 16, "%s", inet_ntoa(addr.sin_addr));
			client_port = ntohs(addr.sin_port);

			// Log the Client Connected event
			if (log_level <= 1) { syslog(LOG_NOTICE,"Connected %s:%d", client_ip, client_port); };

			// Start/Stop the SMTP servlet
			if ( fork() )
			{
				close(client);
			}
			else
			{
				close(sd);
				Child(&client);
				exit(0);
			}
		}
	}
	// Will never get here
    	return 0;
}

