#pragma once
#include <string>

namespace openlyrics {

// RFC 3986 component encoding: unreserved chars (A-Za-z0-9-_.~) pass through;
// every other byte becomes %XX (uppercase hex).
std::string urlEncodeComponent(const std::string& s);

}  // namespace openlyrics
