#include "UrlEncode.h"
#include <iomanip>
#include <sstream>

namespace openlyrics {

std::string urlEncodeComponent(const std::string& s) {
    std::ostringstream result;
    result.fill('0');

    for (unsigned char byte : s) {
        // RFC 3986 unreserved: A-Za-z0-9-_.~
        if ((byte >= 'A' && byte <= 'Z') ||
            (byte >= 'a' && byte <= 'z') ||
            (byte >= '0' && byte <= '9') ||
            byte == '-' || byte == '_' || byte == '.' || byte == '~') {
            result << byte;
        } else {
            result << '%' << std::hex << std::uppercase << std::setw(2) << static_cast<int>(byte);
        }
    }

    return result.str();
}

}  // namespace openlyrics
