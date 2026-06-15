#pragma once
#include <string>
#include <functional>
#include <curl/curl.h>

struct HttpResponse {
    long status_code = 0;
    std::string body;
};

class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    void set_url(const std::string& url);
    void set_method(const std::string& method);
    void set_header(const std::string& key, const std::string& value);
    void set_body(const std::string& body);
    void set_stream_callback(std::function<bool(const std::string&)> cb);

    HttpResponse perform();
    bool perform_stream();
    std::string get_error_body() const { return error_body_; }

private:
    CURL* curl_;
    std::string url_;
    std::string method_;
    std::string body_;
    struct curl_slist* headers_ = nullptr;
    std::function<bool(const std::string&)> stream_cb_;
    std::string error_body_;

    static size_t write_cb(void* contents, size_t size, size_t nmemb, void* userp);
    static size_t write_stream_cb(void* contents, size_t size, size_t nmemb, void* userp);

    struct StreamState {
        std::string buffer;
        std::function<bool(const std::string&)>* cb;
    };
    static size_t write_stream_with_buffer_cb(void* contents, size_t size, size_t nmemb, void* userp);
};
