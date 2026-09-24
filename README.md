## **OSSP (Operating Systems And Systems Programming)**
Course code: 25CS2104E
Section: 3
Project: University Laboratory Management Server

## Project Overview

The University Laboratory Management Server is a Linux-based client-server application developed for the Operating Systems and Systems Programming (OSSP) course. It manages multiple student requests for limited laboratory resources such as computers, printers, and file servers.

The project demonstrates important Operating System concepts including POSIX threads, mutexes, semaphores, signals, process management, synchronization, and file I/O.

## Objectives

- Handle multiple student requests concurrently.
- Manage and allocate limited laboratory resources safely.
- Use threads, mutexes, and semaphores for synchronization.
- Monitor processes, threads, and resource usage.
- Maintain activity logs and support controlled server shutdown.

## Working

Student Client
      |
      v
   Server
      |
      v
 Request Queue
      |
      v
 Worker Threads
      |
      v
 Resource Allocation
      |
      v
 Processing
      |
      v
 Response to Client
      |
      v
 Activity Logging

Students can request a computer, printer, or file server. The server checks resource availability and either allocates the resource or places the request in a waiting queue.

## Operating System Concepts Used

- POSIX Threads – Handle multiple requests concurrently
- Mutex – Protect shared data
- Semaphores – Control limited resources
- Signals – Server control and shutdown
- File I/O – Maintain activity logs
- Process Management – Monitor processes
- Synchronization – Prevent race conditions

## Features

- Student resource requests
- Shared request queue
- Concurrent request processing
- Resource availability dashboard
- Student-specific resource status
- Semaphore-based resource control
- Server activity logging
- Thread and process monitoring
- Signal-based server control
- Controlled shutdown

## Technologies Used

- Linux / Ubuntu
- C / C++
- POSIX Libraries
- GCC
- VS Code

## How to Run

Compile the server:

gcc lab_server.c -o lab_server -pthread

Run the server:

./lab_server

Open another terminal and compile the client:

gcc lab_client.c -o lab_client

Run the client:

./lab_client

Enter the Student ID and select an option from the menu.

## Monitoring and Shutdown

View server threads:

ps -T -p <SERVER_PID>

View server activity log:

cat server.log

Check pending requests:

kill -USR1 <SERVER_PID>

Check resource availability:

kill -USR2 <SERVER_PID>

Safely shut down the server:

kill -TERM <SERVER_PID>

## Team Members

2520030022 – A V S N R Akhila  
2520030098 – Gutha Sushma Sree  
2520030335 – Manasvi Nilesh Ghag

## Individual Contributions

A V S N R Akhila – Server process management, process monitoring, signals, synchronization, and server shutdown.

Gutha Sushma Sree – POSIX threads, shared request queue, mutexes, condition variables, and semaphores.

Manasvi Nilesh Ghag – Laboratory activity logs, transaction logs, Linux file I/O, file descriptors, and resource/request data handling.

## Expected Outcome

The project provides a working Linux-based laboratory management server that can handle multiple student requests simultaneously, safely manage limited resources, and maintain request and activity information.

## Repository

https://github.com/guthasushmasree/OSSP--University-Laboratory-Management-Server
