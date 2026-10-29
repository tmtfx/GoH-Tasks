#pragma once
#include <string>
#include <vector>

struct Task {
    std::string id;
    std::string title;
    std::string notes;
    std::string status;   // "needsAction" or "completed"
    std::string due;      // RFC 3339
    std::string updated;
    std::string selfLink;
    bool starred = false; // local only

    bool isCompleted() const { return status == "completed"; }
};

struct TaskList {
    std::string id;
    std::string title;
    std::string updated;
};
