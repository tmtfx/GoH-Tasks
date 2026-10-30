#pragma once
#include "Task.h"
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <Locker.h>

class GoogleAuth;

class GoogleTasksAPI {
public:
    explicit GoogleTasksAPI(GoogleAuth* auth);

    bool getTaskLists(std::vector<TaskList>& lists);
    bool getTasks(const std::string& listId, std::vector<Task>& tasks);
    bool createTask(const std::string& listId, const std::string& title,
                    const std::string& notes, const std::string& due, Task& created);
    bool updateTask(const std::string& listId, const Task& task);
    bool deleteTask(const std::string& listId, const std::string& taskId);
    bool completeTask(const std::string& listId, const std::string& taskId, bool complete);
    bool createTaskList(const std::string& title, TaskList& created);
    bool deleteTaskList(const std::string& listId);
    bool moveTask(const std::string& listId, const std::string& taskId,
                  const std::string& parent, const std::string& previous);

    // Persist starred state to local file
    void setTaskStarred(const std::string& taskId, bool starred);

    const std::string& lastError() const { return fLastError; }

private:
    std::string httpGet(const std::string& url);
    std::string httpPost(const std::string& url, const std::string& body);
    std::string httpPatch(const std::string& url, const std::string& body);
    std::string httpDelete(const std::string& url);
    std::string doRequest(const std::string& method, const std::string& url,
                          const std::string& body);

    void loadStarred();
    void saveStarred();
    bool isStarred(const std::string& taskId) const;
    void setStarred(const std::string& taskId, bool starred);

    GoogleAuth* fAuth;
    std::string fLastError;
    std::string fSettingsDir;
    std::map<std::string, bool> fStarred;
    mutable BLocker fStarredLock;
};
