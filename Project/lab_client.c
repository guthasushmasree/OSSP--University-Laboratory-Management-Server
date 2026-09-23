#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define SERVER_IP "127.0.0.1"
#define PORT 8080
#define BUFFER_SIZE 1024

void clear_input_buffer() {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

int main() {
    int sock;
    struct sockaddr_in server_addr;

    int student_id;
    int request_type;
    int resource = 0;
    int duration = 0;
    char message[256];
    char buffer[BUFFER_SIZE];

    printf("\n========================================\n");
    printf("   UNIVERSITY LAB MANAGEMENT CLIENT\n");
    printf("========================================\n");

    /* Student ID */
    printf("\nEnter Student ID: ");
    scanf("%d", &student_id);
    clear_input_buffer();

    /* Request menu */
    printf("\n------------- MENU ----------------\n");
    printf("1. Request Computer\n");
    printf("2. Request Printer\n");
    printf("3. Request File Server\n");
    printf("4. View Resource Dashboard\n");
    printf("5. View All Allocated Resources\n");
    printf("6. View My Resource Status\n");
    printf("-----------------------------------\n");

    printf("Enter your choice: ");
    scanf("%d", &request_type);
    clear_input_buffer();

    /* Resource requests */
    if (request_type >= 1 && request_type <= 3) {

        resource = request_type;

        printf("\nEnter required usage time (in seconds): ");
        scanf("%d", &duration);
        clear_input_buffer();

        if (duration <= 0) {
            printf("Invalid duration. Please enter a positive number.\n");
            return 1;
        }

        switch (resource) {
            case 1:
                strcpy(message, "Computer");
                break;

            case 2:
                strcpy(message, "Printer");
                break;

            case 3:
                strcpy(message, "File Server");
                break;
        }
    }

    /* Dashboard / allocation / student status */
    else if (request_type == 4 ||
             request_type == 5 ||
             request_type == 6) {

        resource = 0;
        duration = 0;
        strcpy(message, "Status request");
    }

    else {
        printf("\nInvalid choice.\n");
        return 1;
    }

    /* Create socket */
    sock = socket(AF_INET, SOCK_STREAM, 0);

    if (sock < 0) {
        perror("Socket creation failed");
        return 1;
    }

    /* Server address */
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("Invalid server address");
        close(sock);
        return 1;
    }

    /* Connect to server */
    if (connect(sock,
                (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0) {

        perror("Connection to server failed");
        close(sock);
        return 1;
    }

    /*
     * Request format:
     * student_id|request_type|resource|duration|message
     */
    snprintf(buffer,
             sizeof(buffer),
             "%d|%d|%d|%d|%s",
             student_id,
             request_type,
             resource,
             duration,
             message);

    /* Send request */
    if (send(sock, buffer, strlen(buffer), 0) < 0) {
        perror("Failed to send request");
        close(sock);
        return 1;
    }

    /* Receive server response */
    memset(buffer, 0, sizeof(buffer));

    int bytes_received = recv(sock,
                              buffer,
                              sizeof(buffer) - 1,
                              0);

    if (bytes_received < 0) {
        perror("Failed to receive response");
        close(sock);
        return 1;
    }

    buffer[bytes_received] = '\0';

    printf("\n========================================\n");
    printf("           SERVER RESPONSE\n");
    printf("========================================\n");
    printf("%s\n", buffer);
    printf("========================================\n");

    close(sock);

    return 0;
}
