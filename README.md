## Features
1. Multiprocess client handling using `fork()`.
2. Monitoring of child processes to prevent zombie processes; immediate termination of all child processes upon parent process termination.
3. Web server operates as a daemon.
4. Basic support for HTTP protocol:
   - GET and POST methods;
   - status codes 200 OK and 404 Not Found;
   - sending text and binary files (images);
5. Logging through syslog.
6. Handling SIGTERM.

## Usage example
### To start the web server at `http://localhost:8000`
Compile:
```
gcc -o web web.c
```
Launch:
```
./web
```
Check if the server is successfully launched (PID):
- macOS: 
```
pgrep -x web
```
- Linux:
```
pidof "./web"
```
Terminate the process:
```
sudo kill -15 <PID>
```
