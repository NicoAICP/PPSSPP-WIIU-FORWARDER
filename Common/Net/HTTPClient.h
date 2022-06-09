#pragma once

#include <functional>
#include <memory>
#include <thread>
#include <cstdint>

#include "Common/Net/Resolve.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#endif

#include "Common/Buffer.h"

namespace net {

class Connection {
public:
	Connection();
	virtual ~Connection();

	// Inits the sockaddr_in.
	bool Resolve(const char *host, int port, DNSType type = DNSType::ANY);

	bool Connect(int maxTries = 2, double timeout = 20.0f, bool *cancelConnect = nullptr);
	void Disconnect();

	// Only to be used for bring-up and debugging.
	uintptr_t sock() const { return sock_; }

protected:
	// Store the remote host here, so we can send it along through HTTP/1.1 requests.
	// TODO: Move to http::client?
	std::string host_;
	int port_;

	addrinfo *resolved_;

private:
	uintptr_t sock_;

};

}	// namespace net

namespace http {

bool GetHeaderValue(const std::vector<std::string> &responseHeaders, const std::string &header, std::string *value);

class Client : public net::Connection {
public:
	Client();
	~Client();

	// Return value is the HTTP return code. 200 means OK. < 0 means some local error.
	int GET(const char *resource, Buffer *output, float *progress = nullptr, bool *cancelled = nullptr);
	int GET(const char *resource, Buffer *output, std::vector<std::string> &responseHeaders, float *progress = nullptr, bool *cancelled = nullptr);

	// Return value is the HTTP return code.
	int POST(const char *resource, const std::string &data, const std::string &mime, Buffer *output, float *progress = nullptr);
	int POST(const char *resource, const std::string &data, Buffer *output, float *progress = nullptr);

	// HEAD, PUT, DELETE aren't implemented yet, but can be done with SendRequest.

	int SendRequest(const char *method, const char *resource, const char *otherHeaders = nullptr, float *progress = nullptr, bool *cancelled = nullptr);
	int SendRequestWithData(const char *method, const char *resource, const std::string &data, const char *otherHeaders = nullptr, float *progress = nullptr, bool *cancelled = nullptr);
	int ReadResponseHeaders(Buffer *readbuf, std::vector<std::string> &responseHeaders, float *progress = nullptr, bool *cancelled = nullptr);
	// If your response contains a response, you must read it.
	int ReadResponseEntity(Buffer *readbuf, const std::vector<std::string> &responseHeaders, Buffer *output, float *progress = nullptr, bool *cancelled = nullptr);

	void SetDataTimeout(double t) {
		dataTimeout_ = t;
	}

protected:
	const char *userAgent_;
	const char *httpVersion_;
	double dataTimeout_ = -1.0;
};

// Not particularly efficient, but hey - it's a background download, that's pretty cool :P
class Download {
public:
	Download(const std::string &url, const std::string &outfile);
	~Download();

	void Start();

	void Join();

	// Returns 1.0 when done. That one value can be compared exactly - or just use Done().
	float Progress() const { return progress_; }

	bool Done() const { return completed_; }
	bool Failed() const { return failed_; }

	// NOTE! The value of ResultCode is INVALID until Done() returns true.
	int ResultCode() const { return resultCode_; }

	std::string url() const { return url_; }
	std::string outfile() const { return outfile_; }

	// If not downloading to a file, access this to get the result.
	Buffer &buffer() { return buffer_; }
	const Buffer &buffer() const { return buffer_; }

	void Cancel() {
		cancelled_ = true;
	}

	bool IsCancelled() const {
		return cancelled_;
	}

	// NOTE: Callbacks are NOT executed until RunCallback is called. This is so that
	// the call will end up on the thread that calls g_DownloadManager.Update().
	void SetCallback(std::function<void(Download &)> callback) {
		callback_ = callback;
	}
	void RunCallback() {
		if (callback_) {
			callback_(*this);
		}
	}

	// Just metadata. Convenient for download managers, for example, if set,
	// Downloader::GetCurrentProgress won't return it in the results.
	bool IsHidden() const { return hidden_; }
	void SetHidden(bool hidden) { hidden_ = hidden; }

private:
	void Do();  // Actually does the download. Runs on thread.
	int PerformGET(const std::string &url);
	std::string RedirectLocation(const std::string &baseUrl);
	void SetFailed(int code);
	float progress_ = 0.0f;
	Buffer buffer_;
	std::vector<std::string> responseHeaders_;
	std::string url_;
	std::string outfile_;
	std::thread thread_;
	int resultCode_ = 0;
	bool completed_ = false;
	bool failed_ = false;
	bool cancelled_ = false;
	bool hidden_ = false;
	bool joined_ = false;
	std::function<void(Download &)> callback_;
};

using std::shared_ptr;

class Downloader {
public:
	~Downloader() {
		CancelAll();
	}

	std::shared_ptr<Download> StartDownload(const std::string &url, const std::string &outfile);

	std::shared_ptr<Download> StartDownloadWithCallback(
		const std::string &url,
		const std::string &outfile,
		std::function<void(Download &)> callback);

	// Drops finished downloads from the list.
	void Update();
	void CancelAll();

	std::vector<float> GetCurrentProgress();

private:
	std::vector<std::shared_ptr<Download>> downloads_;
};

}	// http
