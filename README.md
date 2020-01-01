# voidsmtp
Lightweight SMTP server able to run custom system commands

VoidSMTP Server (v1.0.0) readme file

VoidSMTP Server is a software designed to work on Tomato operating system / firmware (but not limited to). Its intended use is to run a user-specified shell script/command when an email arrives. Tested on the WRT54-GL Router.
In order for this to work, you must follow these steps:
1. Enable the /jffs/ partition on the Tomato router (Administration - JFFS2)
2. Copy the voidsmtp application to /jffs/voidsmtp
3. Forward the port 25 on the router as follows: on [v] / Proto [TCP] / Ext Ports [25] / Int address [the LAN IP of the router (192.168.0.1 or 192.168.1.1, etc.)] / Description [VoidSMTP]
4. You must have a (d)dns name associated with your router WAN IP (in the folowing script the "domain.name" is considered to be your domain name).
5. Add the following lines to the Firewall Script (Administration - Scripts):

/jffs/voidsmtp –m "email_address@domain.name" -M "led amber on" -r "my_password" -R "led amber off" &
iptables -A INPUT -p tcp --dport smtp -j ACCEPT -d !!!LAN_IP!!!

(Explanation: the first line is starting the voidsmtp server and the second is telling the router to route the connections on port 25 from the WAN port to the LAN IP of the router)

The parameters for the VoidSMTP are:

voidsmtp [-p PORT] -m eMAIL_ADDRESS -M eMAIL_COMMAND [-r RESET_ADDRESS] [-R RESET_COMMAND] [-l LOG_LEVEL] [-h] [-v]
 -p PORT                TCP/IP port (default 25)
 -m eMAIL_ADDRESS       set email address (filter)
 -M eMAIL_COMMAND       command/script to execute
 -r RESET_ADDRESS       set email reset address (filter)
 -R RESET_COMMAND       command/script to execute
 -l LOG_LEVEL           syslog verbose level (default 0) (0 = INFO, 1 = NOTICE, 2 = WARNING, 3 = ERROR)
 -h                     help

Working example:

- jffs partition activated
- voidsmtp is in /jffs/voidsmtp

- Router WAN IP address = 84.232.101.13
- Router LAN IP address = 192.168.0.1
- DDNS Name = test.bounceme.net

- the command we want to run when an email is received at "my_router@test.bounceme.net" is "led white on".
- the command we want to run when an email is received at "my_strong_and_long_password" is "led white off".

- the corresponding Firewall script is:

/jffs/voidsmtp –m "my_router@test.bounceme.net" -M "led white on" -r "my_strong_and_long_password" -R "led white off" &
iptables -A INPUT -p tcp --dport smtp -j ACCEPT -d 192.168.0.1

- configure your email account (for example my_account@gmail.com) to forward incoming emails to "my_router@test.bounceme.net" and keep a copy of the email in the inbox.

Test the configuration (using telnet):
- start a cmd session (in windows) or terminal (linux)
- telnet to the router on port 25:
telnet 192.168.0.1 25
- the following message should appear:
220 Void SMTP server @ your service
- try some SMTP commands:
HELO 192.168.0.1
- you should get the response: "250 Hi there"
RCPT my_router@test.bounceme.net
- you should get the response: "250 ok" and see the white led lighting up
RCPT my_strong_and_long_password
- you should get the response: "550 Invalid mailbox" and see the white led turning off
QUIT
- you should get the response: "221 Bye 4 now. Hope 2 see u again"

Test the configuration sending an email:
- send an email to your address: my_account@gmail.com or directly to the router: my_router@test.bounceme.net.
- the white led should light up.

After reading your emails at my_account@gmail.com you may want to shut down the white led. For this you should configure your router to run the "led white off" command when the button on the router is pressed. So in the administration page of the router at the Administration/Buttons/LED you should set the button to run the following script when the button is pressed for x seconds:
led white on
[ $1 -ge 20 ] && telnetd -p 233 -l /bin/sh
The first line turns off the white led and the second line is optional (it is there by default in Tomato to start up the telnet daemon on port 233)

2 DO
Implement the code to act as a daemon (good example at http://www.netzmafia.de/skripten/unix/linux-daemon-howto.html);
