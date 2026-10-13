#pragma once

#include <string>

namespace sx1255::web {

// Self-contained HTML/CSS/JS dashboard (no external frameworks).  Served at
// the HTTP root by HttpStatusServer.
const std::string& dashboardHtml();

}  // namespace sx1255::web
