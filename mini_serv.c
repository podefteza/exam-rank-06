#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>

int		max_fd = 0;
int		next_id = 0;
int		listen_fd = -1;
int		ids[65536]; // 256^2
char	*bufs[65536]; // 256^2
fd_set	rfds, wfds, afds;

void	fatal(void)
{
	write(2, "Fatal error\n", 12);
	exit(1);
}

void	broadcast(int except, char *msg)
{
	for (int fd = 0; fd <= max_fd; fd++)
		if (fd != except && fd != listen_fd && FD_ISSET(fd, &wfds))
			send(fd, msg, strlen(msg), 0);
}

void	add_client(int sockfd)
{
	int	newfd;
	char	msg[64];

	newfd = accept(sockfd, NULL, NULL);
	if (newfd < 0)
		return ;
	if (newfd > max_fd)
		max_fd = newfd;
	ids[newfd] = next_id++;
	bufs[newfd] = NULL;
	FD_SET(newfd, &afds);
	sprintf(msg, "server: client %d just arrived\n", ids[newfd]);
	broadcast(newfd, msg);
}

void	remove_client(int fd)
{
	char	msg[64];

	sprintf(msg, "server: client %d just left\n", ids[fd]);
	broadcast(fd, msg);
	FD_CLR(fd, &afds);
	free(bufs[fd]);
	bufs[fd] = NULL;
	close(fd);
}

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

void	handle_client(int fd)
{
	char	tmp[4096];
	char	*msg;
	int	ret;
	int	r;

	ret = recv(fd, tmp, sizeof(tmp) - 1, 0);
	if (ret <= 0)
	{
		remove_client(fd);
		return ;
	}
	tmp[ret] = '\0';
	bufs[fd] = str_join(bufs[fd], tmp);
	if (!bufs[fd])
		fatal();
	while ((r = extract_message(&bufs[fd], &msg)) == 1)
	{
		char out[65536];

		sprintf(out, "client %d: %s", ids[fd], msg);
		broadcast(fd, out);
		free(msg);
	}
	if (r == -1)
		fatal();
}

// GIVEN WITH THE SUBJECT BUT WE NEED TO MODIFY IT
int main(int argc, char **argv) { // MODIFIED: added argc & argv parameters
	if (argc != 2) // NEW: check argument count
	{
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
	servaddr.sin_addr.s_addr = htonl(2130706433);
	servaddr.sin_port = htons(atoi(argv[1])); // MODIFIED: parse port dynamically from command line argument argv[1]

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

	while (1) // NEW: multi-client non-blocking event loop replacing single accept
	{
		rfds = wfds = afds; // NEW: copy master fd set to read and write sets for select()
		if (select(max_fd + 1, &rfds, &wfds, NULL, NULL) < 0) // NEW: block until an fd is ready
			continue; // NEW: restart loop if select was interrupted

		for (int fd = 0; fd <= max_fd; fd++) // NEW: check all file descriptors up to max_fd
		{
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