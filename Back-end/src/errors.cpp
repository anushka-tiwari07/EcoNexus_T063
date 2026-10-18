#include "carbon/errors.hpp"

namespace carbon {

ApiError badRequest(std::string m, nlohmann::json d) { return {400, "bad_request", std::move(m), std::move(d)}; }
ApiError unauthorized(std::string m) { return {401, "unauthorized", std::move(m)}; }
ApiError forbidden(std::string m) { return {403, "forbidden", std::move(m)}; }
ApiError notFound(std::string m) { return {404, "not_found", std::move(m)}; }
ApiError conflict(std::string m, nlohmann::json d) { return {409, "conflict", std::move(m), std::move(d)}; }
ApiError unprocessable(std::string m, nlohmann::json d) { return {422, "validation_failed", std::move(m), std::move(d)}; }
ApiError tooManyRequests(std::string m) { return {429, "rate_limited", std::move(m)}; }
ApiError internalError() { return {500, "internal_error", "An internal error occurred"}; }
ApiError serviceUnavailable(std::string m) { return {503, "service_unavailable", std::move(m)}; }

std::string defaultErrorCode(int status) {
  switch (status) {
    case 400: return "bad_request";
    case 401: return "unauthorized";
    case 403: return "forbidden";
    case 404: return "not_found";
    case 405: return "method_not_allowed";
    case 409: return "conflict";
    case 413: return "payload_too_large";
    case 415: return "unsupported_media_type";
    case 422: return "validation_failed";
    case 429: return "rate_limited";
    case 503: return "service_unavailable";
    default: return status >= 500 ? "internal_error" : "error";
  }
}

std::string defaultErrorMessage(int status) {
  switch (status) {
    case 400: return "The request could not be understood";
    case 401: return "Authentication required";
    case 403: return "Forbidden";
    case 404: return "Resource not found";
    case 405: return "Method not allowed for this resource";
    case 413: return "Request body is too large";
    case 415: return "Unsupported media type";
    case 429: return "Too many requests";
    case 503: return "Service temporarily unavailable";
    default: return status >= 500 ? "An internal error occurred" : "Request failed";
  }
}

nlohmann::json makeErrorBody(std::string_view code, std::string_view message, std::string_view requestId,
                             const nlohmann::json& details) {
  nlohmann::json err = {{"code", code}, {"message", message}};
  if (!requestId.empty()) err["request_id"] = requestId;
  if (!details.is_null()) err["details"] = details;
  return {{"error", std::move(err)}};
}

}  // namespace carbon
