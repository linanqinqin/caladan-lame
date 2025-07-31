/*
 * hello_network.c - A simple network example demonstrating POSIX socket APIs on Caladan
 * 
 * This example shows how to write a standard POSIX network application
 * that runs transparently on Caladan using standard socket APIs.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define PORT 8080
#define BUFFER_SIZE 1024

int main(int argc, char *argv[])
{
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];
    int opt = 1;
    
    printf("Hello Network from Caladan!\n");
    printf("This is a standard POSIX socket application running on Caladan\n");
    
    // Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("Socket creation failed");
        return 1;
    }
    
    // Set socket options
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("Setsockopt failed");
        return 1;
    }
    
    // Configure server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    // Bind socket
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        return 1;
    }
    
    // Listen for connections
    if (listen(server_fd, 3) < 0) {
        perror("Listen failed");
        return 1;
    }
    
    printf("Server listening on port %d...\n", PORT);
    printf("(Note: This is a demonstration - no actual client will connect)\n");
    
    // Accept connection (this will block, so we'll just demonstrate the setup)
    printf("Socket APIs working correctly on Caladan!\n");
    printf("Server socket created and configured successfully.\n");
    
    // Clean up
    close(server_fd);
    
    printf("Network example completed successfully!\n");
    return 0;
} 