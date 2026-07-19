# Order Processing Center Simulation

This repository contains a C-based multithreaded simulation of an Order Processing Center. It was developed to demonstrate core Operating Systems concepts, specifically the **Thread Pool** design pattern, synchronization, and shared resource management.

The program utilizes POSIX threads (`pthreads`) to simulate a team of employees concurrently processing orders from a thread-safe, dynamically prioritized queue.

## 🚀 Features

- **Thread Pool Architecture:** Worker threads (employees) are initialized once at startup and continuously pull tasks from a shared queue.
- **Thread-Safe Operations:** Complete prevention of race conditions using Mutexes (`pthread_mutex_t`) and Condition Variables (`pthread_cond_t`).
- **Zero Busy-Waiting:** Worker threads efficiently sleep when the queue is empty and are awakened only when new orders arrive.
- **Dynamic Priority Queue:** Orders are processed based on priority (1 to 5). In the event of a tie, the oldest order (FIFO) is processed first.
- **Starvation Prevention (Aging):** To prevent low-priority tasks from being ignored indefinitely, an order's priority is automatically increased for every 10 seconds it spends waiting in the queue.
- **Interactive System Console:** The system accepts real-time commands during execution:
  - `PAUSE`: Halts workers from taking new orders (active orders finish).
  - `RESUME`: Wakes up workers to continue processing.
  - `CANCEL <id>`: Removes a specific order from the queue if processing hasn't started.
  - `SHUTDOWN`: Triggers an emergency stop, clears the queue, finishes active jobs, and safely terminates all threads.
- **Comprehensive Reporting:** Generates a detailed post-execution summary including total processed amounts, max/average wait times, and individual employee efficiency.

## 📋 Prerequisites

- A C compiler (e.g., `gcc`)
- A POSIX-compliant operating system (Linux/macOS) or WSL for Windows (required for `<pthread.h>` and `<sys/time.h>`).

## 🛠️ Compilation and Execution

1.  **Clone the repository:**

    ```bash
    git clone https://github.com/MRHadian/os-lab.git
    or
    git clone git@github.com:MRHadian/os-lab.git

    cd os-lab
    cd project_1
    ```

2.  **Compile the code:**
    You must link the `pthread` library during compilation.

    ```bash
    gcc main.c -o main.exe -pthread
    ```

3.  **Run the application:**
    ```bash
    ./main
    ```

## 💻 Usage

Upon running, the program will prompt you to configure the simulation:

1.  **Number of employees:** Enter a value between `2` and `10`.
2.  **Number of orders:** Enter a value between `10` and `1000`.

The main thread will immediately begin simulating order arrivals with random intervals, priorities, and processing times. Type any of the interactive commands (`PAUSE`, `RESUME`, `CANCEL <id>`, `SHUTDOWN`) directly into the terminal while the program is running.

_Note: Once all orders are produced and the queue is empty, you must press `Enter` to unblock the console listener and view the final report_.

## 📊 Sample Output

```text
Enter number of employees (2-10): 3
Enter number of orders (10-1000): 15

--- Processing Center Online. Type commands (PAUSE, RESUME, CANCEL <id>, SHUTDOWN) anytime ---

Order 1: Priority 3, ProcessingTime 2, Amount 450
Employee 1 started processing Order 1
Order 2: Priority 5, ProcessingTime 4, Amount 1200
Employee 2 started processing Order 2
...
Employee 1 finished Order 1 after 2 seconds
...
==================================================
               FINAL EXECUTION REPORT
==================================================
Total processed orders: 15
Total processed amount: 11250

--- Worker Employee Performance Summary ---
Employee 1 processed 6 order(s) | Total Working Time: 14 sec
Employee 2 processed 4 order(s) | Total Working Time: 12 sec
Employee 3 processed 5 order(s) | Total Working Time: 15 sec

Most efficient employee (Highest Orders Processed): Employee 1
Average order waiting time: 0.1245 seconds
Maximum order waiting time: 0.8520 seconds

Actual Processing Sequence Tracking: [1, 2, 3, 5, 4, 6, 7, 8, 9, 11, 10, 12, 13, 14, 15]
==================================================
```
