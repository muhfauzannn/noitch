#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "task_model.h"

namespace noitch {

// Talks to a Moodle site's web service, the documented API its mobile app uses. Approved by the owner
// on 2026-11-12, with the limits in docs/architecture.md (Tasks):
//
// - The password is sent once, to login/token.php, and never kept. The key it returns is kept in
//   Credential Manager, read for each request and dropped after it.
// - The key travels in the request body, never in a URL, and is never logged.
// - The blocking work runs on a short-lived pool thread, which ends by posting the result to the
//   window that asked. Nothing here repeats on its own.
class MoodleSource {
public:
    static constexpr UINT kMessage = WM_APP + 4;

    enum class Kind { SignIn, UseKey, Tasks };

    // lparam of kMessage; the receiver must delete it. wparam is the request's generation.
    struct Result {
        Kind kind = Kind::Tasks;
        MoodleProblem problem = MoodleProblem::Unavailable;
        std::string message;
        std::string token;
        MoodleSiteInfo info;
        std::vector<MoodleTask> tasks;
    };

    // `password` is wiped once sent.
    static void SignIn(HWND notify, uintptr_t generation, const std::string& site, const std::string& username, std::string password);
    static void UseKey(HWND notify, uintptr_t generation, const std::string& site, std::string token);
    // What is still to do, from a week ago on: the list the Dashboard's Timeline shows.
    static void Tasks(HWND notify, uintptr_t generation, const std::string& site, double now);
};

// The site's key in Credential Manager, as a generic credential for this user on this PC.
namespace task_token {

bool Save(const std::string& site, const std::string& token);
std::string Read(const std::string& site);
void Delete(const std::string& site);

}  // namespace task_token

}  // namespace noitch
