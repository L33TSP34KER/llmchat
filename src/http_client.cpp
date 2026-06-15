#include "http_client.h"
#include <cstring>

HttpClient::HttpClient() {
    curl_ = curl_easy_init();
}

HttpClient::~HttpClient() {
    if (headers_) curl_slist_free_all(headers_);
    if (curl_) curl_easy_cleanup(curl_);
}

void HttpClient::set_url(const std::string& url) { url_ = url; }
void HttpClient::set_method(const std::string& method) { method_ = method; }
void HttpClient::set_body(const std::string& body) { body_ = body; }
void HttpClient::set_stream_callback(std::function<bool(const std::string&)> cb) { stream_cb_ = std::move(cb); }

void HttpClient::set_header(const std::string& key, const std::string& value) {
    std::string h = key + ": " + value;
    headers_ = curl_slist_append(headers_, h.c_str());
}

size_t HttpClient::write_cb(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    auto* response = static_cast<HttpResponse*>(userp);
    response->body.append(static_cast<char*>(contents), realsize);
    return realsize;
}

size_t HttpClient::write_stream_cb(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    auto* cb = static_cast<std::function<bool(const std::string&)>*>(userp);
    std::string chunk(static_cast<char*>(contents), realsize);
    if (!(*cb)(chunk)) return 0;
    return realsize;
}

size_t HttpClient::write_stream_with_buffer_cb(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    auto* state = static_cast<StreamState*>(userp);
    std::string chunk(static_cast<char*>(contents), realsize);
    state->buffer += chunk;
    if (state->cb && *state->cb) {
        if (!(*state->cb)(chunk)) return 0;
    }
    return realsize;
}

HttpResponse HttpClient::perform() {
    HttpResponse response;
    if (!curl_) return response;

    curl_easy_setopt(curl_, CURLOPT_URL, url_.c_str());
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYHOST, 0L);

    if (!method_.empty())
        curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, method_.c_str());

    if (!body_.empty()) {
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, body_.c_str());
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, body_.size());
    }

    if (headers_) curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers_);

    CURLcode res = curl_easy_perform(curl_);
    if (res == CURLE_OK)
        curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &response.status_code);

    return response;
}

bool HttpClient::perform_stream() {
    if (!curl_ || !stream_cb_) return false;

    StreamState state;
    state.buffer.clear();
    state.cb = &stream_cb_;

    curl_easy_setopt(curl_, CURLOPT_URL, url_.c_str());
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, write_stream_with_buffer_cb);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &state);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 0L);
    curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl_, CURLOPT_BUFFERSIZE, 256L);

    if (!method_.empty())
        curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, method_.c_str());

    if (!body_.empty()) {
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, body_.c_str());
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, body_.size());
    }

    if (headers_) curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers_);

    CURLcode res = curl_easy_perform(curl_);
    if (res != CURLE_OK) {
        error_body_ = state.buffer.empty() ? curl_easy_strerror(res) : state.buffer;
        return false;
    }

    long http_code = 0;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code >= 400) {
        error_body_ = state.buffer;
        if (error_body_.empty())
            error_body_ = "HTTP " + std::to_string(http_code) + " error";
        return false;
    }
    return true;
}
