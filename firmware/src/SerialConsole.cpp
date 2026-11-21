#include "SerialConsole.h"

#include <Arduino.h>

#include <cctype>
#include <cstring>

#include "Board.h"
#include "config.h"
#include "config/Settings.h"
#include "net/WebConfig.h"

void SerialConsole::printInfo() {
    Serial.printf("sb:info {\"fw\":\"%s\",\"schema\":%u,\"board\":\"%s\",\"variant\":\"%s\"}\n",
                  FW_VERSION, (unsigned)Settings::SCHEMA, BOARD_ID, boardId());
}

void SerialConsole::poll() {
    while (Serial.available() > 0) {
        const int c = Serial.read();
        if (c < 0)
            break;
        if (c == '\r' || c == '\n') {
            if (!overflow_ && len_ > 0) {
                buf_[len_] = '\0';
                handle(buf_);
            }
            len_      = 0;
            overflow_ = false;
        } else if (len_ < sizeof(buf_) - 1) {
            buf_[len_++] = (char)c;
        } else {
            overflow_ = true;
        }
    }
}

void SerialConsole::handle(char* line) {
    if (strcmp(line, "sb info") == 0) {
        printInfo();
        return;
    }
    if (strncmp(line, "sb ", 3) != 0 || !isdigit((unsigned char)line[3]))
        return;

    char*               p  = line + 3;
    const unsigned long id = strtoul(p, &p, 10);
    if (*p != ' ' || p[1] != '/')
        return;
    char* path  = p + 1;
    char* query = strchr(path, '?');
    if (query)
        *query++ = '\0';

    const ApiReply out = web_.call(path, QueryArgs(query));

    // One write per reply so log lines from the other core cannot split it
    String msg;
    msg.reserve(out.body.length() + 24);
    msg += "sb:";
    msg += id;
    msg += ' ';
    msg += out.code;
    msg += ' ';
    msg += out.body;
    msg += '\n';
    Serial.write((const uint8_t*)msg.c_str(), msg.length());
}
