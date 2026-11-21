#pragma once

#include <cstddef>

class WebConfig;

// Line commands on the USB console:
//   sb info                      -> sb:info {json}
//   sb <id> <path>[?query]       -> sb:<id> <code> <body>
class SerialConsole {
   public:
    explicit SerialConsole(WebConfig& web) : web_(web) {
    }

    void        poll();
    static void printInfo();

   private:
    void handle(char* line);

    WebConfig& web_;
    char       buf_[1536];  // fits a full /save form
    size_t     len_      = 0;
    bool       overflow_ = false;
};
