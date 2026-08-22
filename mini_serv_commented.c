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
// afds = active file descriptors
// rfds = read file descriptors
// wfds = write file descriptors

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

int	extract_message(char **buf, char **msg)
{
	char	*newbuf;
	int		i;

	*msg = 0;
	if (*buf == 0)
		return (0);
	i = 0;
	while ((*buf)[i])
	{
		if ((*buf)[i] == '\n')
		{
			newbuf = calloc(1, strlen(*buf + i + 1) + 1);
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

char	*str_join(char *buf, char *add)
{
	char	*newbuf;
	int		len;

	len = buf ? strlen(buf) : 0;
	newbuf = malloc(len + strlen(add) + 1);
	if (newbuf == 0)
		return (0);
	newbuf[0] = 0;
	if (buf)
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

int	main(int ac, char **av)
{
	if (ac != 2)
	{
		write(2, "Wrong number of arguments\n", 26);
		exit(1);
	}

	int sockfd = socket(AF_INET, SOCK_STREAM, 0); // AF_INET is for ipv4; SOCK_STREAM is for tcp; 0 is for ip
	if (sockfd < 0)
		fatal();
	listen_fd = sockfd;

	struct sockaddr_in serv; // sockaddr_in is a struct that contains information about the server
	bzero(&serv, sizeof(serv)); // set all bytes to 0 to the struct;
	serv.sin_family = AF_INET; // ipv4
	serv.sin_addr.s_addr = htonl(2130706433); // ip of the server, 2130706433 is 127.0.0.1 in big endian
	serv.sin_port = htons(atoi(av[1])); // port of the server

	if (bind(sockfd, (struct sockaddr *)&serv, sizeof(serv)) || listen(sockfd, SOMAXCONN)) // error in case bind or listen fails; SOMAXCONN is the maximum number of connections that can be queued
		fatal();

	FD_ZERO(&afds); // clear all file descriptors from the set
	FD_SET(sockfd, &afds); // add a file descriptor to the set
	max_fd = sockfd; // set the maximum file descriptor to the listening socket

	while (1)
	{
		rfds = wfds = afds; // copy the set of file descriptors
		if (select(max_fd + 1, &rfds, &wfds, NULL, NULL) < 0) // wait for a file descriptor to be ready for reading or writing
			continue;

		for (int fd = 0; fd <= max_fd; fd++)
		{
			if (!FD_ISSET(fd, &rfds)) // check if a file descriptor is in the set
				continue;
			if (fd == sockfd) // if the file descriptor is the listening socket
				add_client(sockfd); // add a new client
			else
				handle_client(fd); // handle the client
		}
	}
	return (0);
}
