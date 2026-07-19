#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

// --- Configuration Constants ---
#define MIN_EMPLOYEES 2
#define MAX_EMPLOYEES 10
#define MIN_ORDERS 10
#define MAX_ORDERS 1000

// --- Structure Definitions ---
typedef struct {
    int order_id;
    int base_priority;
    int current_priority;
    int processing_time;
    int amount;
    struct timeval arrival_time;
} Order;

typedef struct {
    Order *orders[MAX_ORDERS];
    int size;
} PriorityQueue;

typedef struct {
    int id;
    pthread_t thread;
    int processed_count;
    int total_working_time;
} Employee;

// --- Global System State ---
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;

PriorityQueue order_queue = { .size = 0 };
bool system_paused = false;
bool system_shutdown = false;
bool production_finished = false;
bool all_done = false;

// Statistics & Reporting Global Storage
int total_processed_orders = 0;
int total_processed_amount = 0;
int total_unprocessed_orders = 0;
double sum_waiting_time = 0.0;
double max_waiting_time = 0.0;

// Order tracking history arrays for final reporting
int actual_processing_order[MAX_ORDERS];
int actual_processing_count = 0;

// Utility function to get elapsed time in seconds
double get_elapsed_seconds(struct timeval start, struct timeval end) {
    return (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
}

// --- Dynamic Starvation Prevention (Aging) ---
void apply_aging() {
    struct timeval now;
    gettimeofday(&now, NULL);
    for (int i = 0; i < order_queue.size; i++) {
        Order *o = order_queue.orders[i];
        double time_in_queue = get_elapsed_seconds(o->arrival_time, now);
        
        // Every 10 seconds spent in queue increases priority by 1 up to maximum level 5
        int age_bonus = (int)(time_in_queue / 10.0);
        if (age_bonus > 0) {
            int new_priority = o->base_priority + age_bonus;
            if (new_priority > 5) new_priority = 5;
            o->current_priority = new_priority;
        }
    }
}

// --- Priority Queue Core Logic ---
// Higher priority first. If identical, older arrival time wins.
bool is_higher_priority(Order *a, Order *b) {
    if (a->current_priority != b->current_priority) {
        return a->current_priority > b->current_priority;
    }
    if (a->arrival_time.tv_sec != b->arrival_time.tv_sec) {
        return a->arrival_time.tv_sec < b->arrival_time.tv_sec;
    }
    return a->arrival_time.tv_usec < b->arrival_time.tv_usec;
}

void enqueue(Order *order) {
    order_queue.orders[order_queue.size++] = order;
}

Order* dequeue_highest_priority() {
    if (order_queue.size == 0) return NULL;
    
    apply_aging(); // Dynamically re-evaluate priorities based on waiting time

    int best_idx = 0;
    for (int i = 1; i < order_queue.size; i++) {
        if (is_higher_priority(order_queue.orders[i], order_queue.orders[best_idx])) {
            best_idx = i;
        }
    }
    
    Order *best_order = order_queue.orders[best_idx];
    
    // Overwrite the best with the last element
    order_queue.orders[best_idx] = order_queue.orders[order_queue.size - 1];
    order_queue.size--;
    return best_order;
}

// --- Employee Thread Worker Routine ---
void* employee_worker(void* arg) {
    Employee *emp = (Employee*)arg;
    
    while (true) {
        pthread_mutex_lock(&queue_mutex);
        
        // Wait conditions: loop if empty AND production is active OR if system is PAUSED
        while ((order_queue.size == 0 && !production_finished && !system_shutdown) || (system_paused && !system_shutdown)) {
            pthread_cond_wait(&queue_cond, &queue_mutex);
        }
        
        // Check for termination criteria
        if (system_shutdown) {
            pthread_mutex_unlock(&queue_mutex);
            break;
        }
        if (production_finished && order_queue.size == 0) {
            pthread_mutex_unlock(&queue_mutex);
            break;
        }
        
        // Safely extract work item
        Order *order = dequeue_highest_priority();
        pthread_mutex_unlock(&queue_mutex);
        
        if (order != NULL) {
            struct timeval start_processing;
            gettimeofday(&start_processing, NULL);
            
            // Calculate and accumulate key tracking metrics
            double wait_time = get_elapsed_seconds(order->arrival_time, start_processing);
            
            pthread_mutex_lock(&queue_mutex);
            sum_waiting_time += wait_time;
            if (wait_time > max_waiting_time) {
                max_waiting_time = wait_time;
            }
            actual_processing_order[actual_processing_count++] = order->order_id;
            printf("Employee %d started processing Order %d\n", emp->id, order->order_id);
            pthread_mutex_unlock(&queue_mutex);
            
            // Execution Simulation (Mutex MUST be released during processing)
            sleep(order->processing_time);
            
            pthread_mutex_lock(&queue_mutex);
            printf("Employee %d finished Order %d after %d seconds\n", emp->id, order->order_id, order->processing_time);
            
            emp->processed_count++;
            emp->total_working_time += order->processing_time;
            total_processed_orders++;
            total_processed_amount += order->amount;
            
            pthread_mutex_unlock(&queue_mutex);
            
            free(order); // Release memory allocation
        }
    }
    return NULL;
}

// --- Interactive Commands Listener Thread ---
void* command_listener(void* arg) {
    char cmd[50];
    while (true) {
        pthread_mutex_lock(&queue_mutex);
        if (system_shutdown || all_done){
            pthread_mutex_unlock(&queue_mutex);
            break;
        }
        pthread_mutex_unlock(&queue_mutex);

        if (fgets(cmd, sizeof(cmd), stdin) == NULL) continue;
        
        // Strip out trailing newlines
        cmd[strcspn(cmd, "\n")] = 0;
        
        pthread_mutex_lock(&queue_mutex);
        if (strcmp(cmd, "PAUSE") == 0) {
            system_paused = true;
            printf("[System Status] Execution PAUSED. Workers will halt after current jobs.\n");
        } 
        else if (strcmp(cmd, "RESUME") == 0) {
            system_paused = false;
            printf("[System Status] Execution RESUMED.\n");
            pthread_cond_broadcast(&queue_cond);
        } 
        else if (strncmp(cmd, "CANCEL", 6) == 0) {
            int target_id;
            if (sscanf(cmd, "CANCEL %d", &target_id) == 1) {
                bool found = false;
                for (int i = 0; i < order_queue.size; i++) {
                    if (order_queue.orders[i]->order_id == target_id) {
                        Order *cancelled_order = order_queue.orders[i];
                        // Overwrite the canceled order with the last element
                        order_queue.orders[i] = order_queue.orders[order_queue.size - 1];
                        order_queue.size--;
                        free(cancelled_order);
                        found = true;
                        printf("[Order Registry] Order %d successfully cancelled from queue.\n", target_id);
                        break;
                    }
                }
                if (!found) {
                    printf("[Order Registry] Cancellation failed: Order %d is currently active or invalid.\n", target_id);
                }
            }
        } 
        else if (strcmp(cmd, "SHUTDOWN") == 0) {
            system_shutdown = true;
            total_unprocessed_orders = order_queue.size;
            // Purge remaining elements from the execution queue
            for (int i = 0; i < order_queue.size; i++) {
                free(order_queue.orders[i]);
            }
            order_queue.size = 0;
            printf("[System Status] CRITICAL: Emergency SHUTDOWN initialized.\n");
            pthread_cond_broadcast(&queue_cond);
            pthread_mutex_unlock(&queue_mutex);
            break;
        }
        pthread_mutex_unlock(&queue_mutex);
    }
    return NULL;
}


// Main Thread Execution Acts as Interactive Safe Producer Logic Flow
void producer(int num_orders){
    for (int i = 1; i <= num_orders; i++) {
    
        // Random Interval Production Constraints: 100ms - 500ms
        int delay_ms = 100 + rand() % 401;
        usleep(delay_ms * 1000);
        
        Order *new_order = malloc(sizeof(Order));
        new_order->order_id = i;
        new_order->base_priority = 1 + rand() % 5;      // Priority: 1-5
        new_order->current_priority = new_order->base_priority;
        new_order->processing_time = 1 + rand() % 4;   // Processing time: 1-4 seconds
        new_order->amount = (1 + rand() % 30) * 50;    // Random pricing values
        gettimeofday(&new_order->arrival_time, NULL);

        pthread_mutex_lock(&queue_mutex);
        if (system_shutdown) {
            pthread_mutex_unlock(&queue_mutex);
            free(new_order);
            break;
        }

        enqueue(new_order);
        printf("Order %d: Priority %d, ProcessingTime %d, Amount %d\n", 
               new_order->order_id, new_order->base_priority, new_order->processing_time, new_order->amount);
        
        pthread_cond_signal(&queue_cond);
        pthread_mutex_unlock(&queue_mutex);
    }
}

// --- Main Program Entry & Orchestrator ---
int main() {
    int num_employees, num_orders;
    
    // User Configuration Prompt with strict safe constraints checks
    printf("Enter number of employees (%d-%d): ", MIN_EMPLOYEES, MAX_EMPLOYEES);
    if (scanf("%d", &num_employees) != 1 || num_employees < MIN_EMPLOYEES || num_employees > MAX_EMPLOYEES) {
        fprintf(stderr, "Invalid configurations input.\n");
        return EXIT_FAILURE;
    }
    
    printf("Enter number of orders (%d-%d): ", MIN_ORDERS, MAX_ORDERS);
    if (scanf("%d", &num_orders) != 1 || num_orders < MIN_ORDERS || num_orders > MAX_ORDERS) {
        fprintf(stderr, "Invalid configurations input.\n");
        return EXIT_FAILURE;
    }
    
    // Clear out stdin remaining buffers for fgets to act properly later
    int c; while ((c = getchar()) != '\n' && c != EOF);
    
    srand(time(NULL));
    
    // Initializing Employee Records & Workers Threads Execution Pools
    Employee *employees = malloc(sizeof(Employee) * num_employees);
    for (int i = 0; i < num_employees; i++) {
        employees[i].id = i + 1;
        employees[i].processed_count = 0;
        employees[i].total_working_time = 0;
        pthread_create(&employees[i].thread, NULL, employee_worker, &employees[i]);
    }
    
    // Launching System Interactive Console Terminal Engine
    pthread_t listener_tid;
    pthread_create(&listener_tid, NULL, command_listener, NULL);
    
    printf("\n--- Processing Center Online. Type commands (PAUSE, RESUME, CANCEL <id>, SHUTDOWN) anytime ---\n\n");
    
    // main thread produces orders
    producer(num_orders);
    
    pthread_mutex_lock(&queue_mutex);
    production_finished = true;
    pthread_cond_broadcast(&queue_cond);
    pthread_mutex_unlock(&queue_mutex);
    
    // Join employee threads to wait for execution wrap-ups
    for (int i = 0; i < num_employees; i++) {
        pthread_join(employees[i].thread, NULL);
    }

    pthread_mutex_lock(&queue_mutex);
    all_done = true;
    pthread_mutex_unlock(&queue_mutex);

    printf("\n --- All orders have been processed ---\n");
    printf("\n --- Press 'Enter' to see the results ---\n");
    

    pthread_join(listener_tid, NULL);
    


    // --- Post-Execution Comprehensive Statistics Summary Report ---
    printf("\n==================================================\n");
    printf("               FINAL EXECUTION REPORT             \n");
    printf("==================================================\n");
    printf("Total processed orders: %d\n", total_processed_orders);
    printf("Total processed amount: %d\n", total_processed_amount);
    if (system_shutdown) {
        printf("Total unprocessed orders (due to SHUTDOWN): %d\n", total_unprocessed_orders);
    }
    
    printf("\n--- Worker Employee Performance Summary ---\n");
    int max_idx = 0;
    for (int i = 0; i < num_employees; i++) {
        printf("Employee %d processed %d order(s) | Total Working Time: %d sec\n", 
               employees[i].id, employees[i].processed_count, employees[i].total_working_time);
        if (employees[i].processed_count > employees[max_idx].processed_count) {
            max_idx = i;
        }
    }
    
    if (total_processed_orders > 0) {
        printf("\nMost efficient employee (Highest Orders Processed): Employee %d\n", employees[max_idx].id);
        printf("Average order waiting time: %.4f seconds\n", sum_waiting_time / total_processed_orders);
        printf("Maximum order waiting time: %.4f seconds\n", max_waiting_time);
    }
    
    printf("\nActual Processing Sequence Tracking: [");
    for (int i = 0; i < actual_processing_count; i++) {
        printf("%d%s", actual_processing_order[i], (i == actual_processing_count - 1) ? "" : ", ");
    }
    printf("]\n");
    printf("==================================================\n");
    
    // Memory Clean-up Actions
    free(employees);
    pthread_mutex_destroy(&queue_mutex);
    pthread_cond_destroy(&queue_cond);
    
    return 0;
}