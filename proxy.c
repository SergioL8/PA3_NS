#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>

#define BUFF_SIZE 8192

void open_server_socket(int *socket_fd, int port, struct sockaddr_in *socket_address);
void open_client_socket(int *socket_fd, struct sockaddr_in *socket_address, char *host_ip, int port);
int parse_url(char *buffer, char* hostname, char **host_ip, int *port, char *path, char **body);
int blocklist(char *hostname, char *host_ip);

int main(int argc, char* argv[]) {

    /* Variable declaration */
    int server_sockfd; /* socket */
    int client_sockfd; /* client socket */
    int http_sockfd;
    struct sockaddr_in serveraddr; /* server's addr */
    struct sockaddr_in clientaddr; /* client addr */
    struct sockaddr_in httpaddr;
    socklen_t client_addrlen = sizeof(clientaddr); /* byte size of server's address */
    char c_p_buffer[BUFF_SIZE]; /* client to proxy buffer for incoming get requests */
    char s_p_buffer[BUFF_SIZE]; /* server to proxy buffer to receive the webapge  */
    char path[1024];
    char hostname[256];
    int port = 80;
    int bytes;
    char *body = NULL;
    char *host_ip = NULL;
    char request[2048];
    

    /* Check correct number of command line arguments */
    if (argc < 2) {
        perror("Please include one command line argument with the PORT #");
        exit(EXIT_FAILURE);
    }

    /* Open Socket */
    open_server_socket(&server_sockfd, atoi(argv[1]), &serveraddr);

    /* main loop to accept incoming connections */
    while(1) {
        if ((client_sockfd = accept(server_sockfd, (struct sockaddr*)&clientaddr, &client_addrlen)) < 0) {
            perror("ERROR in accept");
            continue;
        }

        /* read from socket */
        int n = read(client_sockfd, c_p_buffer, BUFF_SIZE - 1); 
        if (n == 0) {
            perror("client connected and closed without sending anything");
            close(client_sockfd);
            continue;
        }
        c_p_buffer[n] = '\0';
        

        /* parse url */
        int parse_result = parse_url(c_p_buffer, hostname, &host_ip, &port, path, &body);
        if (parse_result == -1) {
            const char *bad_request =
                "HTTP/1.1 400 Bad Request\r\n"
                "Content-Length: 0\r\n"
                "Connection: close\r\n"
                "\r\n";
            send(client_sockfd, bad_request, strlen(bad_request), 0);
            close(client_sockfd);
            continue;
        }

        /* check if request is in blocklist */
        int in_blocklist = blocklist(hostname, host_ip);
        if (in_blocklist == 1) {
            const char *forbidden =
                "HTTP/1.1 403 Forbidden\r\n"
                "Content-Length: 0\r\n"
                "Connection: close\r\n"
                "\r\n";
            send(client_sockfd, forbidden, strlen(forbidden), 0);
            close(client_sockfd);
            continue;
        } else if (in_blocklist == -1) {
            const char *server_error =
                "HTTP/1.1 500 Internal Server Error\r\n"
                "Content-Length: 0\r\n"
                "Connection: close\r\n"
                "\r\n";
            send(client_sockfd, server_error, strlen(server_error), 0);
            close(client_sockfd);
            continue;
        }

        /* open socket to http server */
        open_client_socket(&http_sockfd, &httpaddr, host_ip, port);

        /* build the request for the http server */
        snprintf(request, sizeof(request),
            "GET %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Connection: close\r\n"
            "\r\n",
            path, hostname);
        
        /* send the request to the http server */
        send(http_sockfd, request, strlen(request), 0);

        /* get response from the server */
        while ((bytes = recv(http_sockfd, s_p_buffer, sizeof(s_p_buffer), 0)) > 0) {
            send(client_sockfd, s_p_buffer, bytes, 0);
        }
    }
}



void open_server_socket(int *socket_fd, int port, struct sockaddr_in *socket_address) {
    /* Open Socket */
    if ((*socket_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Error opening socket");
        exit(EXIT_FAILURE);
    }

    /* Allow socket to be resued inside TIME_WAIT */
    int optval = 1;
    setsockopt(*socket_fd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval , sizeof(int));

    /* Build the server's internet address */
    bzero((char *) socket_address, sizeof(*socket_address));
    socket_address->sin_family = AF_INET;
    socket_address->sin_addr.s_addr = htonl(INADDR_ANY);
    socket_address->sin_port = htons((unsigned short)port);

    /* bind: associate the parent socket with a port */
    if (bind(*socket_fd, (struct sockaddr *) socket_address, sizeof(*socket_address)) < 0) {
        perror("ERROR on binding");
        exit(EXIT_FAILURE);
    }

    /* listen */
    if (listen(*socket_fd, 16) < 0) {
        perror("ERROR in listen");
        exit(EXIT_FAILURE);
    }
}



void open_client_socket(int *socket_fd, struct sockaddr_in *socket_address, char *host_ip, int port) {

    if ((*socket_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Error opening socket");
        exit(EXIT_FAILURE);
    }

    socket_address->sin_family = AF_INET;
    socket_address->sin_port = htons(port);
    socket_address->sin_addr = *(struct in_addr *)host_ip;

    if ((connect(*socket_fd, (struct sockaddr*)socket_address, sizeof(*socket_address))) < 0) {
        perror("Error in connect");
        exit(EXIT_FAILURE);
    }
}



int parse_url(char *buffer, char* hostname, char **host_ip, int *port, char *path, char **body) {

    /* Variable declartion */
    char request_method[4];
    char request_url[150];
    const char *p;
    const char *end;
    size_t hostname_len;
    struct hostent *hostExists;

    /* Parse the method and URL for validation */
    sscanf(buffer, "%s %s", request_method, request_url);
    // printf("Method: %s \nURL: %s \n", request_method, request_url);

    /* Validate method */
    if (strcmp(request_method, "GET") != 0) {  return -1;  }

    /* Skip the http part */
    if (strncmp(request_url, "http://", 7) != 0) {  return -1;  }
    p = request_url + 7; 

    /* find end of host (either ':' or '/' or end of string) */
    end = p;
    while (*end && *end != ':' && *end != '/') {
        end++;
    }

    /* Get hostname length */
    hostname_len = end - p;
    if (hostname_len == 0) {  return -1;  }

    /* Copy the hostname part to its variable */
    strncpy(hostname, p, hostname_len);
    hostname[hostname_len] = '\0';

    /* Check host exists */
    if ((hostExists = gethostbyname(hostname)) == NULL) {  return -1;  }

    /* assign ip */
    *host_ip = hostExists->h_addr_list[0];
    if (*host_ip == NULL) {  return -1;  }

    /* -------- get port --------- */
    if (*end == ':') {
        const char *port_start = end + 1;
        const char *port_end = port_start;

        /* find end of port (until '/' or end) */
        while (*port_end && *port_end != '/') {
            port_end++;
        }

        int port_len = port_end - port_start;
        if (port_len == 0) {  return -1;  }

        char port_str[10];
        strncpy(port_str, port_start, port_len);
        port_str[port_len] = '\0';

        *port = atoi(port_str);

        end = port_end;  // move end pointer forward
    }

    /* -------- get path -------- */
    if (*end == '/') {
        strcpy(path, end);
    } else {
        strcpy(path, "/"); /* no path → default "/" */
    }

    /* ------- get the body ------- */
    *body = strstr(buffer, "\r\n\r\n");
    if (*body != NULL) {
        *body += 4;  // move past the "\r\n\r\n"
    }
    
    return 0;
}



int blocklist(char *hostname, char *host_ip) {

    /* Varibable declaration */
    char line[256];
    struct in_addr addr;
    char ip_str[INET_ADDRSTRLEN];

    /* convert ip to string */
    memcpy(&addr, host_ip, sizeof(addr)); // Convert to in_addr for inet_ntop
    inet_ntop(AF_INET, &addr, ip_str, INET_ADDRSTRLEN);

    /* open file */
    FILE *fp = fopen("./blocklist", "r");
    if (fp == NULL) { return -1; }

    /* iterate through the file comparing */
    while (fgets(line, sizeof(line), fp) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strcmp(line, hostname) == 0 || strcmp(line, ip_str) == 0) {
            return 1;
        }
    }
    fclose(fp);
    return 0;
}