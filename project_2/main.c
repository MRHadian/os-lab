#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PROCS 10

typedef struct {
    char id[10];
    int id_num; // Extracted number for tie-breaking
    int arrival_time;
    int burst_time;
    int priority;
    
    // Runtime properties
    int remaining_time;
    int first_start_time;
    int completion_time;
    int waiting_time;
    int turnaround_time;
    int response_time;
    int preemption_count;
    int is_completed;
} Process;

typedef struct {
    char name[20];
    int start;
    int end;
} Gantt;

typedef struct {
    float avg_wt;
    float avg_tat;
    float avg_rt;
    int total_cs;
    float cpu_util;
    float throughput;
} AlgoStats;

enum { FCFS, SJF, SRTF, PRIORITY_NONPREEMPT, PRIORITY_PREEMPTIVE, RR };
const char* algo_names[] = {"FCFS", "SJF", "SRTF", "Priority (NP)", "Priority (P)", "Round Robin"};

// Global variables for Gantt chart
Gantt gantt_chart[10000];
int gantt_count = 0;

void record_gantt(const char* name, int start, int end) {
    if (gantt_count > 0 && strcmp(gantt_chart[gantt_count - 1].name, name) == 0 && gantt_chart[gantt_count - 1].end == start) {
        gantt_chart[gantt_count - 1].end = end;
    } else {
        strcpy(gantt_chart[gantt_count].name, name);
        gantt_chart[gantt_count].start = start;
        gantt_chart[gantt_count].end = end;
        gantt_count++;
    }
}

int find_best_process(Process *p, int n, int current_time, int algo) {
    int best_idx = -1;
    for (int i = 0; i < n; i++) {
        if (p[i].arrival_time <= current_time && !p[i].is_completed) {
            if (best_idx == -1) {
                best_idx = i;
                continue;
            }

            int replace = 0;
            if (algo == FCFS) {
                if (p[i].arrival_time < p[best_idx].arrival_time) replace = 1;
                else if (p[i].arrival_time == p[best_idx].arrival_time && p[i].id_num < p[best_idx].id_num) replace = 1;
            }
            else if (algo == SJF || algo == SRTF) {
                if (p[i].remaining_time < p[best_idx].remaining_time) replace = 1;
                else if (p[i].remaining_time == p[best_idx].remaining_time) {
                    if (p[i].arrival_time < p[best_idx].arrival_time) replace = 1;
                    else if (p[i].arrival_time == p[best_idx].arrival_time && p[i].id_num < p[best_idx].id_num) replace = 1;
                }
            }
            else if (algo == PRIORITY_NONPREEMPT || algo == PRIORITY_PREEMPTIVE) {
                // Higher number means higher priority
                if (p[i].priority > p[best_idx].priority) replace = 1;
                else if (p[i].priority == p[best_idx].priority) {
                    if (p[i].arrival_time < p[best_idx].arrival_time) replace = 1;
                    else if (p[i].arrival_time == p[best_idx].arrival_time && p[i].id_num < p[best_idx].id_num) replace = 1;
                }
            }

            if (replace) {
                best_idx = i;
            }
        }
    }
    return best_idx;
}

void run_simulation(Process* original_procs, int n, int cs_time, int quantum, int algo, AlgoStats* stat_out) {
    Process p[MAX_PROCS];
    int total_burst = 0;
    for (int i = 0; i < n; i++) {
        p[i] = original_procs[i];
        p[i].remaining_time = p[i].burst_time;
        p[i].first_start_time = -1;
        p[i].completion_time = 0;
        p[i].waiting_time = 0;
        p[i].turnaround_time = 0;
        p[i].response_time = 0;
        p[i].preemption_count = 0;
        p[i].is_completed = 0;
        total_burst += p[i].burst_time;
    }

    gantt_count = 0;
    
    int current_time = 0;
    int completed = 0;
    int current_proc = -1;
    int pending_proc = -1;
    int prev_proc = -1; 
    int cs_timer = 0;
    int quantum_left = 0;
    int total_idle = 0;
    int total_cs = 0;
    int total_preemptions = 0;

    // Queue for Round Robin
    int q[10000];
    int q_front = 0, q_rear = 0;

    while (completed < n) {
        // A. Handle new arrivals for Round Robin Queue
        if (algo == RR) {
            int arrived_now[MAX_PROCS];
            int c = 0;
            for (int i = 0; i < n; i++) {
                if (p[i].arrival_time == current_time && !p[i].is_completed) {
                    arrived_now[c++] = i;
                }
            }
            // Sort simultaneous arrivals by process ID
            for (int i = 0; i < c - 1; i++) {
                for (int j = i + 1; j < c; j++) {
                    if (p[arrived_now[i]].id_num > p[arrived_now[j]].id_num) {
                        int temp = arrived_now[i];
                        arrived_now[i] = arrived_now[j];
                        arrived_now[j] = temp;
                    }
                }
            }
            for (int i = 0; i < c; i++) {
                q[q_rear++] = arrived_now[i];
            }
        }

        // B. Handle Ongoing Context Switch
        if (cs_timer > 0) {
            record_gantt("CONTEXT_SWITCH", current_time, current_time + 1);
            cs_timer--;
            if (cs_timer == 0) {
                current_proc = pending_proc;
                pending_proc = -1;
                prev_proc = current_proc;
                quantum_left = quantum;
            }
            current_time++;
            continue; // CPU is busy doing CS
        }

        // C. Check Preemption
        if (current_proc != -1) {
            int preempted = 0;
            if (algo == RR) {
                if (quantum_left == 0) preempted = 1;
            } else if (algo == SRTF || algo == PRIORITY_PREEMPTIVE) {
                int best = find_best_process(p, n, current_time, algo);
                if (best != -1 && best != current_proc) {
                    preempted = 1;
                }
            }

            if (preempted) {
                p[current_proc].preemption_count++;
                total_preemptions++;
                if (algo == RR) {
                    q[q_rear++] = current_proc; // Re-queue preempted process
                }
                current_proc = -1; // CPU gets freed
            }
        }

        // D. Select Next Process
        if (current_proc == -1) {
            int next_proc = -1;
            if (algo == RR) {
                if (q_front < q_rear) {
                    next_proc = q[q_front++];
                }
            } else {
                next_proc = find_best_process(p, n, current_time, algo);
            }

            if (next_proc != -1) {
                if (prev_proc != -1 && prev_proc != next_proc && cs_time > 0) {
                    cs_timer = cs_time;
                    pending_proc = next_proc;
                    total_cs++;
                } else {
                    current_proc = next_proc;
                    prev_proc = current_proc;
                    quantum_left = quantum;
                }
            }
        }

        // E. Execute or IDLE
        if (current_proc != -1) {
            if (p[current_proc].first_start_time == -1) {
                p[current_proc].first_start_time = current_time;
            }
            record_gantt(p[current_proc].id, current_time, current_time + 1);
            p[current_proc].remaining_time--;
            quantum_left--;

            if (p[current_proc].remaining_time == 0) {
                p[current_proc].completion_time = current_time + 1;
                p[current_proc].turnaround_time = p[current_proc].completion_time - p[current_proc].arrival_time;
                p[current_proc].waiting_time = p[current_proc].turnaround_time - p[current_proc].burst_time;
                p[current_proc].response_time = p[current_proc].first_start_time - p[current_proc].arrival_time;
                p[current_proc].is_completed = 1;
                completed++;
                current_proc = -1;
            }
        } else if (cs_timer > 0) {
            // First tick of a newly triggered CS
            record_gantt("CONTEXT_SWITCH", current_time, current_time + 1);
            cs_timer--;
            if (cs_timer == 0) {
                current_proc = pending_proc;
                pending_proc = -1;
                prev_proc = current_proc;
                quantum_left = quantum;
            }
        } else {
            // CPU is IDLE
            record_gantt("IDLE", current_time, current_time + 1);
            total_idle++;
            prev_proc = -1; // Going IDLE avoids CS penalty for the next process
        }

        current_time++;
    }

    // Print Output for this Algorithm
    printf("\n======================================================\n");
    printf("Algorithm: %s\n", algo_names[algo]);
    printf("======================================================\n\n");
    
    printf("Gantt Chart:\n");
    for (int i = 0; i < gantt_count; i++) {
        printf("[%d-%d: %s]\n", gantt_chart[i].start, gantt_chart[i].end, gantt_chart[i].name);
    }

    float sum_wt = 0, sum_tat = 0, sum_rt = 0;
    int max_wt = -1, min_wt = 999999;
    
    printf("\nProcess Table:\n");
    printf("%-7s %-7s %-7s %-8s %-10s %-7s %-10s %-8s %-10s\n",
           "Process", "Arrival", "Burst", "Priority", "Completion", "Waiting", "Turnaround", "Response", "Preemption");
    for (int i = 0; i < n; i++) {
        printf("%-7s %-7d %-7d %-8d %-10d %-7d %-10d %-8d %-10d\n",
               p[i].id, p[i].arrival_time, p[i].burst_time, p[i].priority,
               p[i].completion_time, p[i].waiting_time, p[i].turnaround_time, p[i].response_time, p[i].preemption_count);
        
        sum_wt += p[i].waiting_time;
        sum_tat += p[i].turnaround_time;
        sum_rt += p[i].response_time;
        if (p[i].waiting_time > max_wt) max_wt = p[i].waiting_time;
        if (p[i].waiting_time < min_wt) min_wt = p[i].waiting_time;
    }

    stat_out->avg_wt = sum_wt / n;
    stat_out->avg_tat = sum_tat / n;
    stat_out->avg_rt = sum_rt / n;
    stat_out->total_cs = total_cs;
    stat_out->cpu_util = ((float)total_burst / current_time) * 100.0f;
    stat_out->throughput = (float)n / current_time;

    printf("\nMetrics:\n");
    printf("- Average Waiting Time: %.2f\n", stat_out->avg_wt);
    printf("- Average Turnaround Time: %.2f\n", stat_out->avg_tat);
    printf("- Average Response Time: %.2f\n", stat_out->avg_rt);
    printf("- Max Waiting Time: %d\n", max_wt);
    printf("- Min Waiting Time: %d\n", min_wt);
    printf("- Total Context Switches: %d\n", total_cs);
    printf("- Total Preemptions: %d\n", total_preemptions);
    printf("- Total Simulation Time: %d\n", current_time);
    printf("- Total CPU Idle Time: %d\n", total_idle);
    printf("- CPU Utilization: %.2f%%\n", stat_out->cpu_util);
    printf("- Throughput: %.4f\n", stat_out->throughput);
}

int main() {
    int n, cs_time, quantum;
    
    printf("Number of Processes: ");
    if (scanf("%d", &n) != 1 || n < 3 || n > 10) {
        printf("Error: Number of processes must be between 3 and 10.\n");
        return 1;
    }
    
    printf("Context Switch Time: ");
    if (scanf("%d", &cs_time) != 1 || cs_time < 0) {
        printf("Error: Context Switch Time must be >= 0.\n");
        return 1;
    }
    
    printf("Quantum (for Round Robin): ");
    if (scanf("%d", &quantum) != 1 || quantum <= 0) {
        printf("Error: Quantum must be > 0.\n");
        return 1;
    }

    Process procs[MAX_PROCS];
    printf("\nEnter Process details (ProcessID ArrivalTime BurstTime Priority):\n");
    for (int i = 0; i < n; i++) {
        char id_str[10];
        int arr, bur, pri;
        if (scanf("%s %d %d %d", id_str, &arr, &bur, &pri) != 4) {
             printf("Error: Invalid input format.\n"); 
             return 1;
        }
        if (arr < 0 || bur <= 0 || pri < 1 || pri > 10) {
             printf("Error: Invalid constraints for process %s.\n", id_str); 
             return 1;
        }
        strcpy(procs[i].id, id_str);
        if (sscanf(id_str, "P%d", &procs[i].id_num) != 1) {
            procs[i].id_num = i; // Fallback if format is not exactly 'P#'
        }
        procs[i].arrival_time = arr;
        procs[i].burst_time = bur;
        procs[i].priority = pri;
    }

    AlgoStats stats[6];
    
    for (int algo = 0; algo < 6; algo++) {
        run_simulation(procs, n, cs_time, quantum, algo, &stats[algo]);
    }

    // Final Comparison Table
    printf("\n=========================================================================================================\n");
    printf("Algorithm Comparison\n");
    printf("=========================================================================================================\n");
    printf("%-15s %-15s %-15s %-15s %-18s %-16s %-15s\n",
           "Algorithm", "Avg Waiting", "Avg Turnaround", "Avg Response", "Context Switches", "CPU Utilization", "Throughput");
    printf("---------------------------------------------------------------------------------------------------------\n");
    for (int i = 0; i < 6; i++) {
        printf("%-15s %-15.2f %-15.2f %-15.2f %-18d %-15.2f%% %.4f\n",
               algo_names[i], stats[i].avg_wt, stats[i].avg_tat, stats[i].avg_rt,
               stats[i].total_cs, stats[i].cpu_util, stats[i].throughput);
    }

    // Determine Best Algorithms
    int best_wt_idx = 0, best_tat_idx = 0, best_rt_idx = 0, best_cs_idx = 0, best_util_idx = 0;
    for (int i = 1; i < 6; i++) {
        if (stats[i].avg_wt < stats[best_wt_idx].avg_wt) best_wt_idx = i;
        if (stats[i].avg_tat < stats[best_tat_idx].avg_tat) best_tat_idx = i;
        if (stats[i].avg_rt < stats[best_rt_idx].avg_rt) best_rt_idx = i;
        if (stats[i].total_cs < stats[best_cs_idx].total_cs) best_cs_idx = i;
        if (stats[i].cpu_util > stats[best_util_idx].cpu_util) best_util_idx = i;
    }

    printf("\n======================================================\n");
    printf("Best Algorithms By Category\n");
    printf("======================================================\n");
    printf("- Lowest Average Waiting Time: %s (%.2f)\n", algo_names[best_wt_idx], stats[best_wt_idx].avg_wt);
    printf("- Lowest Average Turnaround Time: %s (%.2f)\n", algo_names[best_tat_idx], stats[best_tat_idx].avg_tat);
    printf("- Lowest Average Response Time: %s (%.2f)\n", algo_names[best_rt_idx], stats[best_rt_idx].avg_rt);
    printf("- Lowest Context Switches: %s (%d)\n", algo_names[best_cs_idx], stats[best_cs_idx].total_cs);
    printf("- Highest CPU Utilization: %s (%.2f%%)\n", algo_names[best_util_idx], stats[best_util_idx].cpu_util);
    printf("======================================================\n");

    return 0;
}