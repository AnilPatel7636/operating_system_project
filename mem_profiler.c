// 2204219 - Kishlay Pal
// 2203102 - Anil Patel

// Instructions to run the code:-

// 1. Save this file(mem_profiler) in a folder.
// 2. Go to the location of folder containing this file in terminal.
// 3. type command "gcc -o mem_profiler mem_profiler.c"

// -> Now, to check memory stats of any process , we need its PID, so using command "top" in terminal, you will get list of all running process along with PID's
// -> Suppose you are runing google chrome, then you can write its PID as an argument , as shown in next step.
// 4. type command "./mem_profiler <PID>"
// -> Then the program starts and you can see the output in file "memory_profile.log" in same folder.
// -> You can try to navigate/use (chrome or any process) like, open new tabs or running youtube , open some website to observe change in its memory status.



#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h> 

int X = 0; // global variable to get timestamps(just like indexing for better understanding the output)

//mutex (`lock`) ensures thread-safe access to shared resources like `alloc_list` & `mem_stats`. 
//it prevents race conditions and data corruption by allowing only one thread to access critical sections at a time.
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

//struct for memory allocation tracking
typedef struct allocation{
    void *address;
    size_t size;
    struct allocation *next;
}allocation_t;

//linked list head for tracking allocations
allocation_t *alloc_list = NULL;

//struct to hold memory statistics
typedef struct{
    size_t total_allocated;  // tracks total allocated heap memory
    size_t total_freed;      //tracks total free memory
    size_t peak_usage;
}memory_stats_t;

memory_stats_t mem_stats = {0,0,0};

//struct to store previous memory stats for comparison, (if it is same then it will not shown again in output file)
typedef struct{
    long virtual_mem;
    long resident_mem;
    size_t heap_allocated;
    size_t heap_freed;
    size_t heap_peak_usage;
    long stack_size;
}previous_memory_stats_t;

previous_memory_stats_t prev_mem_stats = {0,0,0,0,0,0};

//adding new allocation to the list
void add_allocation(void *ptr, size_t size) {
    allocation_t *new_alloc = (allocation_t *)malloc(sizeof(allocation_t));
    if(new_alloc){
        new_alloc->address = ptr;
        new_alloc->size = size;
        new_alloc->next = NULL;

        pthread_mutex_lock(&lock);
        new_alloc->next = alloc_list;
        alloc_list = new_alloc;
        pthread_mutex_unlock(&lock);
    }
}

//removing an allocation from the list and get its size
size_t remove_allocation(void *ptr){
    size_t size = 0;
    pthread_mutex_lock(&lock);
    allocation_t **current = &alloc_list;
    while(*current){
        if ((*current)->address == ptr) {
            allocation_t *to_remove = *current;
            size = to_remove->size;
            *current = to_remove->next;
            free(to_remove);
            break;
        }
        current = &(*current)->next;
    }
    pthread_mutex_unlock(&lock);
    return size;
}

//custom malloc with tracking(heap)
void *custom_malloc(size_t size){
    void *ptr = malloc(size);
    if(ptr){
        pthread_mutex_lock(&lock);
        mem_stats.total_allocated += size;  //updating total allocated memory
        if(mem_stats.total_allocated - mem_stats.total_freed > mem_stats.peak_usage){
            mem_stats.peak_usage = mem_stats.total_allocated - mem_stats.total_freed;
        }
        pthread_mutex_unlock(&lock);

        add_allocation(ptr, size);
    }
    return ptr;
}

//custom free with tracking(heap)
void custom_free(void *ptr){
    if(ptr){
        size_t size = remove_allocation(ptr);  // remove from tracking list
        if(size>0){
            pthread_mutex_lock(&lock);
            mem_stats.total_freed += size;  // updating total freed memory
            pthread_mutex_unlock(&lock);
        }
        free(ptr);  //free the actual memory
    }
}

//reading memory usage from /proc/[pid]/statm
void read_memory_usage(int pid, long *virtual_mem, long *resident_mem){
    char path[40],line[100];
    snprintf(path, sizeof(path), "/proc/%d/statm", pid);  //construct file path
    FILE *f = fopen(path, "r");

    if(f){
        fgets(line, sizeof(line), f);  //read the memory data
        fclose(f);

        long size, resident;
        sscanf(line, "%ld %ld", &size, &resident);   //parsing the memory values

        *virtual_mem = size * sysconf(_SC_PAGE_SIZE)/1024; //convert to KB
        *resident_mem = resident * sysconf(_SC_PAGE_SIZE)/1024; //convert to KB
    }
}

//reading stack size from /proc/[pid]/status
void read_stack_size(int pid, long *stack_size){
    char path[40], line[256];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    FILE *f = fopen(path, "r");

    if(f){
        while(fgets(line,sizeof(line), f)){      //iterating through file lines
            if(strncmp(line,"VmStk:",6) == 0){   // find stack size line
                sscanf(line + 6, "%ld", stack_size); //extracting stack size in KB
            }
        }
        fclose(f);
    }
}

//get process name from /proc/[pid]/comm
void get_process_name(int pid, char *process_name, size_t max_len){
    char path[40];
    snprintf(path, sizeof(path), "/proc/%d/comm", pid);
    FILE *f = fopen(path, "r");
    if(f){
        fgets(process_name, max_len, f);   //read the process name
        process_name[strcspn(process_name, "\n")] = '\0'; //remove trailing newline
        fclose(f);
    } 
    else{
        strncpy(process_name, "Unknown", max_len);
    }
}


//compare current memory stats with previous ones( if changed then showm them in output else skip)
int has_memory_changed(long virtual_mem, long resident_mem, size_t heap_allocated, size_t heap_freed, size_t heap_peak_usage, long stack_size){
    return virtual_mem != prev_mem_stats.virtual_mem ||
           resident_mem != prev_mem_stats.resident_mem ||
           heap_allocated != prev_mem_stats.heap_allocated ||
           heap_freed != prev_mem_stats.heap_freed ||
           heap_peak_usage != prev_mem_stats.heap_peak_usage ||
           stack_size != prev_mem_stats.stack_size;
}

//inference function to determine the cause of memory change(if any)
const char* generate_inference(long virtual_mem, long resident_mem, size_t heap_allocated, size_t heap_freed, size_t heap_peak_usage, long stack_size){
    static char inference[2048];
    inference[0] = '\0'; //reset the inference message

    // append observations based on memory changes
    if(virtual_mem > prev_mem_stats.virtual_mem){
        strcat(inference, "\nVirtual memory increased.\n   The process is storing large datasets, buffers, or mapping files into memory.\n   Therefore, workload increased.");
    } 
    else if(virtual_mem < prev_mem_stats.virtual_mem){
        strcat(inference, "\nVirtual memory decreased.\n   The memory-mapped files or large allocations are being released.");
    }

    if(resident_mem > prev_mem_stats.resident_mem){
        strcat(inference, "\nResident memory increased.\n   More physical memory (RAM) is being actively used by the application. ");
    } 
    else if(resident_mem < prev_mem_stats.resident_mem){
        strcat(inference, "\nResident memory decreased.\n   Pages in RAM are swapped out to disk (in systems with swap space) or memory is released and is no longer required.");
    }

    if(heap_allocated > prev_mem_stats.heap_allocated){
        strcat(inference, "\nHeap allocated increased.\n   The application is dynamically allocating more memory on the heap (e.g., using malloc, calloc, or new in C/C++) ");
    } 
    else if(heap_allocated < prev_mem_stats.heap_allocated){
        strcat(inference, "\nHeap allocated decreased.\n   The application has released memory, via free() or similar calls ");
    }

    if(heap_freed > prev_mem_stats.heap_freed){
        strcat(inference, "\nHeap freed increased.\n   The application is releasing memory it no longer needs. ");
    } 
    else if(heap_freed < prev_mem_stats.heap_freed){
        strcat(inference, "\nHeap freed decreased.\n   Memory allocated earlier is being kept for future use, or it may indicate that memory is not being properly managed (potential memory leak).");
    }

    if(heap_peak_usage > prev_mem_stats.heap_peak_usage){
        strcat(inference, "\nHeap peak usage increased.\n   Indicates the maximum memory demand that the application required at any point. Helps identify memory bottlenecks ");
    } 
    else if(heap_peak_usage < prev_mem_stats.heap_peak_usage){
        strcat(inference, "\nHeap peak usage decreased.\n   Possibly due to more efficient memory management, optimized algorithms, or reduced workload. ");
    }

    if(stack_size > prev_mem_stats.stack_size){
        strcat(inference, "\nStack size increased.\n   Deeper recursion or larger local variables being used in the application.\n   Could also be due to too many nested function calls, excessive recursion, or large local data structures,");
    } 
    else if(stack_size < prev_mem_stats.stack_size){
        strcat(inference, "\nStack size decreased.\n   Application has returned from function calls, ");
    }
    return inference[0] != '\0' ? inference : "No significant change in memory.";
}


//checking if the log file exists and is empty
int is_log_file_empty(const char* log_filename) {
    struct stat st;
    if(stat(log_filename, &st) == 0 && st.st_size == 0){
        return 1; //empty or does not exist
    }
    return 0; //file has data
}

// log memory usage for a specific process
void log_memory_usage(int pid) {

    // NOTE: "memory_profile.log" is created on running this file , which contains our output.
    const char* log_filename = "memory_profile.log";

    //check if the log file is empty (first run for this process)
    if(is_log_file_empty(log_filename)){
        //open the log file in "w" mode to clear the previous content (for every new process when we run program it clears previous outputs for better readability)
        FILE* log = fopen(log_filename, "w"); // "w" will clear the file
        if(log){
            // initial setup for the log
            char process_name[256] = {0};
            get_process_name(pid, process_name, sizeof(process_name));

            //read memory stats
            long virtual_mem = 0, resident_mem = 0, stack_size = 0;
            read_memory_usage(pid, &virtual_mem, &resident_mem);
            read_stack_size(pid, &stack_size);

            size_t heap_allocated = mem_stats.total_allocated / 1024;
            size_t heap_freed = mem_stats.total_freed / 1024;
            size_t heap_peak_usage = mem_stats.peak_usage / 1024;
            if(has_memory_changed(virtual_mem, resident_mem, heap_allocated, heap_freed, heap_peak_usage, stack_size)){
                pthread_mutex_lock(&lock);
                //prints inference first
                fprintf(log, "Timestamp: %d\n",X);
                fprintf(log, "Process Name: %s\n", process_name);
                X=X+1;
                //fprintf(log, "Inference: %s\n", generate_inference(virtual_mem, resident_mem, heap_allocated, heap_freed, heap_peak_usage, stack_size));
                fprintf(log, "---------------------------------------------\n");
                fprintf(log, "Memory Profile for PID: %d\n", pid);
                fprintf(log, "---------------------------------------------\n");
                fprintf(log, "Memory Type         | Usage (KB)\n");
                fprintf(log, "---------------------------------------------\n");
                fprintf(log, "Virtual Memory      | %ld\n", virtual_mem);
                fprintf(log, "Resident Memory     | %ld\n", resident_mem);
                fprintf(log, "Heap Allocated      | %zu\n", heap_allocated);
                fprintf(log, "Heap Freed          | %zu\n", heap_freed);
                fprintf(log, "Heap Peak Usage     | %zu\n", heap_peak_usage);
                fprintf(log, "Stack Size          | %ld\n", stack_size);
                fprintf(log, "---------------------------------------------\n\n\n");
                pthread_mutex_unlock(&lock);

                //update previous memory stats
                prev_mem_stats.virtual_mem = virtual_mem;
                prev_mem_stats.resident_mem = resident_mem;
                prev_mem_stats.heap_allocated = heap_allocated;
                prev_mem_stats.heap_freed = heap_freed;
                prev_mem_stats.heap_peak_usage = heap_peak_usage;
                prev_mem_stats.stack_size = stack_size;
            }

            fclose(log);
        }
    } 
    else{
        //if the log file is not empty, append new data
        FILE* log = fopen(log_filename, "a"); //open in append mode
        if(log){
            long virtual_mem = 0, resident_mem = 0, stack_size = 0;
            read_memory_usage(pid, &virtual_mem, &resident_mem);
            read_stack_size(pid, &stack_size);
            size_t heap_allocated = mem_stats.total_allocated / 1024;
            size_t heap_freed = mem_stats.total_freed / 1024;
            size_t heap_peak_usage = mem_stats.peak_usage / 1024;
            if(has_memory_changed(virtual_mem, resident_mem, heap_allocated, heap_freed, heap_peak_usage, stack_size)){
                pthread_mutex_lock(&lock);
                //write inference first
                fprintf(log, "Timestamp: %d\n",X);
                X=X+1;
                fprintf(log, "Inference: %s\n", generate_inference(virtual_mem, resident_mem, heap_allocated, heap_freed, heap_peak_usage, stack_size));
                fprintf(log, "---------------------------------------------\n");
                fprintf(log, "Memory Profile for PID: %d\n", pid);
                fprintf(log, "---------------------------------------------\n");
                fprintf(log, "Memory Type         | Usage (KB)\n");
                fprintf(log, "---------------------------------------------\n");
                fprintf(log, "Virtual Memory      | %ld\n", virtual_mem);
                fprintf(log, "Resident Memory     | %ld\n", resident_mem);
                fprintf(log, "Heap Allocated      | %zu\n", heap_allocated);
                fprintf(log, "Heap Freed          | %zu\n", heap_freed);
                fprintf(log, "Heap Peak Usage     | %zu\n", heap_peak_usage);
                fprintf(log, "Stack Size          | %ld\n", stack_size);
                fprintf(log, "---------------------------------------------\n\n\n");
                pthread_mutex_unlock(&lock);

                //update previous memory stats
                prev_mem_stats.virtual_mem = virtual_mem;
                prev_mem_stats.resident_mem = resident_mem;
                prev_mem_stats.heap_allocated = heap_allocated;
                prev_mem_stats.heap_freed = heap_freed;
                prev_mem_stats.heap_peak_usage = heap_peak_usage;
                prev_mem_stats.stack_size = stack_size;
            }

            fclose(log);
        }
    }
}

int main(int argc, char *argv[]){

    //checks for a valid PID argument, initializes logging, and starts periodic profiling
    if(argc<2){
        fprintf(stderr, "Usage: %s <PID>\n", argv[0]);
        return EXIT_FAILURE;
    }

    printf("Check memory_profile.log file for results\n");
    printf("Press Ctrl+C to exit\n");
    printf("Running Memory Profiler...\n");

    int pid = atoi(argv[1]);
    FILE *log = fopen("memory_profile.log","w");

    //log memory usage periodically
    while(1){
        log_memory_usage(pid);
        sleep(2); //log every second
    }

    return 0;
}
