#ifndef CURL_H
#define CURL_H

#include "logger.h"
#include "util.h"
#include <curl/curl.h>
#include <curl/multi.h>
#include <map>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <variant>

class CURLGlobalInitializer {
public:
    static CURLGlobalInitializer& instance() {
        static CURLGlobalInitializer instance;
        return instance;
    }

private:
    CURLGlobalInitializer() { curl_global_init(CURL_GLOBAL_ALL); }
    ~CURLGlobalInitializer() { curl_global_cleanup(); }
};

struct HttpResult {
    int http_code;
    std::string message;
};

using CURLResult = std::variant<std::string, HttpResult>;

template <typename T>
class CURLMultipleTransfer {
public:
    explicit CURLMultipleTransfer(size_t parallel);
    CURLMultipleTransfer(CURLMultipleTransfer const&) = delete;
    CURLMultipleTransfer& operator=(CURLMultipleTransfer const&) = delete;
    ~CURLMultipleTransfer();

    void add(T key, std::string const& url);
    std::unordered_map<T, CURLResult> run();

private:
    struct Request {
        T key;
        std::string response;
    };

    CURLResult process_curl_message(CURLMsg* msg, Request* req);
    static size_t write_callback(char* ptr, size_t size, size_t nmemb,
                                 std::string* data);
    void add_transfer(T key, std::string const& url);

    CURLM* cm_;
    size_t parallel_;
    std::queue<std::pair<T, std::string>> pending_;
    std::map<T, std::unique_ptr<Request>> requests_;
};

template <typename T>
inline CURLMultipleTransfer<T>::CURLMultipleTransfer(size_t parallel)
    : cm_(curl_multi_init()), parallel_(parallel) {
    CURLGlobalInitializer::instance();

    if (!cm_) {
        throw std::runtime_error("curl_multi_init failed");
    }
}

template <typename T>
inline CURLMultipleTransfer<T>::~CURLMultipleTransfer() {
    curl_multi_cleanup(cm_);
}

template <typename T>
inline void CURLMultipleTransfer<T>::add(T key, std::string const& url) {
    pending_.emplace(key, url);
}

template <typename T>
inline std::unordered_map<T, CURLResult> CURLMultipleTransfer<T>::run() {
    std::unordered_map<T, CURLResult> results;

    while (requests_.size() < parallel_ && !pending_.empty()) {
        auto [key, url] = pending_.front();
        pending_.pop();
        add_transfer(key, url);
    }

    int is_running{1};

    LOG(TRACE) << "Starting CURL batch, size: " << requests_.size()
               << " parallel: " << parallel_;

    while (is_running && !requests_.empty()) {
        curl_multi_perform(cm_, &is_running);

        CURLMsg* msg{nullptr};
        int msgs_left{0};

        while ((msg = curl_multi_info_read(cm_, &msgs_left))) {
            Request* req{};
            curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &req);

            results.emplace(req->key, process_curl_message(msg, req));

            curl_multi_remove_handle(cm_, msg->easy_handle);
            requests_.erase(req->key);
        }

        if (!pending_.empty()) {
            auto [key, url] = pending_.front();
            pending_.pop();
            add_transfer(key, url);
        }

        if (!requests_.empty()) {
            curl_multi_wait(cm_, nullptr, 0, 1000, nullptr);
        }
    }

    return results;
}

template <typename T>
inline CURLResult CURLMultipleTransfer<T>::process_curl_message(CURLMsg* msg,
    Request* req) {
    if (msg->msg != CURLMSG_DONE) {
        char const* error = curl_easy_strerror(msg->data.result);

        char* url{};
        curl_easy_getinfo(msg->easy_handle, CURLINFO_EFFECTIVE_URL, &url);
        LOG(WARN) << "Failed CURL task: " << url << " in " << error;

        return std::string{error};
    }

    long http_code = 0;
    curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &http_code);

    if (Logger::get().is_enabled(TRACE)) {
        char* url{};
        curl_easy_getinfo(msg->easy_handle, CURLINFO_EFFECTIVE_URL, &url);

        curl_off_t total{};
        curl_easy_getinfo(msg->easy_handle, CURLINFO_TOTAL_TIME_T, &total);

        std::chrono::microseconds total_chrono{total};

        LOG(TRACE) << "Finished CURL task: " << url << " in "
                   << format_elapsed_time(total_chrono);
    }

    return HttpResult{static_cast<int>(http_code), req->response};
}

template <typename T>
inline size_t CURLMultipleTransfer<T>::write_callback(char* ptr, size_t size,
    size_t nmemb,
    std::string* data) {
    data->append(ptr, size * nmemb);
    return size * nmemb;
}

template <typename T>
inline void CURLMultipleTransfer<T>::add_transfer(T key,
                                                  std::string const& url) {
    LOG(TRACE) << "Adding CURL task: " << url;

    auto req = std::make_unique<Request>(key, "");
    CURL* handle = curl_easy_init();
    if (!handle) {
        throw std::runtime_error("curl_easy_init failed");
    }

    curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &req->response);
    curl_easy_setopt(handle, CURLOPT_PRIVATE, req.get());
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);

    curl_multi_add_handle(cm_, handle);

    requests_.emplace(key, std::move(req));
}

#endif // CURL_H
