#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <arpa/inet.h>
#include <time.h>
#include <errno.h>

#define PORT 8080
#define MAX_CLIENTS 100
#define QUEUE_SIZE 100
#define WORKER_COUNT 3
#define BUFFER_SIZE 2048

#define COMPUTER   1
#define PRINTER    2
#define FILESERVER 3

/* ================= REQUEST ================= */

typedef struct
{
    int client_socket;
    int student_id;
    int request_type;
    int resource;
    int duration;
    char message[256];
} Request;

/* ================= ALLOCATION ================= */

typedef struct
{
    int student_id;
    int resource;
    int duration;

    time_t start_time;
    time_t expected_end_time;

    int allocated;
} Allocation;

/* ================= GLOBAL VARIABLES ================= */

Request request_queue[QUEUE_SIZE];

int queue_front = 0;
int queue_rear = 0;
int queue_count = 0;

pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queue_not_empty = PTHREAD_COND_INITIALIZER;
pthread_cond_t queue_not_full = PTHREAD_COND_INITIALIZER;

pthread_mutex_t resource_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Resource totals */

int resource_total[4] =
{
    0,
    5,  /* Computers */
    2,  /* Printers */
    2   /* File Servers */
};

/* Currently available resources */

int resource_available[4] =
{
    0,
    5,
    2,
    2
};

/* Semaphores */

sem_t computer_sem;
sem_t printer_sem;
sem_t fileserver_sem;

/* Allocation table */
Allocation allocations[MAX_CLIENTS];

int allocation_count = 0;

volatile sig_atomic_t server_running = 1;

int server_socket = -1;

/* =========================================================
   INDIAN TIME FUNCTION
   ========================================================= */

void get_indian_time(time_t timestamp, char *output, size_t size)
{
    struct tm *time_info;

    time_info = localtime(&timestamp);

    if (time_info != NULL)
    {
        strftime(output,
                 size,
                 "%H:%M %d-%m-%Y",
                 time_info);
    }
    else
    {
        strcpy(output, "Unknown");
    }
}

/* =========================================================
   LOGGING
   ========================================================= */
void write_log(const char *message)
{
    FILE *file = fopen("server.log", "a");

    if (file == NULL)
        return;

    time_t now = time(NULL);

    char time_string[64];

    get_indian_time(now,
                    time_string,
                    sizeof(time_string));

    fprintf(file,
            "[%s IST] %s\n",
            time_string,
            message);

    fclose(file);
}

/* =========================================================
   RESOURCE NAME
   ========================================================= */

const char *get_resource_name(int resource)
{
    switch (resource)
    {
        case COMPUTER:
            return "Computer";
                  case PRINTER:
            return "Printer";

        case FILESERVER:
            return "File Server";

        default:
            return "Unknown";
    }
}

/* =========================================================
   GET RESOURCE SEMAPHORE
   ========================================================= */

sem_t *get_resource_semaphore(int resource)
{
    switch (resource)
    {
        case COMPUTER:
            return &computer_sem;

        case PRINTER:
            return &printer_sem;

        case FILESERVER:
            return &fileserver_sem;

        default:
            return NULL;
    }
}

/* =========================================================
   RESOURCE DASHBOARD
   ========================================================= */
void show_resource_status(int client_socket)
{
    char response[BUFFER_SIZE];

    pthread_mutex_lock(&resource_mutex);

    snprintf(response,
             sizeof(response),

             "\n"
             "========================================\n"
             "        LAB RESOURCE DASHBOARD\n"
             "========================================\n"
             "Computers    : %d/%d available\n"
             "Printers     : %d/%d available\n"
             "File Servers : %d/%d available\n"
             "========================================\n",

             resource_available[COMPUTER],
             resource_total[COMPUTER],

             resource_available[PRINTER],
             resource_total[PRINTER],

             resource_available[FILESERVER],
             resource_total[FILESERVER]);

    pthread_mutex_unlock(&resource_mutex);

    send(client_socket,
         response,
         strlen(response),
         0);

    write_log("Resource dashboard viewed.");
}
/* =========================================================
   VIEW ALL ALLOCATIONS
   ========================================================= */

void show_all_allocations(int client_socket)
{
    char response[BUFFER_SIZE * 3];
    char temp[700];

    response[0] = '\0';

    pthread_mutex_lock(&resource_mutex);

    strcat(response,
           "\n"
           "========================================\n"
           "       CURRENT RESOURCE ALLOCATIONS\n"
           "========================================\n");

    int found = 0;

    for (int i = 0; i < allocation_count; i++)
    {
        if (allocations[i].allocated)
        {
            found = 1;

            char start_time[64];
            char end_time[64];

            get_indian_time(
                allocations[i].start_time,
                start_time,
                sizeof(start_time));
        time_t now = time(NULL);

            int used_time =
                (int)difftime(
                    now,
                    allocations[i].start_time);

            if (used_time < 0)
                used_time = 0;

            snprintf(temp,
                     sizeof(temp),

                     "\nStudent ID     : %d\n"
                     "Resource       : %s\n"
                     "Requested Time : %d seconds\n"
                     "Used Time      : %d seconds\n"
                     "Start Time     : %s IST\n"
                     "Expected End   : %s IST\n"
                     "----------------------------------------\n",

                     allocations[i].student_id,

                     get_resource_name(
                         allocations[i].resource),

                     allocations[i].duration,

                     used_time,

                     start_time,

                     end_time);

            strcat(response, temp);
        }
  }

    if (!found)
    {
        strcat(response,
               "No resources are currently allocated.\n");
    }

    strcat(response,
           "========================================\n");

    pthread_mutex_unlock(&resource_mutex);

    send(client_socket,
         response,
         strlen(response),
         0);

    write_log("All allocated resources viewed.");
}

/* =========================================================
   STUDENT SPECIFIC STATUS
   ========================================================= */

void show_student_status(int client_socket,
                         int student_id)
{
    char response[BUFFER_SIZE * 2];
    char temp[700];

    response[0] = '\0';

    pthread_mutex_lock(&resource_mutex);

    snprintf(response,
             sizeof(response),
      
             "\n"
             "========================================\n"
             "       STUDENT RESOURCE STATUS\n"
             "========================================\n"
             "Student ID : %d\n",

             student_id);

    int found = 0;

    for (int i = 0; i < allocation_count; i++)
    {
        if (allocations[i].allocated &&
            allocations[i].student_id == student_id)
        {
            found = 1;

            time_t now = time(NULL);

            int elapsed =
                (int)difftime(
                    now,
                    allocations[i].start_time);

            int remaining =
                allocations[i].duration - elapsed;

            if (elapsed < 0)
                elapsed = 0;

            if (remaining < 0)
                remaining = 0;

            char start_time[64];
            char end_time[64];
         get_indian_time(
                allocations[i].start_time,
                start_time,
                sizeof(start_time));

            get_indian_time(
                allocations[i].expected_end_time,
                end_time,
                sizeof(end_time));

            snprintf(temp,
                     sizeof(temp),

                     "\nResource       : %s\n"
                     "Requested Time : %d seconds\n"
                     "Used Time      : %d seconds\n"
                     "Remaining Time : %d seconds\n"
                     "Start Time     : %s IST\n"
                     "Expected End   : %s IST\n",

                     get_resource_name(
                         allocations[i].resource),

                     allocations[i].duration,

                     elapsed,

                     remaining,

                     start_time,

                     end_time);

            strcat(response, temp);
        }
    }
   if (!found)
    {
        strcat(response,
               "\nNo resource is currently allocated "
               "to this student.\n");
    }

    strcat(response,
           "\n========================================\n");

    pthread_mutex_unlock(&resource_mutex);

    send(client_socket,
         response,
         strlen(response),
         0);

    write_log("Student-specific status viewed.");
}

/* =========================================================
   DUPLICATE ALLOCATION CHECK
   ========================================================= */

int has_duplicate_allocation(int student_id,
                             int resource)
{
    for (int i = 0; i < allocation_count; i++)
    {
        if (allocations[i].allocated &&
            allocations[i].student_id == student_id &&
            allocations[i].resource == resource)
        {
            return 1;
        }
    }
  return 0;
}

/* =========================================================
   ADD ALLOCATION
   ========================================================= */

void add_allocation(int student_id,
                    int resource,
                    int duration)
{
    pthread_mutex_lock(&resource_mutex);

    if (allocation_count < MAX_CLIENTS)
    {
        allocations[allocation_count].student_id =
            student_id;

        allocations[allocation_count].resource =
            resource;

        allocations[allocation_count].duration =
            duration;

        allocations[allocation_count].start_time =
            time(NULL);

        allocations[allocation_count].expected_end_time =
            allocations[allocation_count].start_time
            + duration;

        allocations[allocation_count].allocated = 1;

        allocation_count++;
    }
 pthread_mutex_unlock(&resource_mutex);
}

/* =========================================================
   RELEASE RESOURCE
   ========================================================= */

void release_allocation(int index)
{
    int resource =
        allocations[index].resource;

    int student_id =
        allocations[index].student_id;

    int duration =
        allocations[index].duration;

    resource_available[resource]++;

    allocations[index].allocated = 0;

    char log_message[300];

    snprintf(log_message,
             sizeof(log_message),

             "Student %d released %s after %d seconds.",

             student_id,

             get_resource_name(resource),

             duration);

    write_log(log_message);
   printf("[RESOURCE] Student %d released %s.\n",
           student_id,
           get_resource_name(resource));
}

/* =========================================================
   CHECK EXPIRED ALLOCATIONS
   ========================================================= */

void check_expired_allocations()
{
    time_t now = time(NULL);

    pthread_mutex_lock(&resource_mutex);

    for (int i = 0;
         i < allocation_count;
         i++)
    {
        if (allocations[i].allocated &&
            now >= allocations[i].expected_end_time)
        {
            int resource =
                allocations[i].resource;

            int student_id =
                allocations[i].student_id;

            printf("[RESOURCE] Automatically releasing %s "
                   "from Student %d.\n",

                   get_resource_name(resource),
                   student_id);

            release_allocation(i);
         sem_t *sem =
                get_resource_semaphore(resource);

            if (sem != NULL)
            {
                sem_post(sem);
            }
        }
    }

    pthread_mutex_unlock(&resource_mutex);
}

/* =========================================================
   TIMER THREAD
   ========================================================= */

void *timer_thread(void *arg)
{
    (void)arg;

    while (server_running)
    {
        sleep(1);

        check_expired_allocations();
    }

    return NULL;
}

/* =========================================================
   ADD REQUEST TO QUEUE
   ========================================================= */

void add_request(Request request)
{
    pthread_mutex_lock(&queue_mutex);

    while (queue_count == QUEUE_SIZE &&
           server_running)
    {
        pthread_cond_wait(
            &queue_not_full,
            &queue_mutex);
    }

    if (!server_running)
    {
        pthread_mutex_unlock(&queue_mutex);
        return;
    }

    request_queue[queue_rear] =
        request;

    queue_rear =
        (queue_rear + 1) % QUEUE_SIZE;

    queue_count++;

    pthread_cond_signal(
        &queue_not_empty);

    pthread_mutex_unlock(&queue_mutex);
}

/* =========================================================
   GET REQUEST FROM QUEUE
   ========================================================= */

Request get_request()
{
   Request request;

    memset(&request,
           0,
           sizeof(Request));

    pthread_mutex_lock(&queue_mutex);

    while (queue_count == 0 &&
           server_running)
    {
        pthread_cond_wait(
            &queue_not_empty,
            &queue_mutex);
    }

    if (!server_running &&
        queue_count == 0)
    {
        pthread_mutex_unlock(&queue_mutex);

        return request;
    }

    request =
        request_queue[queue_front];

    queue_front =
        (queue_front + 1) % QUEUE_SIZE;

    queue_count--;

    pthread_cond_signal(
        &queue_not_full);

    pthread_mutex_unlock(&queue_mutex);
    return request;
}

/* =========================================================
   PROCESS RESOURCE REQUEST
   ========================================================= */

void process_resource_request(Request request)
{
    int resource =
        request.resource;

    if (resource < COMPUTER ||
        resource > FILESERVER)
    {
        const char *message =
            "ERROR: Invalid resource.\n";

        send(request.client_socket,
             message,
             strlen(message),
             0);

        return;
    }

    if (request.duration <= 0)
    {
        const char *message =
            "ERROR: Duration must be greater than 0 seconds.\n";

        send(request.client_socket,
             message,
             strlen(message),
             0);
     return;
    }

    /* Duplicate allocation check */

    pthread_mutex_lock(&resource_mutex);

    if (has_duplicate_allocation(
            request.student_id,
            resource))
    {
        pthread_mutex_unlock(&resource_mutex);

        char response[500];

        snprintf(response,
                 sizeof(response),

                 "ERROR: Student %d already has a %s allocated.\n",

                 request.student_id,

                 get_resource_name(resource));

        send(request.client_socket,
             response,
             strlen(response),
             0);

        write_log(response);

        return;
    }

    pthread_mutex_unlock(&resource_mutex);

    sem_t *sem =
get_resource_semaphore(resource);

    if (sem == NULL)
    {
        const char *message =
            "ERROR: Semaphore unavailable.\n";

        send(request.client_socket,
             message,
             strlen(message),
             0);

        return;
    }

    char log_message[300];

    snprintf(log_message,
             sizeof(log_message),

             "Student %d waiting for %s.",

             request.student_id,

             get_resource_name(resource));

    write_log(log_message);

    printf("[RESOURCE] Student %d waiting for %s.\n",
           request.student_id,
           get_resource_name(resource));

    /* Semaphore controls limited resources */

    sem_wait(sem);

    pthread_mutex_lock(&resource_mutex);
   resource_available[resource]--;

    pthread_mutex_unlock(&resource_mutex);

    add_allocation(
        request.student_id,
        resource,
        request.duration);

    time_t start_time = time(NULL);

    char start_string[64];
    char end_string[64];

    get_indian_time(
        start_time,
        start_string,
        sizeof(start_string));

    get_indian_time(
        start_time + request.duration,
        end_string,
        sizeof(end_string));

    snprintf(log_message,
             sizeof(log_message),

             "Student %d acquired %s for %d seconds. "
             "Start: %s IST, Expected End: %s IST.",

             request.student_id,

             get_resource_name(resource),

             request.duration,
           start_string,

             end_string);

    write_log(log_message);

    printf("\n[RESOURCE] %s acquired.\n",
           get_resource_name(resource));

    printf("[RESOURCE] Student ID : %d\n",
           request.student_id);

    printf("[RESOURCE] Duration    : %d seconds\n",
           request.duration);

    printf("[RESOURCE] Start Time  : %s IST\n",
           start_string);

    printf("[RESOURCE] Expected End: %s IST\n\n",
           end_string);

    char response[BUFFER_SIZE];

    snprintf(response,
             sizeof(response),

             "\n"
             "========================================\n"
             "          RESOURCE ALLOCATED\n"
             "========================================\n"
             "Student ID   : %d\n"
             "Resource     : %s\n"
             "Duration     : %d seconds\n"
             "Start Time   : %s IST\n"
             "Expected End : %s IST\n"
             "----------------------------------------\n"
             "The resource will be automatically\n"
             "released after %d seconds.\n"
             "========================================\n",

             request.student_id,

             get_resource_name(resource),

             request.duration,

             start_string,

             end_string,

             request.duration);

    send(request.client_socket,
         response,
         strlen(response),
         0);
}

/* =========================================================
   WORKER THREAD
   ========================================================= */

void *worker_thread(void *arg)
{
    int worker_id =
        *((int *)arg);

    free(arg);

    printf("[WORKER %d] Started.\n",
           worker_id);
 char log_message[300];

    snprintf(log_message,
             sizeof(log_message),
             "Worker %d started.",
             worker_id);

    write_log(log_message);

    while (server_running)
    {
        Request request =
            get_request();

        if (!server_running &&
            request.client_socket == 0)
        {
            break;
        }

        if (request.client_socket == 0)
            continue;

        printf("[WORKER %d] Processing Student %d request.\n",
               worker_id,
               request.student_id);

        snprintf(log_message,
                 sizeof(log_message),

                 "Worker %d processing Student %d request.",

                 worker_id,

                 request.student_id);
  write_log(log_message);

        if (request.request_type >= 1 &&
            request.request_type <= 3)
        {
            process_resource_request(request);
        }
        else if (request.request_type == 4)
        {
            show_resource_status(
                request.client_socket);
        }
        else if (request.request_type == 5)
        {
            show_all_allocations(
                request.client_socket);
        }
        else if (request.request_type == 6)
        {
            show_student_status(
                request.client_socket,
                request.student_id);
        }
        else
        {
            const char *message =
                "ERROR: Invalid request type.\n";

            send(request.client_socket,
                 message,
                 strlen(message),
                 0);
        }

        close(request.client_socket);
    }
 printf("[WORKER %d] Stopped.\n",
           worker_id);

    snprintf(log_message,
             sizeof(log_message),
             "Worker %d stopped.",
             worker_id);

    write_log(log_message);

    return NULL;
}

/* =========================================================
   SIGNAL HANDLER
   ========================================================= */

void handle_signal(int signal_number)
{
    if (signal_number == SIGUSR1)
    {
        pthread_mutex_lock(&queue_mutex);

        int pending =
            queue_count;

        pthread_mutex_unlock(&queue_mutex);

        printf("[SIGNAL] Pending requests: %d\n",
               pending);

        char message[200];

        snprintf(message,
                 sizeof(message),
             "SIGUSR1: Pending requests = %d.",

                 pending);

        write_log(message);
    }

    else if (signal_number == SIGUSR2)
    {
        printf("[SIGNAL] Resource status requested.\n");

        pthread_mutex_lock(&resource_mutex);

        printf("Computers    : %d/%d available\n",
               resource_available[COMPUTER],
               resource_total[COMPUTER]);

        printf("Printers     : %d/%d available\n",
               resource_available[PRINTER],
               resource_total[PRINTER]);

        printf("File Servers : %d/%d available\n",
               resource_available[FILESERVER],
               resource_total[FILESERVER]);

        pthread_mutex_unlock(&resource_mutex);

        write_log(
            "SIGUSR2: Resource status requested.");
    }

    else if (signal_number == SIGINT ||
             signal_number == SIGTERM)
    {
        printf("[SIGNAL] Shutdown signal received.\n");
    write_log(
            "Shutdown signal received.");

        server_running = 0;

        pthread_cond_broadcast(
            &queue_not_empty);

        pthread_cond_broadcast(
            &queue_not_full);

        if (server_socket != -1)
        {
            shutdown(server_socket,
                     SHUT_RDWR);

            close(server_socket);

            server_socket = -1;
        }
    }
}

/* =========================================================
   MAIN
   ========================================================= */

int main()
{
    /*
     * Force the program to use
     * Indian Standard Time.
     */

    setenv("TZ", "Asia/Kolkata", 1);
    tzset();
 int opt = 1;

    pthread_t workers[WORKER_COUNT];

    pthread_t timer;

    struct sockaddr_in server_addr;

    /* Initialize semaphores */

    sem_init(&computer_sem,
             0,
             5);

    sem_init(&printer_sem,
             0,
             2);

    sem_init(&fileserver_sem,
             0,
             2);

    /* Signal handlers */

    signal(SIGUSR1,
           handle_signal);

    signal(SIGUSR2,
           handle_signal);

    signal(SIGINT,
           handle_signal);

    signal(SIGTERM,
           handle_signal);
  /* Start worker threads */

    for (int i = 0;
         i < WORKER_COUNT;
         i++)
    {
        int *worker_id =
            malloc(sizeof(int));

        if (worker_id == NULL)
        {
            perror("Memory allocation failed");
            return 1;
        }

        *worker_id = i + 1;

        pthread_create(
            &workers[i],
            NULL,
            worker_thread,
            worker_id);
    }

    /* Start automatic release timer */

    pthread_create(
        &timer,
        NULL,
        timer_thread,
        NULL);

    /* Create socket */

    server_socket =
        socket(AF_INET,
          SOCK_STREAM,
               0);

    if (server_socket < 0)
    {
        perror("Socket creation failed");

        server_running = 0;

        return 1;
    }

    setsockopt(
        server_socket,
        SOL_SOCKET,
        SO_REUSEADDR,
        &opt,
        sizeof(opt));

    memset(
        &server_addr,
        0,
        sizeof(server_addr));

    server_addr.sin_family =
        AF_INET;

    server_addr.sin_addr.s_addr =
        INADDR_ANY;

    server_addr.sin_port =
        htons(PORT);
 /* Bind */

    if (bind(server_socket,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0)
    {
        perror("Bind failed");

        close(server_socket);

        server_running = 0;

        return 1;
    }

    printf("[SERVER] Socket created successfully.\n");

    printf("[SERVER] Server bound to port %d.\n",
           PORT);

    /* Listen */

    if (listen(server_socket,
               MAX_CLIENTS) < 0)
    {
        perror("Listen failed");

        close(server_socket);

        server_running = 0;

        return 1;
    }

    printf("[SERVER] Server is listening on port %d.\n",
           PORT);
 char start_log[200];

    time_t server_start =
        time(NULL);

    char server_time[64];

    get_indian_time(
        server_start,
        server_time,
        sizeof(server_time));

    printf("[SERVER] Indian Time: %s IST\n",
           server_time);

    snprintf(start_log,
             sizeof(start_log),

             "University Laboratory Management Server started at %s IST.",

             server_time);

    write_log(start_log);
/* ================= ACCEPT CLIENTS ================= */

    while (server_running)
    {
        struct sockaddr_in client_addr;

        socklen_t client_len =
            sizeof(client_addr);

        int client_socket =
            accept(
                server_socket,
                (struct sockaddr *)&client_addr,
                &client_len);

        if (client_socket < 0)
        {
            if (!server_running)
                break;

            if (errno == EINTR)
                continue;

            perror("Accept failed");

            continue;
        }

        char buffer[BUFFER_SIZE];

        memset(buffer,
               0,
               sizeof(buffer));

        int bytes =
            recv(
                client_socket,
                buffer,
                sizeof(buffer) - 1,
                0);

        if (bytes <= 0)
        {
            close(client_socket);
            continue;
        }

        buffer[bytes] = '\0';

        Request request;

        memset(
            &request,
            0,
            sizeof(request));

        request.client_socket =
            client_socket;

        /*
         * Format:
         *
         * student_id|request_type|resource|duration|message
         */

        char *token;

        token =
            strtok(buffer, "|");

        if (token != NULL)
            request.student_id =
                atoi(token);

        token =
            strtok(NULL, "|");

        if (token != NULL)
            request.request_type =
                atoi(token);

        token =
            strtok(NULL, "|");

        if (token != NULL)
            request.resource =
                atoi(token);

        token =
            strtok(NULL, "|");

        if (token != NULL)
            request.duration =
                atoi(token);

        token =
            strtok(NULL, "|");

        if (token != NULL)
        {
            strncpy(
                request.message,
                token,
                sizeof(request.message) - 1);
        }

        printf("\n[SERVER] Request received "
               "from Student %d.\n",
               request.student_id);

        char log_message[300];

        snprintf(
            log_message,
            sizeof(log_message),

            "Request received from Student %d.",

            request.student_id);

        write_log(log_message);

        add_request(request);

        printf("[SERVER] Request queued "
               "for Student %d.\n",
               request.student_id);

        snprintf(
            log_message,
            sizeof(log_message),

            "Request queued for Student %d.",

            request.student_id);

        write_log(log_message);
    }

    /* ================= SHUTDOWN ================= */

    server_running = 0;

    pthread_cond_broadcast(
        &queue_not_empty);

    pthread_cond_broadcast(
        &queue_not_full);

    for (int i = 0;
         i < WORKER_COUNT;
         i++)
    {
        pthread_join(
            workers[i],
            NULL);
    }

    pthread_join(
        timer,
        NULL);

    sem_destroy(
        &computer_sem);

    sem_destroy(
        &printer_sem);

    sem_destroy(
        &fileserver_sem);

    write_log(
        "University Laboratory Management Server stopped.");

    printf("[SERVER] Server stopped safely.\n");

    return 0;
}
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queue_not_empty = PTHREAD_COND_INITIALIZER;
