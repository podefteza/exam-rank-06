#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>

int		max_fd = 0;
int		next_id = 0;
int		listen_fd = -1;
int		ids[70000]; // was 65536 (256^2)
char	*bufs[70000]; // was 65536 (256^2)
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

	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0)
		fatal();
	listen_fd = sockfd;

	struct sockaddr_in serv;
	bzero(&serv, sizeof(serv));
	serv.sin_family = AF_INET;
	serv.sin_addr.s_addr = htonl(2130706433); // 127.0.0.1, 127 * 256^3 (+ 0 * 256^2 + 0 * 256^1) + 1
	serv.sin_port = htons(atoi(av[1]));

	if (bind(sockfd, (struct sockaddr *)&serv, sizeof(serv)) || listen(sockfd, SOMAXCONN))
		fatal();

	FD_ZERO(&afds);
	FD_SET(sockfd, &afds);
	max_fd = sockfd;

	while (1)
	{
		rfds = wfds = afds;
		if (select(max_fd + 1, &rfds, &wfds, NULL, NULL) < 0)
			continue;

		for (int fd = 0; fd <= max_fd; fd++)
		{
			if (!FD_ISSET(fd, &rfds))
				continue;
			if (fd == sockfd)
				add_client(sockfd);
			else
				handle_client(fd);
		}
	}
	return (0);
}
