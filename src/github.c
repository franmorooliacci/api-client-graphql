#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include "github.h"
#include "db.h"

// Helper structure to store the CURL response
struct MemoryStruct {
    char *memory;
    size_t size;
};

// Callback that libcurl calls every time it receives a data chunk
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) {
        printf("Not enough memory (realloc failed)\n");
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

// Helper function to safely add strings handling nulls
static void add_string_or_null(cJSON *obj, const char *key, cJSON *item) {
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        cJSON_AddStringToObject(obj, key, item->valuestring);
    } else {
        cJSON_AddNullToObject(obj, key);
    }
}

// Extracts the numeric value (totalCount) from a nested node
static double get_nested_count(cJSON *parent, const char *node_name) {
    cJSON *node = cJSON_GetObjectItem(parent, node_name);
    if (node) {
        cJSON *total = cJSON_GetObjectItem(node, "totalCount");
        if (cJSON_IsNumber(total)) return total->valuedouble;
    }
    return 0;
}

// Converts the raw GitHub response to the structured contract format
static char* flatten_github_json(const char* raw_json, const char* target_type) {
    cJSON *root = cJSON_Parse(raw_json);
    if (!root) return NULL;
    
    // --- GRAPHQL ERROR INTERCEPTOR ---
    cJSON *errors = cJSON_GetObjectItem(root, "errors");
    if (errors && cJSON_IsArray(errors) && cJSON_GetArraySize(errors) > 0) {
        cJSON *first_error = cJSON_GetArrayItem(errors, 0);
        cJSON *message = cJSON_GetObjectItem(first_error, "message");
        
        if (cJSON_IsString(message)) {
            fprintf(stderr, "\nError: %s\n\n", message->valuestring);
        } else {
            // Fallback if the error structure is unusual
            char *err_str = cJSON_PrintUnformatted(errors);
            fprintf(stderr, "\nError: %s\n\n", err_str);
            free(err_str);
        }
        
        cJSON_Delete(root);
        return NULL; // Aborts and prevents saving empty data
    }

    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (!data || cJSON_IsNull(data)) { 
        cJSON_Delete(root); 
        return NULL; 
    }

    cJSON *new_root = cJSON_CreateObject();

    if (strcmp(target_type, "profile") == 0) {
        cJSON *user = cJSON_GetObjectItem(data, "user");

        // Strict block for null profiles
        if (!user || cJSON_IsNull(user)) {
            fprintf(stderr, "\nError: GitHub returned a null user. Check your token scopes or username.\n\n");
            cJSON_Delete(root);
            cJSON_Delete(new_root);
            return NULL; 
        }

        add_string_or_null(new_root, "login", cJSON_GetObjectItem(user, "login"));
        add_string_or_null(new_root, "name", cJSON_GetObjectItem(user, "name"));
        add_string_or_null(new_root, "bio", cJSON_GetObjectItem(user, "bio"));
        add_string_or_null(new_root, "avatar_url", cJSON_GetObjectItem(user, "avatarUrl"));
        add_string_or_null(new_root, "location", cJSON_GetObjectItem(user, "location"));
        
        cJSON *stats = cJSON_CreateObject();
        cJSON_AddNumberToObject(stats, "followers", get_nested_count(user, "followers"));
        cJSON_AddNumberToObject(stats, "public_repos", get_nested_count(user, "repositories"));
        cJSON_AddNumberToObject(stats, "gists", get_nested_count(user, "gists"));
        cJSON_AddItemToObject(new_root, "stats", stats);
        
        add_string_or_null(new_root, "created_at", cJSON_GetObjectItem(user, "createdAt"));

    } else if (strcmp(target_type, "repository") == 0) {
        cJSON *repo = cJSON_GetObjectItem(data, "repository");
       
        // Strict block for null repositories
        if (!repo || cJSON_IsNull(repo)) {
            fprintf(stderr, "\nError: GitHub returned a null repository. Check your token scopes or repository name.\n\n");
            cJSON_Delete(root);
            cJSON_Delete(new_root);
            return NULL; 
        }

        add_string_or_null(new_root, "name", cJSON_GetObjectItem(repo, "name"));
        
        cJSON *owner = cJSON_GetObjectItem(repo, "owner");
        if (owner) add_string_or_null(new_root, "owner", cJSON_GetObjectItem(owner, "login"));
        
        add_string_or_null(new_root, "description", cJSON_GetObjectItem(repo, "description"));
        
        cJSON *primaryLang = cJSON_GetObjectItem(repo, "primaryLanguage");
        if (primaryLang) add_string_or_null(new_root, "primary_language", cJSON_GetObjectItem(primaryLang, "name"));
        else cJSON_AddNullToObject(new_root, "primary_language");

        cJSON *langs_array = cJSON_CreateArray();
        cJSON *languages_node = cJSON_GetObjectItem(repo, "languages");
        if (languages_node) {
            cJSON *edges = cJSON_GetObjectItem(languages_node, "edges");
            cJSON *edge;
            cJSON_ArrayForEach(edge, edges) {
                cJSON *lang_obj = cJSON_CreateObject();
                cJSON *size = cJSON_GetObjectItem(edge, "size");
                cJSON *node = cJSON_GetObjectItem(edge, "node");
                cJSON *name = node ? cJSON_GetObjectItem(node, "name") : NULL;
                if (name && size) {
                    cJSON_AddStringToObject(lang_obj, "name", name->valuestring);
                    cJSON_AddNumberToObject(lang_obj, "bytes", size->valuedouble);
                    cJSON_AddItemToArray(langs_array, lang_obj);
                } else { cJSON_Delete(lang_obj); }
            }
        }
        cJSON_AddItemToObject(new_root, "languages", langs_array);

        cJSON *urls = cJSON_CreateObject();
        add_string_or_null(urls, "homepage", cJSON_GetObjectItem(repo, "homepageUrl"));
        cJSON_AddItemToObject(new_root, "urls", urls);

        add_string_or_null(new_root, "visibility", cJSON_GetObjectItem(repo, "visibility"));
        
        cJSON *diskUsage = cJSON_GetObjectItem(repo, "diskUsage");
        if (cJSON_IsNumber(diskUsage)) cJSON_AddNumberToObject(new_root, "disk_usage_kb", diskUsage->valuedouble);

        cJSON *flags_array = cJSON_CreateArray();
        if (cJSON_IsTrue(cJSON_GetObjectItem(repo, "isArchived"))) cJSON_AddItemToArray(flags_array, cJSON_CreateString("isArchived"));
        if (cJSON_IsTrue(cJSON_GetObjectItem(repo, "isFork"))) cJSON_AddItemToArray(flags_array, cJSON_CreateString("isFork"));
        cJSON_AddItemToObject(new_root, "flags", flags_array);

        cJSON *topics_array = cJSON_CreateArray();
        cJSON *topics_node = cJSON_GetObjectItem(repo, "repositoryTopics");
        if (topics_node) {
            cJSON *nodes = cJSON_GetObjectItem(topics_node, "nodes");
            cJSON *node;
            cJSON_ArrayForEach(node, nodes) {
                cJSON *topic = cJSON_GetObjectItem(node, "topic");
                cJSON *name = topic ? cJSON_GetObjectItem(topic, "name") : NULL;
                if (name && cJSON_IsString(name)) cJSON_AddItemToArray(topics_array, cJSON_CreateString(name->valuestring));
            }
        }
        cJSON_AddItemToObject(new_root, "topics", topics_array);

        cJSON *license = cJSON_GetObjectItem(repo, "licenseInfo");
        if (license) add_string_or_null(new_root, "license_spdx", cJSON_GetObjectItem(license, "spdxId"));
        else cJSON_AddNullToObject(new_root, "license_spdx");

        cJSON *stats = cJSON_CreateObject();
        cJSON *stargazers = cJSON_GetObjectItem(repo, "stargazerCount");
        if (stargazers) cJSON_AddNumberToObject(stats, "stargazers", stargazers->valuedouble);
        cJSON *forks = cJSON_GetObjectItem(repo, "forkCount");
        if (forks) cJSON_AddNumberToObject(stats, "forks", forks->valuedouble);
        cJSON_AddNumberToObject(stats, "watchers", get_nested_count(repo, "watchers"));
        cJSON_AddNumberToObject(stats, "open_issues", get_nested_count(repo, "issues"));
        cJSON_AddNumberToObject(stats, "open_prs", get_nested_count(repo, "pullRequests"));
        cJSON_AddNumberToObject(stats, "releases", get_nested_count(repo, "releases"));
        cJSON_AddItemToObject(new_root, "stats", stats);

        cJSON *timestamps = cJSON_CreateObject();
        add_string_or_null(timestamps, "created_at", cJSON_GetObjectItem(repo, "createdAt"));
        add_string_or_null(timestamps, "updated_at", cJSON_GetObjectItem(repo, "updatedAt"));
        cJSON_AddItemToObject(new_root, "timestamps", timestamps);
    }

    char *flat_json_str = cJSON_PrintUnformatted(new_root);
    cJSON_Delete(root);
    cJSON_Delete(new_root);
    return flat_json_str;
}

// Generates the GraphQL query depending on the target type
static char* build_graphql_query(const char *target_type, const char *identifier) {
    cJSON *root = cJSON_CreateObject();
    char query_buffer[2048];

    if (strcmp(target_type, "profile") == 0) {
        snprintf(query_buffer, sizeof(query_buffer), 
            "query { user(login: \"%s\") { login name bio avatarUrl company location createdAt "
            "followers { totalCount } following { totalCount } "
            "repositories(privacy: PUBLIC) { totalCount } "
            "gists { totalCount } starredRepositories { totalCount } } }", 
            identifier);
    } else if (strcmp(target_type, "repository") == 0) {
        char owner[256] = {0};
        char name[256] = {0};
        sscanf(identifier, "%[^/]/%s", owner, name);

        snprintf(query_buffer, sizeof(query_buffer), 
            "query { repository(owner: \"%s\", name: \"%s\") { "
            "name owner { login } description primaryLanguage { name } "
            "languages(first: 10, orderBy: {field: SIZE, direction: DESC}) { edges { size node { name } } } "
            "homepageUrl visibility diskUsage isArchived isFork "
            "stargazerCount forkCount watchers { totalCount } "
            "issues(states: OPEN) { totalCount } pullRequests(states: OPEN) { totalCount } "
            "releases { totalCount } repositoryTopics(first: 10) { nodes { topic { name } } } "
            "licenseInfo { spdxId } createdAt updatedAt pushedAt } }", 
            owner, name);
    } else {
        cJSON_Delete(root);
        return NULL;
    }

    cJSON_AddStringToObject(root, "query", query_buffer);
    char *json_string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    return json_string;
}

int fetch_and_store_github_data(const char *target_type, const char *identifier, const char *token, const char *db_path) {
    CURL *curl;
    CURLcode res;
    struct MemoryStruct chunk;
    chunk.memory = malloc(1);
    chunk.size = 0;

    curl = curl_easy_init();
    if (!curl) {
        free(chunk.memory);
        return 1;
    }

    struct curl_slist *headers = NULL;
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Authorization: bearer %s", token);
    
    headers = curl_slist_append(headers, auth_header);
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "User-Agent: api-client-github-graphql/1.0");

    char *post_data = build_graphql_query(target_type, identifier);
    if (!post_data) {
        printf("Unsupported target type: %s\n", target_type);
        curl_easy_cleanup(curl);
        free(chunk.memory);
        return 1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, "https://api.github.com/graphql");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

    res = curl_easy_perform(curl);

    int final_status = 1;

    if (res != CURLE_OK) {
        fprintf(stderr, "Network error: curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
    } else {
        char asset_uri[256];
        snprintf(asset_uri, sizeof(asset_uri), "github:%s:%s", target_type, identifier);
       
        // Transforms the raw JSON to the flattened JSON according to the contract
        char *flat_payload = flatten_github_json(chunk.memory, target_type);
        
        if (flat_payload) {
            db_upsert_asset(db_path, asset_uri, identifier, target_type, flat_payload);
            printf("Structured data inserted for %s\n", asset_uri);
            free(flat_payload);
            final_status = 0;
        } else {
            fprintf(stderr, "Operation aborted due to an error in the GitHub response.\n");
        }
    }

    // Memory cleanup
    cJSON_free(post_data);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    free(chunk.memory);

    return final_status;
}
