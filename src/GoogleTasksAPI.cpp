#include "GoogleTasksAPI.h"
#include "GoogleAuth.h"
#include "JsonValue.h"

#include <FindDirectory.h>
#include <Path.h>
#include <Directory.h>
#include <Catalog.h>

#include <curl/curl.h>

#include <fstream>
#include <sstream>
#include <map>
#include <cstring>
#include <cstdio>
#include <Autolock.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "GoogleTasksAPI"

static const std::string kBaseUrl = "https://tasks.googleapis.com/tasks/v1";

namespace {

size_t curlWriteCb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    std::string* s = static_cast<std::string*>(userdata);
    s->append(ptr, size * nmemb);
    return size * nmemb;
}

} // anonymous namespace

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
    BAutolock lock(fStarredLock);
    fStarred.clear();
    std::string path = fSettingsDir + "/starred.json";
    std::ifstream f(path);
    if (!f.is_open()) return;
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    try {
        JsonValue j = JsonValue::parse(content);
        if (j.isObject()) {
            for (const std::string& key : j.keys()) {
                if (j[key].isBool() && j[key].boolean())
                    fStarred[key] = true;
            }
        }
    } catch (...) {}
}

void GoogleTasksAPI::saveStarred()
{
    BAutolock lock(fStarredLock);
    std::string path = fSettingsDir + "/starred.json";
    std::ofstream f(path);
    if (!f.is_open()) return;
    f << "{";
    bool first = true;
    for (auto& kv : fStarred) {
        if (kv.second) {
            if (!first) f << ",";
            f << "\"" << JsonValue::escape(kv.first) << "\":true";
            first = false;
        }
    }
    f << "}";
}

bool GoogleTasksAPI::isStarred(const std::string& taskId) const
{
    BAutolock lock(fStarredLock);
    auto it = fStarred.find(taskId);
    return it != fStarred.end() && it->second;
}

void GoogleTasksAPI::setStarred(const std::string& taskId, bool starred)
{
    {
        BAutolock lock(fStarredLock);
        if (starred)
            fStarred[taskId] = true;
        else
            fStarred.erase(taskId);
    }
    saveStarred();
}

// ---- HTTP helpers ----

std::string GoogleTasksAPI::doRequest(const std::string& method,
                                      const std::string& url,
                                      const std::string& body)
{
    std::string token = fAuth->getAccessToken();
    if (token.empty()) {
        fLastError = B_TRANSLATE("Not authenticated");
        return "";
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        fLastError = B_TRANSLATE("curl_easy_init failed");
        return "";
    }

    std::string response;
    std::string authHeader = "Authorization: Bearer " + token;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, authHeader.c_str());
    headers = curl_slist_append(headers, "Accept: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    if (method == "POST") {
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
    } else if (method == "PATCH") {
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
    } else if (method == "PUT") {
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
    } else if (method == "DELETE") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    }
    // GET is default

    CURLcode res = curl_easy_perform(curl);

    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    //fprintf(stderr, "[GoH-Tasks] %s %s -> curl=%d http=%ld bytes=%zu\n",
    //        method.c_str(), url.c_str(), (int)res, httpCode, response.size());
    //if (!response.empty())
    //    fprintf(stderr, "[GoH-Tasks] body: %s\n", response.c_str());

    if (res != CURLE_OK) {
        fLastError = std::string(B_TRANSLATE("CURL error: ")) + curl_easy_strerror(res);
        return "";
    }
    if (httpCode >= 400) {
        fLastError = "HTTP " + std::to_string(httpCode) + ": " + response;
        return "";
    }
    fLastError.clear();
    return response;
}

std::string GoogleTasksAPI::httpGet(const std::string& url) {
    return doRequest("GET", url, "");
}

std::string GoogleTasksAPI::httpPost(const std::string& url, const std::string& body) {
    return doRequest("POST", url, body);
}

std::string GoogleTasksAPI::httpPatch(const std::string& url, const std::string& body) {
    return doRequest("PATCH", url, body);
}

std::string GoogleTasksAPI::httpDelete(const std::string& url) {
    return doRequest("DELETE", url, "");
}

// ---- API methods ----

bool GoogleTasksAPI::getTaskLists(std::vector<TaskList>& lists)
{
    lists.clear();
    std::string url = kBaseUrl + "/users/@me/lists?maxResults=100";
    std::string response = httpGet(url);
    if (response.empty()) return false;

    try {
        JsonValue j = JsonValue::parse(response);
        const JsonValue& items = j["items"];
        for (size_t i = 0; i < items.size(); ++i) {
            TaskList tl;
            tl.id      = items[i]["id"].str();
            tl.title   = items[i]["title"].str();
            tl.updated = items[i]["updated"].str();
            if (!tl.id.empty())
                lists.push_back(tl);
        }
        return true;
    } catch (...) {
        fLastError = B_TRANSLATE("Failed to parse task lists response");
        return false;
    }
}

bool GoogleTasksAPI::getTasks(const std::string& listId, std::vector<Task>& tasks)
{
    tasks.clear();
    std::string url = kBaseUrl + "/lists/" + listId +
                      "/tasks?showHidden=true&showCompleted=true&maxResults=100";
    std::string response = httpGet(url);
    if (response.empty()) return false;

    // Refresh starred state
    loadStarred();

    try {
        JsonValue j = JsonValue::parse(response);
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
            if (!t.id.empty())
                tasks.push_back(t);
        }
        return true;
    } catch (...) {
        fLastError = B_TRANSLATE("Failed to parse tasks response");
        return false;
    }
}

bool GoogleTasksAPI::createTask(const std::string& listId, const std::string& title,
                                const std::string& notes, const std::string& due,
                                Task& created)
{
    std::string body = "{";
    body += "\"title\":\"" + JsonValue::escape(title) + "\"";
    if (!notes.empty())
        body += ",\"notes\":\"" + JsonValue::escape(notes) + "\"";
    if (!due.empty())
        body += ",\"due\":\"" + JsonValue::escape(due) + "\"";
    body += "}";

    std::string url = kBaseUrl + "/lists/" + listId + "/tasks";
    std::string response = httpPost(url, body);
    if (response.empty()) return false;

    try {
        JsonValue j = JsonValue::parse(response);
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
        fLastError = B_TRANSLATE("Failed to parse create task response");
        return false;
    }
}

bool GoogleTasksAPI::updateTask(const std::string& listId, const Task& task)
{
    std::string body = "{";
    body += "\"id\":\"" + JsonValue::escape(task.id) + "\"";
    body += ",\"title\":\"" + JsonValue::escape(task.title) + "\"";
    if (!task.notes.empty())
        body += ",\"notes\":\"" + JsonValue::escape(task.notes) + "\"";
    body += ",\"status\":\"" + JsonValue::escape(task.status) + "\"";
    if (!task.due.empty())
        body += ",\"due\":\"" + JsonValue::escape(task.due) + "\"";
    body += "}";

    std::string url = kBaseUrl + "/lists/" + listId + "/tasks/" + task.id;
    std::string response = doRequest("PUT", url, body);
    if (response.empty()) return false;

    // Update starred state
    setStarred(task.id, task.starred);
    return true;
}

bool GoogleTasksAPI::deleteTask(const std::string& listId, const std::string& taskId)
{
    std::string url = kBaseUrl + "/lists/" + listId + "/tasks/" + taskId;
    std::string response = httpDelete(url);
    // DELETE returns empty body on success (204)
    if (!fLastError.empty()) return false;
    setStarred(taskId, false);
    return true;
}

bool GoogleTasksAPI::completeTask(const std::string& listId, const std::string& taskId,
                                   bool complete)
{
    std::string body;
    if (complete) {
        body = "{\"status\":\"completed\"}";
    } else {
        body = "{\"status\":\"needsAction\",\"completed\":null}";
    }
    std::string url = kBaseUrl + "/lists/" + listId + "/tasks/" + taskId;
    std::string response = httpPatch(url, body);
    return !response.empty() || fLastError.empty();
}

bool GoogleTasksAPI::createTaskList(const std::string& title, TaskList& created)
{
    std::string body = "{\"title\":\"" + JsonValue::escape(title) + "\"}";
    std::string url  = kBaseUrl + "/users/@me/lists";
    std::string response = httpPost(url, body);
    if (response.empty()) return false;

    try {
        JsonValue j = JsonValue::parse(response);
        created.id      = j["id"].str();
        created.title   = j["title"].str();
        created.updated = j["updated"].str();
        return !created.id.empty();
    } catch (...) {
        fLastError = B_TRANSLATE("Failed to parse create list response");
        return false;
    }
}

bool GoogleTasksAPI::deleteTaskList(const std::string& listId)
{
    std::string url = kBaseUrl + "/users/@me/lists/" + listId;
    httpDelete(url);
    return fLastError.empty();
}

void GoogleTasksAPI::setTaskStarred(const std::string& taskId, bool starred)
{
    setStarred(taskId, starred);
}

bool GoogleTasksAPI::moveTask(const std::string& listId, const std::string& taskId,
                               const std::string& parent, const std::string& previous)
{
    std::string url = kBaseUrl + "/lists/" + listId + "/tasks/" + taskId + "/move?";
    if (!parent.empty())   url += "parent="   + parent   + "&";
    if (!previous.empty()) url += "previous=" + previous + "&";
    // Remove trailing & or ?
    if (!url.empty() && (url.back() == '&' || url.back() == '?'))
        url.pop_back();

    std::string response = httpPost(url, "");
    return !response.empty() || fLastError.empty();
}
