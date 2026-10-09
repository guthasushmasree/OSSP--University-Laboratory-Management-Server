#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define SERVER_IP "127.0.0.1"
#define PORT 8080
#define BUFFER_SIZE 2048

void clear_input_buffer(void)
{
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

int main(void)
{
    int sock;
    struct sockaddr_in server_addr;

    int student_id;
    int choice;
    int request_type;
    int resource = 0;
    int duration = 0;

    char message[256];
    char buffer[BUFFER_SIZE];

    printf("\n========================================\n");
    printf("   UNIVERSITY LAB MANAGEMENT CLIENT\n");
    printf("========================================\n");

    printf("\nEnter Student ID: ");
    if (scanf("%d", &student_id) != 1)
    {
        printf("Invalid Student ID.\n");
        return 1;
    }
    clear_input_buffer();

    printf("\n------------- MENU ----------------\n");
    printf("1. Request Computer\n");
    printf("2. Request Printer\n");
    printf("3. Request File Server\n");
    printf("4. Request Projector\n");
    printf("5. Request Scanner\n");
    printf("6. Request Headset\n");
    printf("7. View Resource Dashboard\n");
    printf("8. View All Allocated Resources\n");
    printf("9. View My Resource Status\n");
    printf("-----------------------------------\n");

    printf("Enter your choice: ");
    if (scanf("%d", &choice) != 1)
    {
        printf("Invalid choice.\n");
        return 1;
    }
    clear_input_buffer();

    if (choice >= 1 && choice <= 6)
    {
        request_type = choice;
        resource = choice;

        switch (choice)
        {
            case 1: strcpy(message, "Computer"); break;
            case 2: strcpy(message, "Printer"); break;
            case 3: strcpy(message, "File Server"); break;
            case 4: strcpy(message, "Projector"); break;
            case 5: strcpy(message, "Scanner"); break;
            case 6: strcpy(message, "Headset"); break;
        }

        printf("\nEnter required usage time (in seconds): ");
        if (scanf("%d", &duration) != 1)
        {
            printf("Invalid duration.\n");
            return 1;
        }
        clear_input_buffer();

        if (duration <= 0)
        {
            printf("\nInvalid duration. Please enter a positive number.\n");
            return 1;
        }
    }
    else if (choice == 7 || choice == 8 || choice == 9)
    {
        request_type = choice;
        resource = 0;
        duration = 0;
        strcpy(message, "Status request");
    }
    else
    {
        printf("\nInvalid choice.\n");
        return 1;
    }

    sock = socket(AF_INET, SOCK_STREAM, 0);

    if (sock < 0)
    {
        perror("Socket creation failed");
        return 1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0)
    {
        perror("Invalid server address");
        close(sock);
        return 1;
    }

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Connection to server failed");
        close(sock);
        return 1;
    }

    snprintf(
        buffer,
        sizeof(buffer),
        "%d|%d|%d|%d|%s",
        student_id,
        request_type,
        resource,
        duration,
        message
    );

    if (send(sock, buffer, strlen(buffer), 0) < 0)
    {
        perror("Failed to send request");
        close(sock);
        return 1;
    }

    memset(buffer, 0, sizeof(buffer));

    int bytes_received = recv(
        sock,
        buffer,
        sizeof(buffer) - 1,
        0
    );

    if (bytes_received < 0)
    {
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
