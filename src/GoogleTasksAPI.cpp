#include "GoogleTasksAPI.h"
#include "GoogleAuth.h"
#include "HaikuHttp.h"
#include "JsonValue.h"

#include <Directory.h>
#include <FindDirectory.h>
#include <Path.h>

#include <fstream>
#include <map>
#include <sstream>

static const std::string kBaseUrl = "https://tasks.googleapis.com/tasks/v1";

GoogleTasksAPI::GoogleTasksAPI(GoogleAuth* auth)
    : fAuth(auth)
{
    BPath settingsPath;
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &settingsPath) == B_OK) {
        settingsPath.Append("GoH-Tasks");
        fSettingsDir = settingsPath.Path();
    }
    loadStarred();
}

// ---- Starred state (local JSON file) ----

void GoogleTasksAPI::loadStarred()
{
    fStarred.clear();
    std::string path = fSettingsDir + "/starred.json";
    std::ifstream f(path);
    if (!f.is_open()) return;
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    try {
        JsonValue j = JsonValue::parse(content);
        if (!j.isObject()) return;
        // Scan serialized form for "key":true pairs
        std::string s = j.serialize();
        size_t pos = 0;
        while ((pos = s.find('"', pos)) != std::string::npos) {
            size_t ks = pos + 1;
            size_t ke = s.find('"', ks);
            if (ke == std::string::npos) break;
            std::string key = s.substr(ks, ke - ks);
            size_t col = s.find(':', ke);
            if (col == std::string::npos) break;
            size_t vs = col + 1;
            while (vs < s.size() && s[vs] == ' ') ++vs;
            if (s.substr(vs, 4) == "true")
                fStarred[key] = true;
            pos = ke + 1;
        }
    } catch (...) {}
}

void GoogleTasksAPI::saveStarred()
{
    std::string path = fSettingsDir + "/starred.json";
    std::ofstream f(path);
    if (!f.is_open()) return;
    f << '{';
    bool first = true;
    for (auto& [id, val] : fStarred) {
        if (!val) continue;
        if (!first) f << ',';
        f << '"' << JsonValue::escape(id) << "\":true";
        first = false;
    }
    f << '}';
}

bool GoogleTasksAPI::isStarred(const std::string& taskId) const
{
    auto it = fStarred.find(taskId);
    return it != fStarred.end() && it->second;
}

void GoogleTasksAPI::setTaskStarred(const std::string& taskId, bool starred)
{
    if (starred)
        fStarred[taskId] = true;
    else
        fStarred.erase(taskId);
    saveStarred();
}

// ---- HTTP helpers (native Haiku Network Kit via HaikuHttp) ----

std::vector<std::pair<std::string,std::string>> GoogleTasksAPI::authHeaders(bool json) const
{
    std::string token = fAuth->getAccessToken();
    std::vector<std::pair<std::string,std::string>> h;
    h.push_back({"Authorization", "Bearer " + token});
    if (json)
        h.push_back({"Content-Type", "application/json"});
    return h;
}

std::string GoogleTasksAPI::doRequest(const std::string& method,
                                      const std::string& url,
                                      const std::string& body)
{
    bool hasBody = !body.empty();
    auto resp = HaikuHttp::request(method, url, body, authHeaders(hasBody));

    if (!resp.ok()) {
        fLastError = "HTTP " + std::to_string(resp.statusCode) + ": " + resp.body;
        return "";
    }
    fLastError.clear();
    return resp.body;
}

// ---- API methods ----

bool GoogleTasksAPI::getTaskLists(std::vector<TaskList>& lists)
{
    lists.clear();
    std::string url = kBaseUrl + "/users/@me/lists?maxResults=100";
    std::string r = doRequest("GET", url, "");
    if (r.empty()) return false;

    try {
        JsonValue j = JsonValue::parse(r);
        const JsonValue& items = j["items"];
        for (size_t i = 0; i < items.size(); ++i) {
            TaskList tl;
            tl.id      = items[i]["id"].str();
            tl.title   = items[i]["title"].str();
            tl.updated = items[i]["updated"].str();
            if (!tl.id.empty()) lists.push_back(tl);
        }
        return true;
    } catch (...) {
        fLastError = "Errore nel parsing della lista";
        return false;
    }
}

bool GoogleTasksAPI::getTasks(const std::string& listId, std::vector<Task>& tasks)
{
    tasks.clear();
    std::string url = kBaseUrl + "/lists/" + listId +
                      "/tasks?showHidden=true&showCompleted=true&maxResults=100";
    std::string r = doRequest("GET", url, "");
    if (r.empty()) return false;

    loadStarred();

    try {
        JsonValue j = JsonValue::parse(r);
        const JsonValue& items = j["items"];
        for (size_t i = 0; i < items.size(); ++i) {
            Task t;
            t.id       = items[i]["id"].str();
            t.title    = items[i]["title"].str();
            t.notes    = items[i]["notes"].str();
            t.status   = items[i]["status"].str();
            t.due      = items[i]["due"].str();
            t.updated  = items[i]["updated"].str();
            t.selfLink = items[i]["selfLink"].str();
            t.starred  = isStarred(t.id);
            if (t.status.empty()) t.status = "needsAction";
            if (!t.id.empty()) tasks.push_back(t);
        }
        return true;
    } catch (...) {
        fLastError = "Errore nel parsing dei task";
        return false;
    }
}

bool GoogleTasksAPI::createTask(const std::string& listId, const std::string& title,
                                const std::string& notes, const std::string& due,
                                Task& created)
{
    std::string body = "{\"title\":\"" + JsonValue::escape(title) + "\"";
    if (!notes.empty()) body += ",\"notes\":\"" + JsonValue::escape(notes) + "\"";
    if (!due.empty())   body += ",\"due\":\""   + JsonValue::escape(due)   + "\"";
    body += '}';

    std::string url = kBaseUrl + "/lists/" + listId + "/tasks";
    std::string r = doRequest("POST", url, body);
    if (r.empty()) return false;

    try {
        JsonValue j = JsonValue::parse(r);
        created.id       = j["id"].str();
        created.title    = j["title"].str();
        created.notes    = j["notes"].str();
        created.status   = j["status"].str();
        created.due      = j["due"].str();
        created.updated  = j["updated"].str();
        created.selfLink = j["selfLink"].str();
        created.starred  = false;
        if (created.status.empty()) created.status = "needsAction";
        return !created.id.empty();
    } catch (...) {
        fLastError = "Errore nella creazione del task";
        return false;
    }
}

bool GoogleTasksAPI::updateTask(const std::string& listId, const Task& task)
{
    std::string body = "{\"id\":\""     + JsonValue::escape(task.id)     + "\""
                     + ",\"title\":\""  + JsonValue::escape(task.title)  + "\""
                     + ",\"status\":\"" + JsonValue::escape(task.status) + "\"";
    if (!task.notes.empty()) body += ",\"notes\":\"" + JsonValue::escape(task.notes) + "\"";
    if (!task.due.empty())   body += ",\"due\":\""   + JsonValue::escape(task.due)   + "\"";
    body += '}';

    std::string url = kBaseUrl + "/lists/" + listId + "/tasks/" + task.id;
    std::string r = doRequest("PUT", url, body);
    if (r.empty()) return false;

    setTaskStarred(task.id, task.starred);
    return true;
}

bool GoogleTasksAPI::deleteTask(const std::string& listId, const std::string& taskId)
{
    std::string url = kBaseUrl + "/lists/" + listId + "/tasks/" + taskId;

    // DELETE returns 204 No Content on success — body is empty but ok() is true
    auto resp = HaikuHttp::request("DELETE", url, "", authHeaders(false));
    if (resp.statusCode != 204 && resp.statusCode != 200 && !resp.ok()) {
        fLastError = "HTTP " + std::to_string(resp.statusCode);
        return false;
    }
    fLastError.clear();
    setTaskStarred(taskId, false);
    return true;
}

bool GoogleTasksAPI::completeTask(const std::string& listId, const std::string& taskId,
                                   bool complete)
{
    std::string body = complete
        ? "{\"status\":\"completed\"}"
        : "{\"status\":\"needsAction\",\"completed\":null}";
    std::string url = kBaseUrl + "/lists/" + listId + "/tasks/" + taskId;
    std::string r = doRequest("PATCH", url, body);
    return !r.empty() || fLastError.empty();
}

bool GoogleTasksAPI::createTaskList(const std::string& title, TaskList& created)
{
    std::string body = "{\"title\":\"" + JsonValue::escape(title) + "\"}";
    std::string url  = kBaseUrl + "/users/@me/lists";
    std::string r    = doRequest("POST", url, body);
    if (r.empty()) return false;

    try {
        JsonValue j = JsonValue::parse(r);
        created.id      = j["id"].str();
        created.title   = j["title"].str();
        created.updated = j["updated"].str();
        return !created.id.empty();
    } catch (...) {
        fLastError = "Errore nella creazione della lista";
        return false;
    }
}

bool GoogleTasksAPI::deleteTaskList(const std::string& listId)
{
    std::string url  = kBaseUrl + "/users/@me/lists/" + listId;
    auto resp = HaikuHttp::request("DELETE", url, "", authHeaders(false));
    if (resp.statusCode != 204 && resp.statusCode != 200 && !resp.ok()) {
        fLastError = "HTTP " + std::to_string(resp.statusCode);
        return false;
    }
    fLastError.clear();
    return true;
}

bool GoogleTasksAPI::moveTask(const std::string& listId, const std::string& taskId,
                               const std::string& parent, const std::string& previous)
{
    std::string url = kBaseUrl + "/lists/" + listId + "/tasks/" + taskId + "/move?";
    if (!parent.empty())   url += "parent="   + parent   + "&";
    if (!previous.empty()) url += "previous=" + previous + "&";
    if (!url.empty() && (url.back() == '&' || url.back() == '?'))
        url.pop_back();

    std::string r = doRequest("POST", url, "");
    return !r.empty() || fLastError.empty();
}
