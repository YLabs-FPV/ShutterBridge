// Transport-neutral request/reply for the config API, shared by HTTP and the serial console
#pragma once

#include <Arduino.h>

class ApiArgs {
   public:
    virtual ~ApiArgs()                          = default;
    virtual bool   has(const String& key) const = 0;
    virtual String get(const String& key) const = 0;  // "" when absent
};

struct ApiReply {
    int         code = 200;
    const char* type = "text/plain";
    String      body;

    static ApiReply text(int code, const char* body) {
        return {code, "text/plain", body};
    }
    static ApiReply json(int code, String body) {
        return {code, "application/json", std::move(body)};
    }
};

// Arguments from an application/x-www-form-urlencoded string ("a=1&b=x%20y")
class QueryArgs : public ApiArgs {
   public:
    explicit QueryArgs(const char* query) : q_(query ? query : "") {
    }
    bool   has(const String& key) const override;
    String get(const String& key) const override;

   private:
    bool        find(const String& key, String* out) const;
    const char* q_;
};
