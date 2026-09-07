#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>

int		max_fd = 0; // highest active socket fd
int		next_client = 0; // next client id counter
int		listen_fd = -1; // server listening socket fd
int		client_ids[65536]; // (256^2, can be any large number...) // maps socket fd -> client id
char	*buffer[65536]; // (256^2) // maps socket fd -> client message buffer
fd_set	rfds, wfds, afds; // read fds, write fds, and active master fds set

// GIVEN WITH THE SUBJECT
int extract_message(char **buf, char **msg)
{
	char	*newbuf;
	int	i;

	*msg = 0;
	if (*buf == 0)
		return (0);
	i = 0;
	while ((*buf)[i])
	{
		if ((*buf)[i] == '\n')
		{
			newbuf = calloc(1, sizeof(*newbuf) * (strlen(*buf + i + 1) + 1));
			if (newbuf == 0)
				return (-1);
			strcpy(newbuf, *buf + i + 1);
			*msg = *buf;
			(*msg)[i + 1] = 0;
			*buf = newbuf;
			return (1);
		}
		i++;
	}
	return (0);
}

// GIVEN WITH THE SUBJECT
char *str_join(char *buf, char *add)
{
	char	*newbuf;
	int		len;

	if (buf == 0)
		len = 0;
	else
		len = strlen(buf);
	newbuf = malloc(sizeof(*newbuf) * (len + strlen(add) + 1));
	if (newbuf == 0)
		return (0);
	newbuf[0] = 0;
	if (buf != 0)
		strcat(newbuf, buf);
	free(buf);
	strcat(newbuf, add);
	return (newbuf);
}

void	fatal(void) {
	write(2, "Fatal error\n", 12);
	exit(1);
}

// sends message to all connected clients except the sender, the listening socket and ensures we can write on the recipients
void	broadcast(int sender, char *msg) {
	for (int fd = 0; fd <= max_fd; fd++)
		if (fd != sender && fd != listen_fd && FD_ISSET(fd, &wfds))
			send(fd, msg, strlen(msg), 0);
}

void	add_client(int sockfd) {
	int	new_fd;
	char	msg[64];

	new_fd = accept(sockfd, NULL, NULL); // accepts incoming connections
	if (new_fd < 0)
		return ;
	if (new_fd > max_fd) // update the max_fd for new clients
		max_fd = new_fd;
	client_ids[new_fd] = next_client++; // assigns ID to the new client
	buffer[new_fd] = NULL; // initialize client's message
	FD_SET(new_fd, &afds); // adds client to the active fd's set so select() monitors it
	sprintf(msg, "server: client %d just arrived\n", client_ids[new_fd]); // formats "msg" to transmit
	broadcast(new_fd, msg); // send notification to all connected clients
}

void	remove_client(int fd) {
	char	msg[64];

	sprintf(msg, "server: client %d just left\n", client_ids[fd]); // format message
	broadcast(fd, msg); // broadcast to all remaining clients
	FD_CLR(fd, &afds); // remove fd from the active set so select() stops monitoring it
	free(buffer[fd]);
	buffer[fd] = NULL;
	close(fd); // frees and closes to avoid leaks
}

void	handle_client(int fd) {
	char	tmp[4096];
	char	*msg;
	int	bytes_read;
	int	status;

	bytes_read = recv(fd, tmp, sizeof(tmp) - 1, 0); // read data from client
	// 0 means client disconnected, < 0 means read error -> remove client
	if (bytes_read <= 0) {
		remove_client(fd);
		return ;
	}
	tmp[bytes_read] = '\0'; // null-terminate the received chunk to make it a valid string

	buffer[fd] = str_join(buffer[fd], tmp); // append received chunk to the message buffer
	if (!buffer[fd])
		fatal();

	// extract complete lines (ending with '\n') one by one from the client's buffer
	while ((status = extract_message(&buffer[fd], &msg)) == 1) {
		char out[65536];
		sprintf(out, "client %d: %s", client_ids[fd], msg); // format the line with the required prefix "client %d: "
		broadcast(fd, out); // broadcast the line to all other connected clients
		free(msg);
	}
	if (status == -1) // extract_message returns -1 if calloc fails
		fatal();
}

// GIVEN WITH THE SUBJECT BUT WE NEED TO MODIFY IT
int main(int argc, char **argv) { // MODIFIED: added argc & argv parameters
	if (argc != 2) { // NEW: check argument count
		write(2, "Wrong number of arguments\n", 26);
		exit(1);
	}
	int sockfd; // KEPT: socket descriptor; REMOVED: connfd, len
	struct sockaddr_in servaddr; // KEPT: server address structure; REMOVED: cli

	// socket create and verification
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd == -1) {
		fatal(); // MODIFIED: exit with status 1 and "Fatal error\n" on failure instead of using printf and exit(0)
	}
	listen_fd = sockfd; // NEW: save listening socket in global variable for broadcast filtering

	bzero(&servaddr, sizeof(servaddr));

	// assign IP, PORT
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(2130706433); //127.0.0.1
	servaddr.sin_port = htons(atoi(argv[1])); // MODIFIED: parse port from command line argument argv[1]

	// Binding newly created socket to given IP and verification
	if ((bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr))) != 0) {
		fatal();  // MODIFIED: exit with status 1 and "Fatal error\n" on failure instead of using printf and exit(0)
	}
	if (listen(sockfd, 128) != 0) { // MODIFIED: changed backlog from 10 to 128 (SOMAXCONN)
		fatal(); // MODIFIED: exit with status 1 and "Fatal error\n" on failure instead of using printf and exit(0)
	}

	FD_ZERO(&afds); // NEW: clear master file descriptor set
	FD_SET(sockfd, &afds); // NEW: add listening socket to master fd set
	max_fd = sockfd; // NEW: set initial highest fd to listening socket

	while (1) { // NEW: multi-client non-blocking event loop replacing single accept
		rfds = wfds = afds; // NEW: copy master fd set to read and write sets for select()
		if (select(max_fd + 1, &rfds, &wfds, NULL, NULL) < 0) // NEW: block until an fd is ready
			continue; // NEW: restart loop if select was interrupted

		for (int fd = 0; fd <= max_fd; fd++) { // NEW: check all file descriptors up to max_fd
			if (!FD_ISSET(fd, &rfds)) // NEW: skip fds not ready for reading
				continue; // NEW: skip to next fd
			if (fd == sockfd) // NEW: if listening socket is ready for reading
				add_client(sockfd); // NEW: accept new incoming client connection
			else // NEW: if a connected client socket has sent data
				handle_client(fd); // NEW: receive data, buffer messages, broadcast lines or handle disconnect
		}
	}
	return (0); // NEW: return 0 from main
}