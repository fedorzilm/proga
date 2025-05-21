// src/client/client_processing_logic.h 
#ifndef CLIENT_PROCESSING_LOGIC_H
#define CLIENT_PROCESSING_LOGIC_H

#include "tcp_socket.h" // Или forward-declare, если достаточно только объявления TCPSocket
#include <string>
#include <iostream> // Для std::ostream

// Объявление функции, реализация которой находится в client_main.cpp
bool process_single_request_to_server(
    TCPSocket& socket, // Принимает TCPSocket по ссылке
    const std::string& query,
    std::ostream& out_stream_for_response,
    const std::string& client_id_log_prefix,
    int receive_timeout_ms
);

#endif // CLIENT_PROCESSING_LOGIC_H
