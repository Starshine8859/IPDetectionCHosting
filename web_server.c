#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <pthread.h>
#include <arpa/inet.h>

#define PORT 8080
#define MAX_CONN_QUEUE 5
#define BUF_SIZE 4096
#define HTML_SIZE 4096

int counter = 0;
int client_connected = 0;
char *current_client_ip = NULL;
pthread_mutex_t lock;
int next_client_id = 1; // To generate unique client IDs

const char* http_200_header =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html\r\n"
    "Access-Control-Allow-Origin: *\r\n"
    "Connection: close\r\n"
    "\r\n";

const char* http_json_header =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: application/json\r\n"
    "Access-Control-Allow-Origin: *\r\n"
    "Connection: close\r\n"
    "\r\n";

const char* http_503_header =
    "HTTP/1.1 503 Service Unavailable\r\n"
    "Content-Type: text/html\r\n"
    "Connection: close\r\n"
    "\r\n";

const char* html_page_template =
    "<!DOCTYPE html><html><head><title>Counter</title>"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
    "<style>"
    "body { font-family: Arial, sans-serif; display: flex; flex-direction: column; align-items: center; justify-content: center; height: 100vh; margin: 0; }"
    "h1 { font-size: 48px; margin-bottom: 20px; text-align: center; }"
    "button { font-size: 24px; padding: 10px 20px; margin: 10px; border: none; border-radius: 5px; cursor: pointer; background-color: #007BFF; color: white; min-width: 100px; }"
    "button:hover { background-color: #0056b3; }"
    "@media (max-width: 600px) { "
    "  h1 { font-size: 36px; }"
    "  button { font-size: 20px; padding: 8px 16px; }"
    "} "
    "</style></head><body>"
    "<h1 id=\"counter\">Counter: %d</h1>"
    "<p>Client ID: %d</p>" // Display the client_id here
    "<div>"
    "<button onclick=\"send('/inc')\">+</button>"
    "<button onclick=\"send('/dec')\">-</button>"
    "</div>"
    "<script>"
    "document.addEventListener('DOMContentLoaded', () => {"
    "  fetch('/current')"
    "    .then(response => response.json())"
    "    .then(data => {"
    "      document.getElementById('counter').innerText = 'Counter: ' + data.counter;"
    "    });"
    "});"
    "async function send(path) {"
    "  const response = await fetch(path);"
    "  const data = await response.json();"
    "  document.getElementById('counter').innerText = 'Counter: ' + data.counter;"
    "}"
    "window.onbeforeunload = () => fetch('/disconnect');"
    "</script></body></html>";

const char* html_busy_page =
    "<!DOCTYPE html><html><head><title>Server Busy</title>"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
    "<style>"
    "body { font-family: Arial, sans-serif; display: flex; flex-direction: column; align-items: center; justify-content: center; height: 100vh; margin: 0; background-color: #ffdddd; }"
    "h1 { font-size: 48px; color: #990000; }"
    "p { font-size: 24px; color: #660000; }"
    "</style></head><body>"
    "<h1>Server In Use</h1>"
    "<p>Another user is already connected. Please try again later.</p>"
    "</body></html>";

void send_response(int client_fd, const char* header, const char* body) {
    send(client_fd, header, strlen(header), 0);
    send(client_fd, body, strlen(body), 0);
}

void* handle_client(void* arg) {
    int client_fd = *(int*)arg;
    free(arg);

    struct sockaddr_in client_address;
    socklen_t client_addrlen = sizeof(client_address);

    // Get client info using getpeername
    if (getpeername(client_fd, (struct sockaddr*)&client_address, &client_addrlen) == -1) {
        perror("getpeername failed");
        close(client_fd);
        return NULL;
    }

    // Convert client IP address to human-readable string
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_address.sin_addr, client_ip, sizeof(client_ip));

    printf("Client connected from IP: %s, Port: %d\n", client_ip, ntohs(client_address.sin_port));
    // Retrieve the client port number
    int client_port = ntohs(client_address.sin_port);

    // Print client IP and port to the console
    printf("Client connected: IP = %s, Port = %d\n", client_ip, client_port);

    char buffer[BUF_SIZE] = {0};
    read(client_fd, buffer, sizeof(buffer) - 1);

    pthread_mutex_lock(&lock);

    int client_id = next_client_id++; // Assign unique client_id
    printf("New client connected with ID: %d\n", client_id);

    // Handle disconnect
    if (strstr(buffer, "GET /disconnect")) {
        current_client_ip = NULL; 
        client_connected = 0; // Reset connection flag
        pthread_mutex_unlock(&lock);
        close(client_fd);
        return NULL;
    }

    // Handle increment and decrement actions
    if (strstr(buffer, "GET /inc")) {
        if (client_connected == 1) {
            counter++;
        }
        char json[128];
        snprintf(json, sizeof(json), "{\"counter\": %d}", counter);
        send_response(client_fd, http_json_header, json);
        pthread_mutex_unlock(&lock);
        close(client_fd);
        return NULL;
    }

    if (strstr(buffer, "GET /dec")) {
        if (client_connected == 1) {
            counter--;
        }
        char json[128];
        snprintf(json, sizeof(json), "{\"counter\": %d}", counter);
        send_response(client_fd, http_json_header, json);
        pthread_mutex_unlock(&lock);
        close(client_fd);
        return NULL;
    }

    // Handle current counter status
    if (strstr(buffer, "GET /current")) {
        char json[128];
        snprintf(json, sizeof(json), "{\"counter\": %d}", counter);
        send_response(client_fd, http_json_header, json);
        pthread_mutex_unlock(&lock);
        close(client_fd);
        return NULL;
    }

    printf("Received request: %s\n", client_ip);
    // Handle the main page
    if (strstr(buffer, "GET /")) {
        if (current_client_ip == NULL) {
            client_connected = 1;
            current_client_ip = malloc(INET_ADDRSTRLEN);
            sprintf(current_client_ip, "%s", client_ip);
            char html_page[HTML_SIZE];
            snprintf(html_page, sizeof(html_page), html_page_template, counter, client_id); // Include client_id in the page
            send_response(client_fd, http_200_header, html_page);
        } else if (current_client_ip != NULL && strcmp(current_client_ip, client_ip) == 0) {
            client_connected = 1;
            char html_page[HTML_SIZE];
            snprintf(html_page, sizeof(html_page), html_page_template, counter, client_id); // Include client_id in the page
            send_response(client_fd, http_200_header, html_page);
        } else {
            client_connected = 0;
            send_response(client_fd, http_503_header, html_busy_page);
        }
        pthread_mutex_unlock(&lock);
        close(client_fd);
        return NULL;
    }

    pthread_mutex_unlock(&lock);
    close(client_fd);
    return NULL;
}

int main() {
    int server_fd, *client_fd;
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);

    pthread_mutex_init(&lock, NULL);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("Setsockopt failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address))) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, MAX_CONN_QUEUE)) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Server started on http://localhost:%d\n", PORT);
    printf("Press Ctrl+C to stop the server\n");

    while (1) {
        client_fd = malloc(sizeof(int));
        *client_fd = accept(server_fd, (struct sockaddr*)&address, &addrlen);
        if (*client_fd < 0) {
            perror("Accept failed");
            free(client_fd);
            continue;
        }

        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, client_fd)) {
            perror("Thread creation failed");
            close(*client_fd);
            free(client_fd);
            continue;
        }
        pthread_detach(tid);
    }

    close(server_fd);
    pthread_mutex_destroy(&lock);
    return 0;
}
