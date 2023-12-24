#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <string.h>
#include <fcntl.h>

#define DAEMON_NAME "webserver"
#define WORKING_DIRECTORY "/"
#define STORAGE_PATH "/Users/polinaglezova/webserver/pages"
#define START_PAGE "/start.html"
#define PORT 8000
#define MAX_CHILDREN 100

pid_t children[MAX_CHILDREN];
int listener;
int num_children = 0;

// обработка сигнала SIGTERM
void handleSIGTERM(int signum) {
    // завершение веб-сервера с записью в журнал
    syslog(LOG_INFO, "Received SIGTERM signal. Exiting");

    for (int i = 0; i < num_children; i++) {
        killpg(getpgid(children[i]), SIGTERM);
        waitpid(children[i], NULL, 0);
    }

    close(listener);
    closelog();
    exit(EXIT_SUCCESS);
}

void handleSIGCHLD(int signum) {
    pid_t pid;
    int status;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        syslog(LOG_INFO, "Child process %d terminated", pid);

        // удаление завершенных процессов из массива
        for (int i = 0; i < num_children; i++) {
            if (children[i] == pid) {
                // смещение элементов
                for (int j = i; j < num_children - 1; j++) {
                    children[j] = children[j + 1];
                }
                num_children--;
                break;
            }
        }
    }
}

void start_daemon() {
         // отделение от родительского процесса
         pid_t pid, sid;
         pid = fork();

        if (pid < 0) {
            syslog(LOG_ERR, "Fork failed\n");
            exit(EXIT_FAILURE);
        }

        if (pid > 0) {
            exit(EXIT_SUCCESS);
        }

        // изменение файловой маски
        umask(0);

        // создание уникального ID сессии
        sid = setsid();
        if (sid < 0) {
            syslog(LOG_ERR, "Failed to create session ID\n");
            exit(EXIT_FAILURE);
        }

        // изменение текущего рабочего каталога
        if ((chdir(WORKING_DIRECTORY)) < 0) {
            syslog(LOG_ERR, "Failed to change working directory\n");
            exit(EXIT_FAILURE);
        }

        // закрытие стандартных файловых дескрипторов
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
}

void send_file(int sock, const char* file_path, const char* dot, const char* type) {
        char buf[1024];
        FILE *file = fopen(file_path, "rb");

        if (! file) {
            syslog(LOG_ERR, "Failed to open file %s", file_path);
            return;
        }

        fseek(file, 0, SEEK_END);
        long file_size = ftell(file);
        fseek(file, 0, SEEK_SET);
        
        // формируем заголовок и тело ответа
        sprintf(buf, "HTTP/1.1 200 OK\nContent-Type: %s/%s\nContent-Length: %ld\n\n", type, dot, file_size);
        send(sock, buf, strlen(buf), 0);
        syslog(LOG_INFO, "HTTP/1.1 200 OK\nContent-Type: %s/%s\nContent-Length: %ld", type, dot, file_size);

        char* response = (char*)malloc(file_size);
        fread(response, 1, file_size, file);

        int bytes_sended = send(sock, response, file_size, 0);

        fclose(file);
        free(response);
}

void send_404(int sock) {
    syslog(LOG_INFO, "HTTP/1.1 404 Not Found\nContent-Type: text/html");
    char response[] = "HTTP/1.1 404 Not Found\nContent-Type: text/html\n\n"
                      "<html><head><title>404 Not Found</title></head><body>"
                      "<h1>404 Not Found</h1><p>The requested resource could not be found.</p>"
                      "</body></html>";
    send(sock, response, strlen(response), 0);
}

void send_request(char* url_path, int sock){
    char file_path[256];

    if (strcmp(url_path, "/") == 0) {
        sprintf(file_path, "%s%s", STORAGE_PATH, START_PAGE);
        if (access(file_path, F_OK) != -1) {
            send_file(sock, file_path, "html", "text");
        }
        else {
            send_404(sock);
        }
    } else {
        sprintf(file_path, "%s%s", STORAGE_PATH, url_path);

        if (access(file_path, F_OK) != -1) {
        const char* dot = strrchr(file_path, '.');
        dot += 1;
        char* type;

        if (strcmp(dot, "img") == 0 || strcmp(dot, "jpg") == 0 || strcmp(dot, "jpeg") == 0) {
            type = "image";
           } else {
            type = "text";
           }
        send_file(sock, file_path, dot, type);
        } else {
            send_404(sock);
        }
    }
}

void parse_request(int sock) {
    char buf[1024];
    recv(sock, buf, sizeof(buf), 0);

    char* method = strtok(buf, " ");
    char* url_path = strtok(NULL, " ");
    
    if (method != NULL && url_path != NULL) {
        syslog(LOG_INFO, "Recieved %s request", method);
        syslog(LOG_INFO, "Requested %s by %s", url_path, method);
        if (strcmp(method, "GET") == 0) {
            // обработка GET запроса
            send_request(url_path, sock);
        }    
        else if (strcmp(method, "POST") == 0) {
            // обработка POST запроса
            send_request(url_path, sock);
        }
    }
    else {
        send_404(sock);
    }

    close(sock);
}


void next_client(int listener) {
    //родительский процесс занимается только прослушиванием порта и приемом соединений
    int sock = accept(listener, NULL, NULL);

    if (sock < 0) {
        syslog(LOG_ERR, "Failed to accept client");
        return;
    }
    
    pid_t pid = fork();
    // создание дочернего процесса
    switch(pid) {
        case -1:
            syslog(LOG_ERR, "Failed to fork new client");
            break;

        case 0:
            close(listener);
            parse_request(sock);
            close(sock);
            _exit(0);

        default:
            close(sock);
            children[num_children++] = getpid();
    }
}

int main(){
        // открытие журнала на запись
        openlog("webserver", LOG_PID | LOG_CONS, LOG_USER);

        //запуск демона
        start_daemon();

        // обработка сигналов
        signal(SIGTERM, handleSIGTERM);
        signal(SIGCHLD, handleSIGCHLD);
        
        struct sockaddr_in addr;
        listener = socket(AF_INET, SOCK_STREAM, 0);
        if (listener < 0) {
            syslog(LOG_ERR, "Failed to create listener socket");
            exit(EXIT_FAILURE);
        }
        
        addr.sin_family = AF_INET;
        addr.sin_port = htons(PORT);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);

        if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            syslog(LOG_ERR, "Failed to bind listener");
            exit(EXIT_FAILURE);
        }

        syslog(LOG_INFO, "Web-server successfully launched on 127.0.0.1:%d", PORT);
        listen(listener, 10);

        while(1) {
            next_client(listener);
        }
}