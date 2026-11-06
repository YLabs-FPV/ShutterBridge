#pragma once

#include <cstddef>

class SerialConsole {
   public:
    void        poll();
    static void printInfo();

   private:
    void handle(const char* line);

    char   buf_[48];
    size_t len_      = 0;
    bool   overflow_ = false;
};
