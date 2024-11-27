# Memory Profiler

The Memory Profiler is a command-line tool that monitors the memory usage of a running process in real time. It tracks various memory metrics and logs them into a file for analysis.

## Instructions to Run the Code

1. Download code file as `mem_profiler.c` in a folder.
2. Open a terminal and navigate to the folder containing `mem_profiler.c`.
3. Compile the code using the following command:
   ```bash
   gcc -o mem_profiler mem_profiler.c
   ```
     ### Obtaining the Process ID (PID)

     To monitor a process's memory usage, you first need to obtain its PID (Process ID).

     a. Open a terminal.
   
     b. Use the `top` command to get a list of all running processes along with their PIDs.  
       Run the following command:
       
     c. Find the process you want to monitor in the list. For example, if you are running Google Chrome, look for it in the list and note the 
      corresponding PID.

   
4.Run the compiled program by passing the PID as an argument:
  ```bash
  ./mem_profiler <PID>
  ```

5.The program will start monitoring the memory usage of the specified process and log the memory statistics in a file named memory_profile.log located in the same folder where the program is executed.

6.While the program is running, you can interact with the monitored process (e.g., open new tabs in Chrome, play a YouTube video, or browse a website) to observe changes in its memory usage. These changes will be recorded in the memory_profile.log file.




