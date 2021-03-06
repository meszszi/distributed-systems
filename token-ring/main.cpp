#include <cstdio>
#include <cstdlib>
#include <iostream>

#include <cstring>
#include <queue>
#include <set>
#include <mutex>
#include <condition_variable>
#include <thread>

#include <unistd.h>

#include <sys/socket.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include "chat_protocol.h"


bool operator==(const struct sockaddr_in &a, const struct sockaddr_in &b) {
    return ((a.sin_port == b.sin_port) &&
        (a.sin_addr.s_addr == b.sin_addr.s_addr) &&
        (a.sin_family == b.sin_family));
}



// initial setup parameters
const char* username;
sockaddr_in self_address;
char transport_protocol;



// logging parameters
bool logging = true;
char log_message[128];
int log_data_size;

void format_log_message(const char* message_buffer, int size) {
    std::string msg_type;
    if (message_buffer[0] == MSG_DATA)
        msg_type = "DATA";

    else if (message_buffer[0] == MSG_CONREQ)
        msg_type = "CONNECTION REQUEST";

    else if (message_buffer[0] == MSG_CONFWD)
        msg_type = "CONNECTION FORWARD";

    int token = ((message_buffer[0] == MSG_DATA) || (message_buffer[1] == 1)) ? 1 : 0;

    sprintf(log_message, "%s received message with type %s, token present: %d", username, msg_type.c_str(), token);
    log_data_size = strlen(log_message);
}



// stores the data that is to be forwarded
char forward_buffer[sizeof(data_message)];
int forward_data_size;



// next client pointer
sockaddr_in neighbour_address;
bool connection_established;
std::mutex mt_neighbour_address;

void set_neighbour_address(const struct sockaddr_in &address) {
    std::lock_guard<std::mutex> lock(mt_neighbour_address);
    neighbour_address = address;
}

struct sockaddr_in get_neighbour_address() {
    std::lock_guard<std::mutex> lock(mt_neighbour_address);
    return neighbour_address;
}



// text messages queue
std::queue<struct data_message> message_queue;
std::mutex mt_message_queue;

void push_data_message(struct data_message msg) {
    std::lock_guard<std::mutex> lock(mt_message_queue);
    message_queue.push(msg);
}

int pop_data_message(struct data_message* msg) {
    std::lock_guard<std::mutex> lock(mt_message_queue);
    if (message_queue.empty())
        return -1;

    memcpy(msg, &message_queue.front(), sizeof(struct data_message));
    message_queue.pop();
    return 0;
}



// connection requests queue
std::set<std::pair<in_port_t, in_addr_t> > pending_requests;
std::mutex mt_pending_requests;

bool add_connection_request(const struct sockaddr_in &address) {
    std::lock_guard<std::mutex> lock(mt_pending_requests);
    pending_requests.insert(std::make_pair(address.sin_port, address.sin_addr.s_addr));
}

bool remove_connection_request(const struct sockaddr_in &address) {
    std::lock_guard<std::mutex> lock(mt_pending_requests);
    pending_requests.erase(std::make_pair(address.sin_port, address.sin_addr.s_addr));
}

/** 
 * Removes and stores one request from pending_requests set inside given buffer.
 * If the set is empty, returns -1.
 */
int get_pending_request(struct sockaddr_in* request) {
    std::lock_guard<std::mutex> lock(mt_pending_requests);
    auto request_info = pending_requests.begin();

    if (request_info == pending_requests.end())
        return -1;

    pending_requests.erase(pending_requests.begin());
    request->sin_family = AF_INET;
    request->sin_port = request_info->first;
    request->sin_addr.s_addr = request_info->second;
    return 0;
}



// token parameters
bool has_starting_token;
bool token_is_free;
std::mutex mt_token;

bool get_starting_token() {
    std::lock_guard<std::mutex> lock(mt_token);
    bool result = has_starting_token;
    has_starting_token = false;
    return result;
}



// ==========================================================================================
// Thread methods implementation
// ==========================================================================================

void user_input_thread() {

    char input_buffer[MAX_MSG_SIZE];

    while(true) {

        printf("%s> ", username);
        struct data_message msg;
        msg.type = MSG_DATA;
        msg.token_is_free = 0;
        msg.sender_index = 2;
        msg.buffer[0] = MSG_DATA;
        msg.buffer[1] = 0;
        strcpy(&msg.buffer[2], username);

        int prev_token_len = strlen(username);

        msg.buffer[msg.sender_index + prev_token_len] = '\0';

        std::cin.getline(input_buffer, MAX_MSG_SIZE);
        char *token = std::strtok(input_buffer, " ");

        // first word - destination username
        if (token != NULL) {
            strcpy(&msg.buffer[msg.sender_index + prev_token_len + 1], token);
            msg.receiver_index = msg.sender_index + prev_token_len + 1;
            prev_token_len = strlen(token);
            token = std::strtok(NULL, "\n");
        }

        else {
            std::cout << "message format: dest_username text_message" << std::endl;
            continue;
        }

        // rest of the message
        if (token != NULL) {
            strcpy(&msg.buffer[msg.receiver_index + prev_token_len + 1], token);
            msg.data_index = msg.receiver_index + prev_token_len + 1;
            prev_token_len = strlen(token);
        }

        else {
            std::cout << "message format: dest_username text_message" << std::endl;
            continue;
        }

        msg.total_length = msg.data_index + prev_token_len + 1;
        msg.buffer[msg.total_length - 1] = '\0';
        push_data_message(msg);
        std::cout << "message enqueued" << std::endl;
    }
}

void token_process_thread(Transmission* ts) {

    usleep(TOKEN_SLEEP_TIME);

    if (token_is_free) {

        struct sockaddr_in request;
        int res = get_pending_request(&request);

        // if there is any pending request, the process creates proper connection message
        // and stores it inside forward_buffer
        if (res == 0) {
            struct connection_message msg;
            msg.sender_address = self_address;
            msg.client_address = request;
            msg.neighbour_address = self_address;
            msg.type = MSG_CONFWD;
            msg.with_token = 1;

            forward_data_size = serialize_connection_msg(&msg, forward_buffer);
        }

        // if no request is pending a data message with a token is created and optionally
        // filled with a queued message
        else {
            struct data_message msg;
            int res = pop_data_message(&msg);

            if (res == -1) {
                forward_buffer[0] = MSG_DATA;
                forward_buffer[1] = 1;
                forward_data_size = 2;
            }

            else {
                memcpy(forward_buffer, msg.buffer, msg.total_length);
                forward_data_size = msg.total_length;
            }   
        }
    }

    struct sockaddr_in dest = get_neighbour_address();
    ts->send_bytes(forward_buffer, forward_data_size, &dest);
}

void receive_thread(Transmission* ts) {
    char buffer[MAX_MSG_SIZE];
    struct sockaddr_in sender_address;

    while (true) {
        int msg_size = ts->receive_bytes(buffer, MAX_MSG_SIZE, &sender_address);
        format_log_message(buffer, msg_size);
        ts->log(log_message, log_data_size);
        char type = buffer[0];
        bool token_received = false;
        bool starting_token = get_starting_token();

        if (type == MSG_DATA) {
            token_received = true;
            struct data_message msg;
            deserialize_data_msg(buffer, msg_size, &msg);

            token_is_free = (msg.token_is_free == 1) ? true : false;

            if (!token_is_free) {

                // if the message is addressed to this process
                if (strcmp(&msg.buffer[msg.receiver_index], username) == 0) {
                    std::cout << "message from " << &msg.buffer[msg.sender_index] << ": "
                        << &msg.buffer[msg.data_index] << std::endl;

                    // frees the token since the data has beed successfully delivered
                    token_is_free = true;
                }
                
                // if the message was sent by this process
                else if (strcmp(&msg.buffer[msg.sender_index], username) == 0) {
                    std::cout << "message to " << &msg.buffer[msg.receiver_index] << ": \""
                        << &msg.buffer[msg.data_index] << "\" was not delivered" <<  std::endl;

                    // frees the token since the receiver was not found in the network
                    token_is_free = true; 
                }

                // in any other case the process needs to simply forward the message
                else {
                    memcpy(forward_buffer, &msg.buffer, msg_size);
                    forward_data_size = msg_size;
                }
            }
        }

        else if (type == MSG_CONREQ || type == MSG_CONFWD) {
            struct connection_message msg;
            deserialize_connection_msg(buffer, &msg);

            if (msg.with_token || starting_token) {
                token_received = true;
                remove_connection_request(msg.sender_address);

                // when the process receives connection message with the token and either
                // it is not connected to any client or its neighbour's address is the same
                // as neighbour's address from the message, then the process must set
                // it's neighbour to be the original process that created the connection message
                // (in any case, the token may be freed)
                if ((connection_established && (msg.neighbour_address == get_neighbour_address())) ||
                        (!connection_established)) {

                    set_neighbour_address(msg.client_address);
                    connection_established = true;
                    token_is_free = true;
                }

                // in any other case, the message needs to be forwarded so it reaches the root
                // of the network or the client preceeding the original message creator
                else {
                    msg.type = MSG_CONFWD;
                    msg.sender_address = self_address;
                    forward_data_size = serialize_connection_msg(&msg, forward_buffer);
                    token_is_free = false;
                }
            }

            // if the message doesn't contain the token, then it can only be a connection request
            // to this process that needs to be queued
            else {
                add_connection_request(msg.client_address);
            }
        }

        // after the message is processed, if the token was received,
        // the process needs to run the token processing thread
        if (token_received || starting_token) {
            std::thread th(&token_process_thread, ts);
            th.detach();
        }
    }
}

int main(int argc, char const *argv[]) {
    
    if (argc < 7) {
        std::cout << "usage: ./main login self_ip self_port next_ip next_port ( tcp | udp ) [token]" << std::endl;
        exit(0);
    }

    // initial parameters setup
    username = argv[1];

    const char* self_ip = argv[2];
    int self_port = atoi(argv[3]);
    set_address(self_ip, self_port, &self_address);

    const char* next_ip = argv[4];
    int next_port = atoi(argv[5]);

    transport_protocol = (strcmp(argv[6], "tcp") == 0) ? TRANSPORT_TCP : TRANSPORT_UDP;
    Transmission ts(self_ip, self_port, transport_protocol);

    has_starting_token = (argc > 7) ? true : false;

    if(next_port > 0) {

        struct sockaddr_in addr;
        set_address(next_ip, next_port, &addr);
        set_neighbour_address(addr);

        struct connection_message msg;
        msg.type = MSG_CONREQ;

        msg.with_token = (int) has_starting_token;
        has_starting_token = false;

        msg.client_address = self_address;
        msg.sender_address = self_address;
        msg.neighbour_address = get_neighbour_address();

        char buffer[MAX_MSG_SIZE];
        int size = serialize_connection_msg(&msg, buffer);

        ts.send_bytes(buffer, size, &neighbour_address);
        connection_established = true;
    }

    else {
        connection_established = false;
    }
    
    
    std::thread receiver(&receive_thread, &ts);
    std::thread input(&user_input_thread);

    receiver.join();
    input.join();
}
