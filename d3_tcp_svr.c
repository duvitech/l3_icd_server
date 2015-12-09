  /*****************************************************************************
  * @file    d3_tcp_svr.c
  * @author  George Vigelette
  * @version V1.0.0
  * @date    Sep 28, 2015
  * @brief   Contains the implementation for the TCP Relay Server.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; COPYRIGHT 2015 Duvitech </center></h2>
  *
  * Unless required by applicable law or agreed to in writing, software
  * distributed under the License is distributed on an "AS IS" BASIS,
  * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  * See the License for the specific language governing permissions and
  * limitations under the License.
  *
  ******************************************************************************
  */

#include "d3_tcp_svr.h"

#include "d3_command_proc.h"
#include "nanocore_control_if.h"

#include <stdio.h>
#include <string.h>    //strlen
#include <stdlib.h>    //strlen
#include <sys/socket.h>
#include <arpa/inet.h> //inet_addr
#include <unistd.h>    //write
#include <pthread.h> //for threading , link with lpthread
#include <signal.h>

#define BUFFSIZE 255

extern char* portname;
int fd = 0;
static int gst_pid = -1;

pthread_mutex_t lock;
static char gstreamCommand[440];

//the thread function
void *connection_handler(void *);

int start_tcp_listener(void){

    int socket_desc , client_sock , c;
    struct sockaddr_in server , client;

    // create mutex
    if (pthread_mutex_init(&lock, NULL) != 0)
	{
		puts("\n mutex init failed\n");
		return 1;
	}

    // open serial port
    fd = open_port(portname);
	if(fd<0){
		puts("error opening port\n");
		return 1;
	}else
	{
		puts("COM Port opened\n");
	}

    //Create socket
	socket_desc = socket(AF_INET , SOCK_STREAM , 0);
	if (socket_desc == -1)
	{
		printf("Could not create socket");
	}
	puts("Socket created");

	//Prepare the sockaddr_in structure
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons( 8024 );

    //Bind
    if( bind(socket_desc,(struct sockaddr *)&server , sizeof(server)) < 0)
    {
        //print the error message
        perror("bind failed. Error");
        return 1;
    }
    puts("Bind Done");

    //Listen
    listen(socket_desc , 3);

    //Accept and incoming connection
    puts("Waiting for incoming connections...");
    c = sizeof(struct sockaddr_in);
	pthread_t thread_id;


    while( (client_sock = accept(socket_desc, (struct sockaddr *)&client, (socklen_t*)&c)) )
    {
        puts("Connection accepted");

        if( pthread_create( &thread_id , NULL ,  connection_handler , (void*) &client_sock) < 0)
        {
            perror("could not create thread");
            return 1;
        }

        //Now join the thread , so that we dont terminate before the thread
        //pthread_join( thread_id , NULL);
        puts("Handler assigned");
    }

    if (client_sock < 0)
    {
        perror("accept failed");
        return 1;
    }

	close_port();
	pthread_mutex_destroy(&lock);

    return 0;

}

void *connection_handler(void *socket_desc)
{
    //Get the socket descriptor
    int sock = *(int*)socket_desc;
    FILE *pidFile;
    int read_size;
    int status;
    uint8_t *pRecMessage , client_message[BUFFSIZE];
    uint8_t transmitBuffer[BUFFSIZE];
    int rec_size;
    struct command_packet pCommandReceived;
    struct response_packet ResponseToSend;
	pRecMessage = 0;
	rec_size = 0;

    // clear the response buffer
	memset(transmitBuffer, 0, BUFFSIZE);
/*
    //Send some messages to the client
    message = "Greetings! I am your connection handler\n";
    write(sock , message , strlen(message));

    message = "Now type something and i shall repeat what you type \n";
    write(sock , message , strlen(message));
*/
    //Receive a message from client
    while( (read_size = recv(sock , client_message , BUFFSIZE , 0)) > 0 )
    {
    	int length;

        //end of string marker
		client_message[read_size] = '\0';
		// send the command to the UART
		if(read_size>5){					// min packet size is 6 bytes
			length = read_size;

			// decode packet
			printf("Read PacketSize: %u, ReadSize: %u\n", read_size, length);
			if(pRecMessage)
				free(pRecMessage);
			pRecMessage = 0;
			rec_size = 0;
			uint8_t *pIPString = 0;
			uint8_t *pOffString = 0;

			printf("Checking Client Message for IP Address\n");
			pIPString = strstr(client_message, "IP=");
			pOffString = strstr(client_message, "OFF");
			if(pIPString != NULL){
				// IP address received
				char callerIP[10]={0};
				printf("IP Address Found\n");
				printf("Client MSG: %s\n", client_message);

				printf("Clear Gstreamer Command\n");
				memset(gstreamCommand, 0, 440);
				pIPString+=3;
				printf("Copying IP to Arg string\n");
				printf("Copy args %s \n", pIPString);
				memcpy(callerIP, pIPString,9);
				sprintf(gstreamCommand, "gst-launch-0.10 -e v4l2src input-src=COMPOSITE  ! capsfilter caps=video/x-raw-yuv,format=\\(fourcc\\)NV12,width=640,height=480 ! ffmpegcolorspace ! TIVidenc1 codecName=h264enc engineName=codecServer ! rtph264pay pt=96 ! udpsink port=5000 host=%s > /dev/null & echo $! > /var/run/gst-launch.pid", callerIP);

				printf("gstreamer command: %s\n", gstreamCommand);
				pIPString = 0;
				ResponseToSend.length = 10;   // initially 8 bytes
				ResponseToSend.command = 99;
				ResponseToSend.pData = 0;
				ResponseToSend.status = 1;   // set to fail
				ResponseToSend.checksum = 0;   // will get this from the receive buffer

				// call cam stream
				printf("Stopping any current stream\n");

			     pidFile = fopen("/var/run/gst-launch.pid", "r");
			     if(pidFile != NULL ){
			          fscanf(pidFile, "%d",&gst_pid);
			          printf("Killing  %i\n", gst_pid);
			          kill(gst_pid, SIGKILL);

			          fclose(pidFile);
			          unlink("/var/run/gst-launch.pid");

			     }


				// launch Gstreamer process
				// create new process

			     status = system(gstreamCommand);

			     pidFile = fopen("/var/run/gst-launch.pid", "r");
			     if(pidFile != NULL ){
			    	  gst_pid=-1;
					  fscanf(pidFile, "%d",&gst_pid);
					  fclose(pidFile);
					  if(gst_pid>=0)
							ResponseToSend.status = 0;  // set to pass
				 }

			}else if(pOffString != NULL){
				printf("Stopping any current stream\n");

				 pidFile = fopen("/var/run/gst-launch.pid", "r");
				 if(pidFile != NULL ){
					  fscanf(pidFile, "%d",&gst_pid);
					  printf("Killing  %i\n", gst_pid);
					  kill(gst_pid, SIGKILL);

					  fclose(pidFile);
					  unlink("/var/run/gst-launch.pid");

				 }

			}else{

				printf("TEXT Command NOT Found\n");


				// get lock for uart
				printf("Get UART Lock\n");
				pthread_mutex_lock(&lock);
				// send and receive to uart
				if(!send_receive_buffer(&client_message, length, pRecMessage, &rec_size)){
					// create a standard error packet
					puts("Error sending and receiving from uart\n");
					ResponseToSend.length = 10;   // initially 8 bytes
					ResponseToSend.command = (uint16_t)((client_message[3]<<8)|client_message[2]);
					ResponseToSend.pData = 0;
					ResponseToSend.status = 1;   // set to fail
					ResponseToSend.checksum = 0;   // will get this from the receive buffer
				}

				// unlock uart
				pthread_mutex_unlock(&lock);

				printf("Release UART Lock\n");
			}

			printf("Send Response Back\n");
			//Send the response back to client

			// set response packet
			rec_size = sizeof(ResponseToSend);
			pRecMessage = malloc(rec_size);
			memcpy(pRecMessage, (uint8_t*)(&ResponseToSend), rec_size);

			write(sock , pRecMessage , rec_size);

			// free data
			if(pRecMessage != 0)
				free(pRecMessage);

			pRecMessage = 0;
			rec_size = 0;
		}


		//clear the message buffer
		printf("Clear Message Buffer\n");
		memset(client_message, 0, BUFFSIZE);
    }

    if(read_size == 0)
    {
        puts("Client disconnected");
        fflush(stdout);
    }
    else if(read_size == -1)
    {
        perror("recv failed");
    }

    fflush(stdout);
    return 0;
}

