#pragma once

#include "DataManager.hpp"
#include "DeleteHandler.hpp"
#include "DownloadHandler.hpp"
#include "ListHandler.hpp"
#include "MultipartUploadHandler.hpp"
#include "UploadHandler.hpp"

#include <event.h>
#include <event2/buffer.h>
#include <event2/http.h>
#include <evhttp.h>

#include <chrono>
#include <memory>
#include <string>

namespace storage
{
    class Service
    {
    public:
        explicit Service(DataManager &data_manager)
            : server_port_(Config::GetInstance()->GetServerPort()),
              server_ip_(Config::GetInstance()->GetServerIp()),
              download_prefix_(Config::GetInstance()->GetDownloadPrefix()),
              data_manager_(data_manager)
        {
#ifdef DEBUG_LOG
            mylog::GetLogger("asynclogger")->Debug("Service construct, configured ip:%s, bind:0.0.0.0, port:%d", server_ip_.c_str(), server_port_);
#endif
        }

        bool RunModule()
        {
            std::unique_ptr<event_base, decltype(&event_base_free)> base(event_base_new(), event_base_free);
            if (base == nullptr)
            {
                mylog::GetLogger("asynclogger")->Fatal("event_base_new err!");
                return false;
            }

            std::unique_ptr<evhttp, decltype(&evhttp_free)> httpd(evhttp_new(base.get()), evhttp_free);
            if (httpd == nullptr)
            {
                mylog::GetLogger("asynclogger")->Fatal("evhttp_new err!");
                return false;
            }

            if (evhttp_bind_socket(httpd.get(), "0.0.0.0", server_port_) != 0)
            {
                mylog::GetLogger("asynclogger")->Fatal("evhttp_bind_socket failed!");
                return false;
            }
            evhttp_set_gencb(httpd.get(), GenHandler, this);

#ifdef DEBUG_LOG
            mylog::GetLogger("asynclogger")->Debug("event_base_dispatch");
#endif
            if (event_base_dispatch(base.get()) == -1)
            {
                mylog::GetLogger("asynclogger")->Debug("event_base_dispatch err");
            }
            return true;
        }

    private:
        static void GenHandler(struct evhttp_request *req, void *arg)
        {
            Service *service = static_cast<Service *>(arg);
            if (service == nullptr)
            {
                evhttp_send_reply(req, HTTP_INTERNAL, "server error", NULL);
                return;
            }
            service->Dispatch(req);
        }

        void Dispatch(struct evhttp_request *req)
        {
            auto start = std::chrono::steady_clock::now();
            std::string method = MethodName(evhttp_request_get_command(req));
            std::string client = ClientAddress(req);
            size_t request_bytes = 0;
            struct evbuffer *input = evhttp_request_get_input_buffer(req);
            if (input != nullptr)
                request_bytes = evbuffer_get_length(input);

            std::string path = evhttp_uri_get_path(evhttp_request_get_evhttp_uri(req));
            std::string decoded_path;
            if (!UrlDecode(path, &decoded_path))
            {
                evhttp_send_reply(req, HTTP_BADREQUEST, "bad url", NULL);
                LogAccess(method, path, HTTP_BADREQUEST, client, request_bytes, 0, start, "bad_url");
                return;
            }
            path = decoded_path;
            HttpUtil::HttpResult result;

            if (path.find("/download/") == 0)
            {
                result = DownloadHandler::Handle(req, data_manager_);
            }
            else if (path == "/upload/init")
            {
                result = MultipartUploadHandler::Init(req, data_manager_);
            }
            else if (path == "/upload/chunk")
            {
                result = MultipartUploadHandler::Chunk(req);
            }
            else if (path == "/upload/status")
            {
                result = MultipartUploadHandler::Status(req);
            }
            else if (path == "/upload/complete")
            {
                result = MultipartUploadHandler::Complete(req, data_manager_);
            }
            else if (path == "/upload/abort")
            {
                result = MultipartUploadHandler::Abort(req);
            }
            else if (path.find("/delete/") == 0)
            {
                result = DeleteHandler::Handle(req, data_manager_);
            }
            else if (path == "/upload")
            {
                result = UploadHandler::Handle(req, data_manager_);
            }
            else if (path == "/")
            {
                result = ListHandler::Handle(req, data_manager_);
            }
            else
            {
                evhttp_send_reply(req, HTTP_NOTFOUND, "Not Found", NULL);
                result = HttpUtil::HttpResult(HTTP_NOTFOUND, "not_found");
            }
            LogAccess(method, path, result.status, client, request_bytes, result.bytes, start, result.reason);
        }

        static const char *MethodName(enum evhttp_cmd_type method)
        {
            switch (method)
            {
            case EVHTTP_REQ_GET:
                return "GET";
            case EVHTTP_REQ_POST:
                return "POST";
            case EVHTTP_REQ_HEAD:
                return "HEAD";
            case EVHTTP_REQ_PUT:
                return "PUT";
            case EVHTTP_REQ_DELETE:
                return "DELETE";
            case EVHTTP_REQ_OPTIONS:
                return "OPTIONS";
            case EVHTTP_REQ_TRACE:
                return "TRACE";
            case EVHTTP_REQ_CONNECT:
                return "CONNECT";
            case EVHTTP_REQ_PATCH:
                return "PATCH";
            default:
                return "UNKNOWN";
            }
        }

        static std::string ClientAddress(struct evhttp_request *req)
        {
            struct evhttp_connection *connection = evhttp_request_get_connection(req);
            if (connection == nullptr)
                return "-";

            char *host = nullptr;
            ev_uint16_t port = 0;
            evhttp_connection_get_peer(connection, &host, &port);
            if (host == nullptr)
                return "-";
            return std::string(host) + ":" + std::to_string(port);
        }

        static void LogAccess(const std::string &method,
                              const std::string &path,
                              int status,
                              const std::string &client,
                              size_t request_bytes,
                              size_t response_bytes,
                              std::chrono::steady_clock::time_point start,
                              const std::string &reason)
        {
            auto end = std::chrono::steady_clock::now();
            long long cost_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            mylog::GetLogger("access_logger")->Info("method=%s uri=%s status=%d client=%s req_bytes=%zu resp_bytes=%zu cost_ms=%lld reason=%s",
                                                    method.c_str(), path.c_str(), status, client.c_str(),
                                                    request_bytes, response_bytes, cost_ms, reason.c_str());
        }

    private:
        uint16_t server_port_;
        std::string server_ip_;
        std::string download_prefix_;
        DataManager &data_manager_;
    };
}
