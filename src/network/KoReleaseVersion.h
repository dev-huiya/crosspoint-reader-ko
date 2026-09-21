#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace ko_release {

struct Version {
  uint32_t major = 0;
  uint32_t minor = 0;
  uint32_t patch = 0;
  uint32_t koRevision = 0;
  uint8_t channel = 0;  // development=0, plain=1, KO release=2
  bool valid = false;
};

inline Version parse(const char* text) {
  Version version;
  if (!text) return version;
  if (*text == 'v') ++text;
  char* end = nullptr;
  auto readPart = [&](uint32_t& out) {
    if (*text < '0' || *text > '9') return false;
    const unsigned long value = strtoul(text, &end, 10);
    if (end == text || value > 999999) return false;
    out = static_cast<uint32_t>(value);
    text = end;
    return true;
  };
  if (!readPart(version.major) || *text++ != '.' || !readPart(version.minor) || *text++ != '.' ||
      !readPart(version.patch))
    return version;
  if (*text == '\0') {
    version.channel = 1;
  } else if (strncmp(text, "-ko.", 4) == 0) {
    text += 4;
    if (!readPart(version.koRevision)) return version;
    if (*text == '\0') {
      version.channel = 2;
    } else if (*text == '-' || *text == '+') {
      // "1.6.0-ko.1-x4pro", "1.6.0-ko.1-dev-branch-sha", "1.6.0-ko.1-rc+hash":
      // a development or board-suffixed build of that KO version, which every
      // published KO release of the same base version supersedes.
      version.channel = 0;
    } else {
      return version;
    }
  } else if (*text == '-') {
    version.channel = 0;
  } else {
    return version;
  }
  version.valid = true;
  return version;
}

inline bool isNewer(const char* latest, const char* current) {
  const Version next = parse(latest);
  const Version installed = parse(current);
  if (!next.valid || !installed.valid) return false;
  if (next.major != installed.major) return next.major > installed.major;
  if (next.minor != installed.minor) return next.minor > installed.minor;
  if (next.patch != installed.patch) return next.patch > installed.patch;
  if (next.channel != installed.channel) return next.channel > installed.channel;
  return next.channel == 2 && next.koRevision > installed.koRevision;
}

}  // namespace ko_release
