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

/* ================= RESOURCE IDs ================= */

#define COMPUTER    1
#define PRINTER     2
#define FILESERVER  3
#define PROJECTOR   4
#define SCANNER     5
#define HEADSET     6

/* ================= REQUEST ================= */

typedef struct
{
    int client_socket;
    int student_id;
    int request_type;
    int resource;
    int duration;
    int is_http;
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

/* ================= RESOURCE TOTALS ================= */

/*
   Original resources are unchanged:
   Computers    = 5
   Printers     = 2
   File Servers = 2

   New resources:
   Projectors   = 2
   Scanners     = 2
   Headsets     = 2
*/

int resource_total[7] =
{
    0,
    10,  /* Computers */
    5,  /* Printers */
    5,  /* File Servers */
    5,  /* Projectors */
    5,  /* Scanners */
    5   /* Headsets */
};

/* ================= AVAILABLE RESOURCES ================= */

int resource_available[7] =
{
    0,
    10,
    5,
    5,
    5,
    5,
    5
};

/* ================= SEMAPHORES ================= */

sem_t computer_sem;
sem_t printer_sem;
sem_t fileserver_sem;
sem_t projector_sem;
sem_t scanner_sem;
sem_t headset_sem;

/* ================= ALLOCATION TABLE ================= */

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
        strftime(
            output,
            size,
            "%H:%M %d-%m-%Y",
            time_info
        );
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

    get_indian_time(
        now,
        time_string,
        sizeof(time_string)
    );

    fprintf(
        file,
        "[%s IST] %s\n",
        time_string,
        message
    );

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

        case PROJECTOR:
            return "Projector";

        case SCANNER:
            return "Scanner";

        case HEADSET:
            return "Headset";

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

        case PROJECTOR:
            return &projector_sem;

        case SCANNER:
            return &scanner_sem;

        case HEADSET:
            return &headset_sem;

        default:
            return NULL;
    }
}

/* =========================================================
   HTTP SUPPORT
   ========================================================= */

void send_http_header(int client_socket, const char *content_type)
{
    char header[512];

    snprintf(
        header,
        sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-store\r\n"
        "\r\n",
        content_type
    );

    send(client_socket, header, strlen(header), 0);
}

void send_http_error(int client_socket, int status, const char *message)
{
    const char *status_text = "Internal Server Error";

    if (status == 400)
        status_text = "Bad Request";
    else if (status == 404)
        status_text = "Not Found";
    else if (status == 405)
        status_text = "Method Not Allowed";

    char body[1024];
    snprintf(body, sizeof(body), "%s\n", message);

    char header[512];
    snprintf(
        header,
        sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        status,
        status_text,
        strlen(body)
    );

    send(client_socket, header, strlen(header), 0);
    send(client_socket, body, strlen(body), 0);
}

const char *get_content_type(const char *path)
{
    const char *extension = strrchr(path, '.');

    if (extension == NULL)
        return "text/plain; charset=utf-8";

    if (strcmp(extension, ".html") == 0)
        return "text/html; charset=utf-8";

    if (strcmp(extension, ".css") == 0)
        return "text/css; charset=utf-8";

    if (strcmp(extension, ".js") == 0)
        return "application/javascript; charset=utf-8";

    if (strcmp(extension, ".json") == 0)
        return "application/json; charset=utf-8";

    return "application/octet-stream";
}

void serve_static_file(int client_socket, const char *url_path)
{
    const char *relative_path = NULL;

    if (strcmp(url_path, "/") == 0 ||
        strcmp(url_path, "/index.html") == 0)
    {
        relative_path = "lab_frontend/index.html";
    }
    else if (strcmp(url_path, "/style.css") == 0)
    {
        relative_path = "lab_frontend/style.css";
    }
    else if (strcmp(url_path, "/script.js") == 0)
    {
        relative_path = "lab_frontend/script.js";
    }
    else
    {
        send_http_error(
            client_socket,
            404,
            "Requested frontend file was not found."
        );
        return;
    }

    FILE *file = fopen(relative_path, "rb");

    if (file == NULL)
    {
        send_http_error(
            client_socket,
            404,
            "Frontend file could not be opened."
        );
        return;
    }

    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        send_http_error(client_socket, 500, "Could not read frontend file.");
        return;
    }

    long file_size = ftell(file);

    if (file_size < 0)
    {
        fclose(file);
        send_http_error(client_socket, 500, "Could not determine file size.");
        return;
    }

    rewind(file);

    char header[512];

    snprintf(
        header,
        sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-store\r\n"
        "\r\n",
        get_content_type(relative_path),
        file_size
    );

    send(client_socket, header, strlen(header), 0);

    char buffer[8192];
    size_t bytes_read;

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0)
    {
        size_t sent = 0;

        while (sent < bytes_read)
        {
            ssize_t result = send(
                client_socket,
                buffer + sent,
                bytes_read - sent,
                0
            );

            if (result <= 0)
            {
                fclose(file);
                return;
            }

            sent += (size_t)result;
        }
    }

    fclose(file);
}

int get_query_int(const char *target, const char *key, int default_value)
{
    const char *query = strchr(target, '?');

    if (query == NULL)
        return default_value;

    query++;

    char query_copy[1024];
    strncpy(query_copy, query, sizeof(query_copy) - 1);
    query_copy[sizeof(query_copy) - 1] = '\0';

    char *pair = strtok(query_copy, "&");

    while (pair != NULL)
    {
        char *equals = strchr(pair, '=');

        if (equals != NULL)
        {
            *equals = '\0';

            if (strcmp(pair, key) == 0)
                return atoi(equals + 1);
        }

        pair = strtok(NULL, "&");
    }

    return default_value;
}

int is_http_request(const char *buffer)
{
    return strncmp(buffer, "GET ", 4) == 0 ||
           strncmp(buffer, "HEAD ", 5) == 0;
}

/* =========================================================
   RESOURCE DASHBOARD
   ========================================================= */

void show_resource_status(int client_socket)
{
    char response[BUFFER_SIZE];

    pthread_mutex_lock(&resource_mutex);

    snprintf(
        response,
        sizeof(response),

        "\n"
        "========================================\n"
        "        LAB RESOURCE DASHBOARD\n"
        "========================================\n"
        "Computers    : %d/%d available\n"
        "Printers     : %d/%d available\n"
        "File Servers : %d/%d available\n"
        "Projectors   : %d/%d available\n"
        "Scanners     : %d/%d available\n"
        "Headsets     : %d/%d available\n"
        "========================================\n",

        resource_available[COMPUTER],
        resource_total[COMPUTER],

        resource_available[PRINTER],
        resource_total[PRINTER],

        resource_available[FILESERVER],
        resource_total[FILESERVER],

        resource_available[PROJECTOR],
        resource_total[PROJECTOR],

        resource_available[SCANNER],
        resource_total[SCANNER],

        resource_available[HEADSET],
        resource_total[HEADSET]
    );

    pthread_mutex_unlock(&resource_mutex);

    send(
        client_socket,
        response,
        strlen(response),
        0
    );

    write_log("Resource dashboard viewed.");
}

/* =========================================================
   VIEW ALL ALLOCATIONS
   ========================================================= */

void show_all_allocations(int client_socket)
{
    char response[BUFFER_SIZE * 4];
    char temp[700];

    response[0] = '\0';

    pthread_mutex_lock(&resource_mutex);

    strcat(
        response,
        "\n"
        "========================================\n"
        "       CURRENT RESOURCE ALLOCATIONS\n"
        "========================================\n"
    );

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
                sizeof(start_time)
            );

            get_indian_time(
                allocations[i].expected_end_time,
                end_time,
                sizeof(end_time)
            );

            time_t now = time(NULL);

            int used_time =
                (int)difftime(
                    now,
                    allocations[i].start_time
                );

            if (used_time < 0)
                used_time = 0;

            snprintf(
                temp,
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
                    allocations[i].resource
                ),

                allocations[i].duration,

                used_time,

                start_time,

                end_time
            );

            strcat(response, temp);
        }
    }

    if (!found)
    {
        strcat(
            response,
            "No resources are currently allocated.\n"
        );
    }

    strcat(
        response,
        "========================================\n"
    );

    pthread_mutex_unlock(&resource_mutex);

    send(
        client_socket,
        response,
        strlen(response),
        0
    );

    write_log("All allocated resources viewed.");
}

/* =========================================================
   STUDENT SPECIFIC STATUS
   ========================================================= */

void show_student_status(
    int client_socket,
    int student_id
)
{
    char response[BUFFER_SIZE * 2];
    char temp[700];

    response[0] = '\0';

    pthread_mutex_lock(&resource_mutex);

    snprintf(
        response,
        sizeof(response),

        "\n"
        "========================================\n"
        "       STUDENT RESOURCE STATUS\n"
        "========================================\n"
        "Student ID : %d\n",

        student_id
    );

    int found = 0;

    for (int i = 0; i < allocation_count; i++)
    {
        if (
            allocations[i].allocated &&
            allocations[i].student_id == student_id
        )
        {
            found = 1;

            time_t now = time(NULL);

            int elapsed =
                (int)difftime(
                    now,
                    allocations[i].start_time
                );

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
                sizeof(start_time)
            );

            get_indian_time(
                allocations[i].expected_end_time,
                end_time,
                sizeof(end_time)
            );

            snprintf(
                temp,
                sizeof(temp),

                "\nResource       : %s\n"
                "Requested Time : %d seconds\n"
                "Used Time      : %d seconds\n"
                "Remaining Time : %d seconds\n"
                "Start Time     : %s IST\n"
                "Expected End   : %s IST\n",

                get_resource_name(
                    allocations[i].resource
                ),

                allocations[i].duration,

                elapsed,

                remaining,

                start_time,

                end_time
            );

            strcat(response, temp);
        }
    }

    if (!found)
    {
        strcat(
            response,
            "\nNo resource is currently allocated "
            "to this student.\n"
        );
    }

    strcat(
        response,
        "\n========================================\n"
    );

    pthread_mutex_unlock(&resource_mutex);

    send(
        client_socket,
        response,
        strlen(response),
        0
    );

    write_log("Student-specific status viewed.");
}

/* =========================================================
   DUPLICATE ALLOCATION CHECK
   ========================================================= */

int has_duplicate_allocation(
    int student_id,
    int resource
)
{
    for (int i = 0; i < allocation_count; i++)
    {
        if (
            allocations[i].allocated &&
            allocations[i].student_id == student_id &&
            allocations[i].resource == resource
        )
        {
            return 1;
        }
    }

    return 0;
}

/* =========================================================
   ADD ALLOCATION
   ========================================================= */

void add_allocation(
    int student_id,
    int resource,
    int duration
)
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

    snprintf(
        log_message,
        sizeof(log_message),

        "Student %d released %s after %d seconds.",

        student_id,

        get_resource_name(resource),

        duration
    );

    write_log(log_message);

    printf(
        "[RESOURCE] Student %d released %s.\n",
        student_id,
        get_resource_name(resource)
    );
}

/* =========================================================
   CHECK EXPIRED ALLOCATIONS
   ========================================================= */

void check_expired_allocations()
{
    time_t now = time(NULL);

    pthread_mutex_lock(&resource_mutex);

    for (
        int i = 0;
        i < allocation_count;
        i++
    )
    {
        if (
            allocations[i].allocated &&
            now >= allocations[i].expected_end_time
        )
        {
            int resource =
                allocations[i].resource;

            int student_id =
                allocations[i].student_id;

            printf(
                "[RESOURCE] Automatically releasing %s "
                "from Student %d.\n",

                get_resource_name(resource),
                student_id
            );

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

    while (
        queue_count == QUEUE_SIZE &&
        server_running
    )
    {
        pthread_cond_wait(
            &queue_not_full,
            &queue_mutex
        );
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
        &queue_not_empty
    );

    pthread_mutex_unlock(&queue_mutex);
}

/* =========================================================
   GET REQUEST FROM QUEUE
   ========================================================= */

Request get_request()
{
    Request request;

    memset(
        &request,
        0,
        sizeof(Request)
    );

    pthread_mutex_lock(&queue_mutex);

    while (
        queue_count == 0 &&
        server_running
    )
    {
        pthread_cond_wait(
            &queue_not_empty,
            &queue_mutex
        );
    }

    if (
        !server_running &&
        queue_count == 0
    )
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
        &queue_not_full
    );

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

    if (
        resource < COMPUTER ||
        resource > HEADSET
    )
    {
        const char *message =
            "ERROR: Invalid resource.\n";

        send(
            request.client_socket,
            message,
            strlen(message),
            0
        );

        return;
    }

    if (request.duration <= 0)
    {
        const char *message =
            "ERROR: Duration must be greater than 0 seconds.\n";

        send(
            request.client_socket,
            message,
            strlen(message),
            0
        );

        return;
    }

    /* Duplicate allocation check */

    pthread_mutex_lock(&resource_mutex);

    if (
        has_duplicate_allocation(
            request.student_id,
            resource
        )
    )
    {
        pthread_mutex_unlock(&resource_mutex);

        char response[500];

        snprintf(
            response,
            sizeof(response),

            "ERROR: Student %d already has a %s allocated.\n",

            request.student_id,

            get_resource_name(resource)
        );

        send(
            request.client_socket,
            response,
            strlen(response),
            0
        );

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

        send(
            request.client_socket,
            message,
            strlen(message),
            0
        );

        return;
    }

    char log_message[300];

    snprintf(
        log_message,
        sizeof(log_message),

        "Student %d waiting for %s.",

        request.student_id,

        get_resource_name(resource)
    );

    write_log(log_message);

    printf(
        "[RESOURCE] Student %d waiting for %s.\n",
        request.student_id,
        get_resource_name(resource)
    );

    /* Semaphore controls limited resources */

    sem_wait(sem);

    pthread_mutex_lock(&resource_mutex);

    resource_available[resource]--;

    pthread_mutex_unlock(&resource_mutex);

    add_allocation(
        request.student_id,
        resource,
        request.duration
    );

    time_t start_time = time(NULL);

    char start_string[64];
    char end_string[64];

    get_indian_time(
        start_time,
        start_string,
        sizeof(start_string)
    );

    get_indian_time(
        start_time + request.duration,
        end_string,
        sizeof(end_string)
    );

    snprintf(
        log_message,
        sizeof(log_message),

        "Student %d acquired %s for %d seconds. "
        "Start: %s IST, Expected End: %s IST.",

        request.student_id,

        get_resource_name(resource),

        request.duration,

        start_string,

        end_string
    );

    write_log(log_message);

    printf(
        "\n[RESOURCE] %s acquired.\n",
        get_resource_name(resource)
    );

    printf(
        "[RESOURCE] Student ID : %d\n",
        request.student_id
    );

    printf(
        "[RESOURCE] Duration    : %d seconds\n",
        request.duration
    );

    printf(
        "[RESOURCE] Start Time  : %s IST\n",
        start_string
    );

    printf(
        "[RESOURCE] Expected End: %s IST\n\n",
        end_string
    );

    char response[BUFFER_SIZE];

    snprintf(
        response,
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

        request.duration
    );

    send(
        request.client_socket,
        response,
        strlen(response),
        0
    );
}

/* =========================================================
   WORKER THREAD
   ========================================================= */

void *worker_thread(void *arg)
{
    int worker_id =
        *((int *)arg);

    free(arg);

    printf(
        "[WORKER %d] Started.\n",
        worker_id
    );

    char log_message[300];

    snprintf(
        log_message,
        sizeof(log_message),
        "Worker %d started.",
        worker_id
    );

    write_log(log_message);

    while (server_running)
    {
        Request request =
            get_request();

        if (
            !server_running &&
            request.client_socket == 0
        )
        {
            break;
        }

        if (request.client_socket == 0)
            continue;

        if (request.is_http)
        {
            send_http_header(
                request.client_socket,
                "text/plain; charset=utf-8"
            );
        }

        printf(
            "[WORKER %d] Processing Student %d request.\n",
            worker_id,
            request.student_id
        );

        snprintf(
            log_message,
            sizeof(log_message),

            "Worker %d processing Student %d request.",

            worker_id,

            request.student_id
        );

        write_log(log_message);

        /*
         * Request types:
         *
         * 1 = Computer
         * 2 = Printer
         * 3 = File Server
         * 4 = Projector
         * 5 = Scanner
         * 6 = Headset
         *
         * 7 = Dashboard
         * 8 = All Allocations
         * 9 = My Status
         */

        if (
            request.request_type >= 1 &&
            request.request_type <= 6
        )
        {
            process_resource_request(request);
        }
        else if (request.request_type == 7)
        {
            show_resource_status(
                request.client_socket
            );
        }
        else if (request.request_type == 8)
        {
            show_all_allocations(
                request.client_socket
            );
        }
        else if (request.request_type == 9)
        {
            show_student_status(
                request.client_socket,
                request.student_id
            );
        }
        else
        {
            const char *message =
                "ERROR: Invalid request type.\n";

            send(
                request.client_socket,
                message,
                strlen(message),
                0
            );
        }

        close(request.client_socket);
    }

    printf(
        "[WORKER %d] Stopped.\n",
        worker_id
    );

    snprintf(
        log_message,
        sizeof(log_message),
        "Worker %d stopped.",
        worker_id
    );

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

        printf(
            "[SIGNAL] Pending requests: %d\n",
            pending
        );

        char message[200];

        snprintf(
            message,
            sizeof(message),
            "SIGUSR1: Pending requests = %d.",
            pending
        );

        write_log(message);
    }

    else if (signal_number == SIGUSR2)
    {
        printf(
            "[SIGNAL] Resource status requested.\n"
        );

        pthread_mutex_lock(&resource_mutex);

        printf(
            "Computers    : %d/%d available\n",
            resource_available[COMPUTER],
            resource_total[COMPUTER]
        );

        printf(
            "Printers     : %d/%d available\n",
            resource_available[PRINTER],
            resource_total[PRINTER]
        );

        printf(
            "File Servers : %d/%d available\n",
            resource_available[FILESERVER],
            resource_total[FILESERVER]
        );

        printf(
            "Projectors   : %d/%d available\n",
            resource_available[PROJECTOR],
            resource_total[PROJECTOR]
        );

        printf(
            "Scanners     : %d/%d available\n",
            resource_available[SCANNER],
            resource_total[SCANNER]
        );

        printf(
            "Headsets     : %d/%d available\n",
            resource_available[HEADSET],
            resource_total[HEADSET]
        );

        pthread_mutex_unlock(&resource_mutex);

        write_log(
            "SIGUSR2: Resource status requested."
        );
    }

    else if (
        signal_number == SIGINT ||
        signal_number == SIGTERM
    )
    {
        printf(
            "[SIGNAL] Shutdown signal received.\n"
        );

        write_log(
            "Shutdown signal received."
        );

        server_running = 0;

        pthread_cond_broadcast(
            &queue_not_empty
        );

        pthread_cond_broadcast(
            &queue_not_full
        );

        if (server_socket != -1)
        {
            shutdown(
                server_socket,
                SHUT_RDWR
            );

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

    setenv(
        "TZ",
        "Asia/Kolkata",
        1
    );

    tzset();

    int opt = 1;

    pthread_t workers[WORKER_COUNT];

    pthread_t timer;

    struct sockaddr_in server_addr;

    /* ================= SEMAPHORES ================= */

    sem_init(
        &computer_sem,
        0,
        10
    );

    sem_init(
        &printer_sem,
        0,
        5
    );

    sem_init(
        &fileserver_sem,
        0,
        5
    );

    sem_init(
        &projector_sem,
        0,
        5
    );

    sem_init(
        &scanner_sem,
        0,
        5
    );

    sem_init(
        &headset_sem,
        0,
        5
    );

    /* ================= SIGNAL HANDLERS ================= */

    signal(
        SIGUSR1,
        handle_signal
    );

    signal(
        SIGUSR2,
        handle_signal
    );

    signal(
        SIGINT,
        handle_signal
    );

    signal(
        SIGTERM,
        handle_signal
    );

    /* ================= WORKER THREADS ================= */

    for (
        int i = 0;
        i < WORKER_COUNT;
        i++
    )
    {
        int *worker_id =
            malloc(sizeof(int));

        if (worker_id == NULL)
        {
            perror(
                "Memory allocation failed"
            );

            return 1;
        }

        *worker_id = i + 1;

        pthread_create(
            &workers[i],
            NULL,
            worker_thread,
            worker_id
        );
    }

    /* ================= TIMER THREAD ================= */

    pthread_create(
        &timer,
        NULL,
        timer_thread,
        NULL
    );

    /* ================= CREATE SOCKET ================= */

    server_socket =
        socket(
            AF_INET,
            SOCK_STREAM,
            0
        );

    if (server_socket < 0)
    {
        perror(
            "Socket creation failed"
        );

        server_running = 0;

        return 1;
    }

    setsockopt(
        server_socket,
        SOL_SOCKET,
        SO_REUSEADDR,
        &opt,
        sizeof(opt)
    );

    memset(
        &server_addr,
        0,
        sizeof(server_addr)
    );

    server_addr.sin_family =
        AF_INET;

    server_addr.sin_addr.s_addr =
        INADDR_ANY;

    server_addr.sin_port =
        htons(PORT);

    /* ================= BIND ================= */

    if (
        bind(
            server_socket,
            (struct sockaddr *)&server_addr,
            sizeof(server_addr)
        ) < 0
    )
    {
        perror("Bind failed");

        close(server_socket);

        server_running = 0;

        return 1;
    }

    printf(
        "[SERVER] Socket created successfully.\n"
    );

    printf(
        "[SERVER] Server bound to port %d.\n",
        PORT
    );

    /* ================= LISTEN ================= */

    if (
        listen(
            server_socket,
            MAX_CLIENTS
        ) < 0
    )
    {
        perror("Listen failed");

        close(server_socket);

        server_running = 0;

        return 1;
    }

    printf(
        "[SERVER] Server is listening on port %d.\n",
        PORT
    );

    char start_log[200];

    time_t server_start =
        time(NULL);

    char server_time[64];

    get_indian_time(
        server_start,
        server_time,
        sizeof(server_time)
    );

    printf(
        "[SERVER] Indian Time: %s IST\n",
        server_time
    );

    snprintf(
        start_log,
        sizeof(start_log),

        "University Laboratory Management Server "
        "started at %s IST.",

        server_time
    );

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
                &client_len
            );

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

        memset(
            buffer,
            0,
            sizeof(buffer)
        );

        int bytes =
            recv(
                client_socket,
                buffer,
                sizeof(buffer) - 1,
                0
            );

        if (bytes <= 0)
        {
            close(client_socket);
            continue;
        }

        buffer[bytes] = '\0';

        if (is_http_request(buffer))
        {
            char method[16];
            char target[1024];

            memset(method, 0, sizeof(method));
            memset(target, 0, sizeof(target));

            if (sscanf(buffer, "%15s %1023s", method, target) != 2)
            {
                send_http_error(
                    client_socket,
                    400,
                    "Invalid HTTP request."
                );
                close(client_socket);
                continue;
            }

            if (strcmp(method, "GET") != 0)
            {
                send_http_error(
                    client_socket,
                    405,
                    "Only GET requests are supported."
                );
                close(client_socket);
                continue;
            }

            char path_only[1024];
            strncpy(path_only, target, sizeof(path_only) - 1);
            path_only[sizeof(path_only) - 1] = '\0';

            char *question = strchr(path_only, '?');

            if (question != NULL)
                *question = '\0';

            if (strcmp(path_only, "/") == 0 ||
                strcmp(path_only, "/index.html") == 0 ||
                strcmp(path_only, "/style.css") == 0 ||
                strcmp(path_only, "/script.js") == 0)
            {
                serve_static_file(client_socket, path_only);
                close(client_socket);
                continue;
            }

            Request http_request;

            memset(&http_request, 0, sizeof(http_request));

            http_request.client_socket = client_socket;
            http_request.is_http = 1;
            http_request.student_id = get_query_int(target, "student_id", 0);
            http_request.resource = get_query_int(target, "resource", 0);
            http_request.duration = get_query_int(target, "duration", 0);

            if (strcmp(path_only, "/api/dashboard") == 0)
            {
                http_request.request_type = 7;
                strcpy(http_request.message, "Dashboard request");
            }
            else if (strcmp(path_only, "/api/allocations") == 0)
            {
                http_request.request_type = 8;
                strcpy(http_request.message, "Allocation request");
            }
            else if (strcmp(path_only, "/api/status") == 0)
            {
                http_request.request_type = 9;
                strcpy(http_request.message, "Student status request");

                if (http_request.student_id <= 0)
                {
                    send_http_error(
                        client_socket,
                        400,
                        "student_id is required."
                    );
                    close(client_socket);
                    continue;
                }
            }
            else if (strcmp(path_only, "/api/request") == 0)
            {
                if (http_request.student_id <= 0 ||
                    http_request.resource < COMPUTER ||
                    http_request.resource > HEADSET ||
                    http_request.duration <= 0)
                {
                    send_http_error(
                        client_socket,
                        400,
                        "student_id, resource and a positive duration are required."
                    );
                    close(client_socket);
                    continue;
                }

                http_request.request_type = http_request.resource;
                snprintf(
                    http_request.message,
                    sizeof(http_request.message),
                    "%s",
                    get_resource_name(http_request.resource)
                );
            }
            else
            {
                send_http_error(
                    client_socket,
                    404,
                    "API endpoint not found."
                );
                close(client_socket);
                continue;
            }

            printf(
                "\n[SERVER] HTTP request received for %s\n",
                path_only
            );

            add_request(http_request);
            continue;
        }

        Request request;

        memset(
            &request,
            0,
            sizeof(request)
        );

        request.client_socket =
            client_socket;

        /*
         * Format:
         *
         * student_id|request_type|resource|duration|message
         */

        char *token;

        token =
            strtok(
                buffer,
                "|"
            );

        if (token != NULL)
        {
            request.student_id =
                atoi(token);
        }

        token =
            strtok(
                NULL,
                "|"
            );

        if (token != NULL)
        {
            request.request_type =
                atoi(token);
        }

        token =
            strtok(
                NULL,
                "|"
            );

        if (token != NULL)
        {
            request.resource =
                atoi(token);
        }

        token =
            strtok(
                NULL,
                "|"
            );

        if (token != NULL)
        {
            request.duration =
                atoi(token);
        }

        token =
            strtok(
                NULL,
                "|"
            );

        if (token != NULL)
        {
            strncpy(
                request.message,
                token,
                sizeof(request.message) - 1
            );
        }

        printf(
            "\n[SERVER] Request received "
            "from Student %d.\n",
            request.student_id
        );

        char log_message[300];

        snprintf(
            log_message,
            sizeof(log_message),

            "Request received from Student %d.",

            request.student_id
        );

        write_log(log_message);

        add_request(request);

        printf(
            "[SERVER] Request queued "
            "for Student %d.\n",
            request.student_id
        );

        snprintf(
            log_message,
            sizeof(log_message),

            "Request queued for Student %d.",

            request.student_id
        );

        write_log(log_message);
    }

    /* ================= SHUTDOWN ================= */

    server_running = 0;

    pthread_cond_broadcast(
        &queue_not_empty
    );

    pthread_cond_broadcast(
        &queue_not_full
    );

    for (
        int i = 0;
        i < WORKER_COUNT;
        i++
    )
    {
        pthread_join(
            workers[i],
            NULL
        );
    }

    pthread_join(
        timer,
        NULL
    );

    /* ================= DESTROY SEMAPHORES ================= */

    sem_destroy(
        &computer_sem
    );

    sem_destroy(
        &printer_sem
    );

    sem_destroy(
        &fileserver_sem
    );

    sem_destroy(
        &projector_sem
    );

    sem_destroy(
        &scanner_sem
    );

    sem_destroy(
        &headset_sem
    );

    write_log(
        "University Laboratory Management Server stopped."
    );

    printf(
        "[SERVER] Server stopped safely.\n"
    );

    return 0;
}
