# Advanced Operating Systems Project  

This project is an exercise developed as part of the **Advanced Operating Systems** course at Harokopio University of Athens. The goal is to explore key operating system concepts through practical programming tasks, including process management, inter-process communication, and graceful shutdown techniques.  

## Description  
The exercise involves:  
- **Creating a Process Tree**: A parent process spawns multiple child processes, and both write messages to an output file.  
- **Inter-Process Communication**: Communication between parent and child processes is achieved using pipes, with messages being exchanged in both directions.  
- **Worker Pool Paradigm**: Idle child processes can be assigned tasks dynamically by the parent process, forming a basic worker pool.  
- **Graceful Shutdown**: Ensuring proper cleanup of resources and termination of all child processes when the parent process is terminated.
- **RPC**: There is a simple example of RPC with rpcgen. 

This project is implemented in **C** and runs in a **Linux environment**.  

## How to Run  

### Prerequisites  
- **Linux Environment** (virtual machine or physical hardware).  
- **C Compiler** (e.g., `gcc`).
- `make` for building and running the project  

### Instructions  

1. Clone the repository:  
   ```bash  
   git clone git@github.com:Markets05/AdvanceOS.git
   git clone https://github.com/Markets05/AdvanceOS.git
   ```
   
2. **Compile the Program** using the `Makefile` by simply running:  
   ```bash  
   make  
   ```  
   This will build the executable based on the `Makefile` settings.

3. **Run the Program**:  
   ```bash  
   make run
   ```  
   This will run the program with the specified output filename and number of child processes. You can modify the `run` target in the `Makefile` to suit your needs.  
   
   Alternatively, you can also run the program manually after building it:
   ```bash  
   ./main <output_filename> <number_of_children>  
   ```  
   - Replace `<output_filename>` with the desired name of the output file.  
   - Replace `<number_of_children>` with the number of child processes to create.

## Makefile Commands  
The `Makefile` includes the following commands:

- **`make`** (default):  
  Compiles the project and creates the executable file. It also shows the "Build successful!" message when the build is complete.

- **`make run`**:  
  Executes the program, creating a process tree with the specified number of child processes and writing to the specified output file.

- **`make clean`**:  
  Removes the compiled executable.

- **`make help`**:  
  Displays how to run the program.

## Example Usage  

To create a process tree with 5 child processes and save the output to `output.txt`:  
```bash  
make run  
```

If you want to build the project manually and run it afterward:
```bash  
make  
./main output.txt 5  
```

## Graceful Shutdown  

To ensure proper cleanup, the program supports **graceful shutdown**, terminating all child processes before the parent exits. You can stop the program using one of the following methods:  

### 1️⃣ Pressing `CTRL + C`  
This sends a `SIGINT` signal to the parent process, triggering a clean shutdown.  

### 2️⃣ Using `kill` from Another Terminal  
If you want to terminate the program externally:  

1. **Find the parent process ID (`parent_pid`)**:  
   ```bash
   ps -e | grep main
   ```  
   Identify the smallest `PID` in the output (this is the parent process).  

2. **Send a `SIGTERM` signal**:  
   ```bash
   kill -SIGTERM <parent_pid>
   ```  
   This sends a `SIGTERM` signal to the parent process, triggering a clean shutdown. 

## Remote Procedure Call (RPC) with `rpcgen`

This section demonstrates communication between processes using RPC with `rpcgen`.

1. **Compile the RPC files**  
   Run the following command to compile the server and client files:
   ```bash
   make -f Makefile.add
   ```

2. **Run the Server**  
   Start the server using:
   ```bash
   sudo ./add_server
   ```

3. **Run the Client**  
   Connect to the server by running:
   ```bash
   ./add_client localhost
   ```
The client will connect to the server on localhost and perform RPC calls.

## Author  
Xenofon Marketakis it2022063@hua.gr
