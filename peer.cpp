//#include <sys/types.h>
//#include <sys/socket.h>
//#include <netinet/in.h>
#include <netinet/tcp.h>
//#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <netdb.h>
#include <ctype.h>
#include <time.h>
#include <pthread.h>
#include <iostream>
#include <cstdlib>

#define DEFAULT_LISTENING_PORT 5000
#define PEER_COUNT 2				// ID starts from 0
#define MAXBUF 1000

class CPeerInfoList {
public:
	int m_nClient;
	int m_narrFDList [1000];
	int m_narrID [1000];
	
	CPeerInfoList () {
		m_nClient = 0;
	};
};

int conn(char *host, int port);
void disconn(void);
void* peer_handler (void* ptr);
CPeerInfoList infoList;
int myID = -1;
int rand();
int freeze = 0;
int state = 1;
int myrequesttimestamp;
/*
0 = null
1 = wanted
2 = held
3 = released
*/
int requestQueue[100];
int requestQueueLen;

int replycount;
int requiredreplies;

uint localclock = 0;


void* peer_handler (void* ptr) {
	int nDesc = *((int*) ptr);
	printf("Client Handler Activated for FD %d\n", nDesc);
	char buf[MAXBUF];
	int len;
	
	while (1)
	{
		if ((len = read(nDesc, buf, MAXBUF)) > 0)
		{
			int messagestart = 0;
			while (len-1 > messagestart)
			{
				int charset = 0;
				char tclock[8];
				int tclocklen = 0;
				char tempfromID[8];
				int fromIDlen = 0;
				char message[MAXBUF];
				int messagelen = 0;
				int i;
				for (i = messagestart; i < len; i++)
				{
					if (buf[i] == '[')
					{ charset++; }
					else if (buf[i] == ']')
					{
						charset++;
						if (charset > 3)
						{ char message[len-i]; }
					}
					else
					{
						if (charset == 1)
						{
							tempfromID[tclocklen] = buf[i];
							fromIDlen++;
						}
						else if (charset == 3)
						{
							tclock[tclocklen] = buf[i];
							tclocklen++;
						}
						else
						{
							message[messagelen] = buf[i];
							messagelen++;
						}
					}
					if (charset > 4)
					{ break; }
				}
				messagestart = i;


				uint fromclock = atoi(tclock);
				int fromID = atoi(tempfromID);
				
				//printf ("%s\n", buf); // raw message
				//printf("fromclock:%d myclock:%d\n", fromclock, localclock);

				if (fromclock > localclock)
				{ localclock = fromclock; }
				localclock++;

				if (!strcmp(message, "request"))
				{
					//printf("[%d][%d]%s\n", fromID, fromclock, message);
					if (state == 2) // 2 = held
					{
						requestQueue[requestQueueLen] = fromID;
						requestQueueLen++;
					}
					else if (state == 1 && myrequesttimestamp > 0 && 	   // 1 = wanted
					(fromclock > myrequesttimestamp ||  // if the local clock is higher, then exit immediately as incoming request has priority
					(fromclock == myrequesttimestamp && // if the requesting clock is higher, then this process has priority
					fromID > myID)))            // if the clocks are the same, then the lower ID has priority 
					{
						requestQueue[requestQueueLen] = fromID;
						requestQueueLen++;
					}
					else
					{
						localclock++;
						std::string replymessage = "[" + std::to_string(myID) + "][" + std::to_string(localclock) + "]reply" /*+ std::to_string(rand()%900+100)*/; //+ localclock + " from " + myID;
						char buf[MAXBUF];
						strcpy(buf, replymessage.c_str());

						//printf ("%s (to %d) ([%d][%d] vs [%d][%d])\n", buf, fromID, fromID, fromclock, myID, myrequesttimestamp);
						write(infoList.m_narrFDList[fromID], buf, strlen(buf) + 1);
					}
				}
				else if (!strcmp(message, "reply"))
				{
					//printf("[%d][%d]%s\n", fromID, fromclock, message);
					replycount++;
				}
				else
				{ printf("[%d][%d]%s\n", fromID, fromclock, message); }

				//printf("fromID:%d fromclock:%d message:%s\n", fromID, fromclock, message);
			}
		}
	}
}

void* debugger (void* print)
{
	while (1)
	{
		sleep(1);
		//printf("state:%d requests:%d replies:%d required:%d\n", state, requestQueueLen, replycount, requiredreplies);
		freeze++;
		if (freeze > 15)
		{
			printf("frozen\n");
			exit(1);
		}
	}
}

void* messager (void* pID)
{
	sleep(3);
	while (1)
	{
		freeze = 0;
		state = 1; //wanted
		localclock++;
		char buf[MAXBUF];
		std::string message = "[" + std::to_string(myID) + "][" + std::to_string(localclock) + "]request" /*+ std::to_string(rand()%900+100)*/; //+ localclock + " from " + myID;
		strcpy(buf, message.c_str());
		myrequesttimestamp = localclock;
		//printf ("%s\n", buf);

		int i;
		requiredreplies = infoList.m_nClient;
		for (i = 0; i <= infoList.m_nClient; i++)
		{
			if (i != myID)
			{ write(infoList.m_narrFDList[i], buf, strlen(buf) + 1); }
		}
		//std::cout << "peer count:" << infoList.m_nClient << std::endl;
		while (replycount < requiredreplies) // state == 1
		{ /* wait for replies */ }
		state = 2;
		myrequesttimestamp = 0;
		replycount = 0;
		message = "[" + std::to_string(myID) + "][" + std::to_string(localclock) + "] entered critical section";
		strcpy(buf, message.c_str());
		printf ("%s\n", buf);
		for (i = 0; i <= infoList.m_nClient; i++)
		{
			if (i != myID)
			{ write(infoList.m_narrFDList[i], buf, strlen(buf) + 1); }
		}
		sleep(1); // time held
		
		localclock++;
		int tempclock = localclock;
		message = "[" + std::to_string(myID) + "][" + std::to_string(tempclock) + "] left critical section";
		strcpy(buf, message.c_str());
		printf ("%s\n", buf);
		for (i = 0; i <= infoList.m_nClient; i++)
		{
			bool send = true;
			int j;
			for (j = 0; j < requestQueueLen; j++)
			{
				if (i == requestQueue[j])
				{ send = false; }
			}
			if (i != myID && send)
			{ write(infoList.m_narrFDList[i], buf, strlen(buf) + 1); }
		}
		
		message = "[" + std::to_string(myID) + "][" + std::to_string(tempclock) + "] left critical section[" + std::to_string(myID) + "][" + std::to_string(tempclock+1) + "]reply";
		localclock++;
		strcpy(buf, message.c_str());
		message = "[" + std::to_string(myID) + "][" + std::to_string(tempclock+1) + "]reply";
		state = 3;
		if (requestQueueLen > 0)
		{
			for (i = 0; i < requestQueueLen; i++)
			{
				write(infoList.m_narrFDList[requestQueue[i]], buf, strlen(buf) + 1);
				//printf ("%s (to %d)\n", message.c_str(), requestQueue[i]);
			}
			requestQueueLen = 0;
		}
		sleep(1);
	}
}

int main (int argc, char* argv [])
{
	if (argc <= 1)
	{ printf("peer id is required\n"); return 1; }
	int nPeerID = myID = atoi (argv [1]);
	std::string filename = "peer " + std::to_string(myID) + " output.txt";
	//freopen(filename.c_str(),"w",stdout);
	int nListeningPortNumber = DEFAULT_LISTENING_PORT + nPeerID;
	
	
	// attempt to connect the other peers whose ID is smaller than the current one
	printf ("My Peer ID is %d\n", nPeerID);

	int i = 0;
	
	for (i = 0; i < nPeerID; i++)
	{
		extern char *optarg;
		extern int optind;
		int c, err = 0; 
		char *prompt = 0;
		int port = DEFAULT_LISTENING_PORT + i;	/* default: whatever is in port.h */
		const char* host = "localhost";	/* default: this host */
		int fd;
 
		printf("connecting to %s:%d\n", host, port);
				
		if (!(fd = conn((char*) host, port)))    /* connect */
		{ exit(1); }   /* something went wrong */

		printf("done.\n");
	
		infoList.m_narrFDList [infoList.m_nClient] = fd;
		infoList.m_narrID [infoList.m_nClient] = i;
		infoList.m_nClient++;
		
		pthread_t thread;
		pthread_create(&thread, NULL, peer_handler, (void*) &fd);			
	}

	pthread_t debugthread;
	pthread_create(&debugthread, NULL, debugger, (void*) &nPeerID);			

	// listen connection request
	{			
		int n, s, ns, len;
		struct sockaddr_in name;	
		int nPortNumber = nListeningPortNumber;
		int nClientFileDescriptor = -1;
		
		// create a new socket object
		// AF_INET: create IP based socket
		// SOCK_STREAM: create TPC based socket
		if ((s = socket (AF_INET, SOCK_STREAM, 0)) < 0)
		{
			perror ("socket");
			exit (1);
		}
		
		// setup the new socket for connection
		int flag = 1;
		setsockopt(s,       								/* socket affected              */
		IPPROTO_TCP,    								 	/* set option at TCP level      */
		TCP_NODELAY,     									/* name of option               */
		(char *) &flag,  									/* the cast is historical cruft */
		sizeof(int));    									/* length of option value       */
		
		// create a server address 
		memset (&name, 0, sizeof (struct sockaddr_in));
		name.sin_family = AF_INET;
		name.sin_port = htons (nPortNumber);
		len = sizeof (struct sockaddr_in);
		
		// use a wildcard address 
		n = INADDR_ANY;
		memcpy (&name.sin_addr, &n, sizeof (long));
		
		// assign the address to the socket
		if (bind (s, (struct sockaddr *) &name, len) < 0)
		{
			perror ("bind");
			exit (1);
		}
		
		pthread_t thread2;
		pthread_create(&thread2, NULL, messager, (void*) &nPeerID);


		while (1) {
			// waiting for a connection
			if (listen (s, 5) < 0)
			{
				perror ("listen");
				exit (1);
			}
		
			// accept the connection
			if ((nClientFileDescriptor = accept (s, (struct sockaddr *) &name, (socklen_t*) &len)) < 0)
			{
				perror ("accept");
				exit (1);
			}
			else
			{
				infoList.m_nClient++;
				infoList.m_narrFDList [infoList.m_nClient] = nClientFileDescriptor;
				infoList.m_narrID [infoList.m_nClient] = infoList.m_nClient;
				std::cout << "accepted connection:" << infoList.m_nClient << " FD:" << infoList.m_narrFDList [infoList.m_nClient-1] << std::endl;
				// find out who this is (ID?)
				pthread_t thread;
				pthread_create(&thread, NULL, peer_handler, (void*) &nClientFileDescriptor);
			}
		}
			
		//////////////////////// connection is made ////////////////////////////			 
		 
		close (s);
	}
	
	
	return 0;
}

/* conn: connect to the service running on host:port */
/* return 0 on failure, non-zero on success */
int conn(char *host, int port)
{
	int fd;  /* fd is the file descriptor for the connected socket */
	struct hostent *hp;	/* host information */
	unsigned int alen;	/* address length when we get the port number */
	struct sockaddr_in myaddr;	/* our address */
	struct sockaddr_in servaddr;	/* server address */

	//printf("conn(host=\"%s\", port=\"%d\")\n", host, port);

	/* get a tcp/ip socket                    */
	/* We do this as we did it for the server */
	/* request the Internet address protocol  */
	/* and a reliable 2-way byte stream       */

	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		perror("cannot create socket");
		return 0;
	}

	/* bind to an arbitrary return address */
	/* because this is the client side, we don't care about the */
	/* address since no application will connect here  --- */
	/* INADDR_ANY is the IP address and 0 is the socket */
	/* htonl converts a long integer (e.g. address) to a network */
	/* representation (agreed-upon byte ordering */

	memset((char *)&myaddr, 0, sizeof(myaddr));
	myaddr.sin_family = AF_INET;
	myaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	myaddr.sin_port = htons(0);		// automatically select available local port number for client

	if (bind(fd, (struct sockaddr *)&myaddr, sizeof(myaddr)) < 0)
	{
		perror("bind failed");
		return 0;
	}

	/* this part is for debugging only - get the port # that the operating */
	/* system allocated for us. */
    alen = sizeof(myaddr);
    if (getsockname(fd, (struct sockaddr *)&myaddr, &alen) < 0)
    {
        perror("getsockname failed");
        return 0;
    }
	printf(" local port number = %d\n", ntohs(myaddr.sin_port));

	/* fill in the server's address and data */
	/* htons() converts a short integer to a network representation */

	memset((char*)&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(port);

	/* look up the address of the server given its name */
	hp = gethostbyname(host);
	if (!hp) {
		fprintf(stderr, "could not obtain address of %s\n", host);
		return 0;
	}

	/* put the host's address into the server address structure */
	memcpy((void *)&servaddr.sin_addr, hp->h_addr_list[0], hp->h_length);

	/* connect to server */
	if (connect(fd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
	{
		perror("connect failed");
		return 0;
	}
	return fd;
}

/* disconnect from the service /
void disconn(void)
{
	printf("disconn()\n");
	shutdown(fd, 2);    // 2 means future sends & receives are disallowed
} /**/
