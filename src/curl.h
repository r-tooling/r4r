#ifndef CURL_H
#define CURL_H

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
    explicit CURLMultipleTransfer(size_t parallel)
        : cm_(curl_multi_init()), parallel_(parallel) {
        CURLGlobalInitializer::instance();

        if (!cm_) {
            throw std::runtime_error("curl_multi_init failed");
        }
    }

    ~CURLMultipleTransfer() { curl_multi_cleanup(cm_); }

    void add(T key, std::string const& url) { pending_.emplace(key, url); }

    std::unordered_map<T, CURLResult> run() {
        std::unordered_map<T, CURLResult> results;

        while (requests_.size() < parallel_ && !pending_.empty()) {
            auto [key, url] = pending_.front();
            pending_.pop();
            add_transfer(key, url);
        }

        int is_running{1};

        while (is_running && !requests_.empty()) {
            curl_multi_perform(cm_, &is_running);

            CURLMsg* msg{nullptr};
            int msgs_left{0};

            while ((msg = curl_multi_info_read(cm_, &msgs_left))) {
                Request* req{};
                curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &req);

                if (msg->msg == CURLMSG_DONE) {
                    long http_code = 0;
                    curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE,
                                      &http_code);

                    results.emplace(
                        req->key,
                        HttpResult{static_cast<int>(http_code), req->response});
                } else {
                    results.emplace(req->key, std::string{curl_easy_strerror(
                                                  msg->data.result)});
                }

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

  private:
    struct Request {
        T key;
        std::string response;
        CURL* handle{};

        ~Request() {
            if (handle) {
                curl_easy_cleanup(handle);
            }
        }
    };

    static size_t write_callback(char* ptr, size_t size, size_t nmemb,
                                 std::string* data) {
        data->append(ptr, size * nmemb);
        return size * nmemb;
    }

    void add_transfer(T key, std::string const& url) {
        auto req = std::make_unique<Request>(key, "", curl_easy_init());
        if (!req->handle) {
            throw std::runtime_error("curl_easy_init failed");
        }

        curl_easy_setopt(req->handle, CURLOPT_URL, url.c_str());
        curl_easy_setopt(req->handle, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(req->handle, CURLOPT_WRITEDATA, &req->response);
        curl_easy_setopt(req->handle, CURLOPT_PRIVATE, req.get());
        curl_easy_setopt(req->handle, CURLOPT_FOLLOWLOCATION, 1L);

        curl_multi_add_handle(cm_, req->handle);

        requests_.emplace(key, std::move(req));
    }

    CURLM* cm_;
    size_t parallel_;
    std::queue<std::pair<T, std::string>> pending_;
    std::map<T, std::unique_ptr<Request>> requests_;
};

#endif // CURL_H
