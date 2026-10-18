// Consistent API error type and JSON error body. Every error response in the API has the shape
//   {"error": {"code": "...", "message": "...", "request_id": "...", "details": ...}}
#pragma once

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <string>
#include <string_view>

namespace carbon {

class ApiError : public std::runtime_error {
 public:
  ApiError(int status, std::string code, std::string message, nlohmann::json details = nullptr)
      : std::runtime_error(message), status_(status), code_(std::move(code)), details_(std::move(details)) {}

  int status() const noexcept { return status_; }
  const std::string& code() const noexcept { return code_; }
  const nlohmann::json& details() const noexcept { return details_; }

 private:
  int status_;
  std::string code_;
  nlohmann::json details_;
};

ApiError badRequest(std::string message, nlohmann::json details = nullptr);
ApiError unauthorized(std::string message = "Authentication required");
ApiError forbidden(std::string message = "You do not have permission to perform this action");
ApiError notFound(std::string message = "Resource not found");
ApiError conflict(std::string message, nlohmann::json details = nullptr);
ApiError unprocessable(std::string message, nlohmann::json details = nullptr);
ApiError tooManyRequests(std::string message = "Too many requests");
ApiError internalError();  // deliberately generic: never leaks internals
ApiError serviceUnavailable(std::string message = "Service temporarily unavailable");

// Stable machine code / human message for a bare HTTP status (used to normalise errors that the
// web framework generates itself, e.g. 404 for unknown routes or 405).
std::string defaultErrorCode(int status);
std::string defaultErrorMessage(int status);

nlohmann::json makeErrorBody(std::string_view code, std::string_view message, std::string_view requestId,
                             const nlohmann::json& details = nullptr);

}  // namespace carbon
