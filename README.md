## Microservice to consume GitHub GraphQL API

---

## Description

HTTP microservice written in C that queries the **GitHub GraphQL API**, fetching the metadata of profiles or repositories.
Use case example via curl:

```bash
# Repository metadata
curl -X POST http://localhost:8080/api/assets/github-graphql \
  -H "Content-Type: application/json" \
  -d '{"target_type": "repository", "identifier": "torvalds/linux"}'

# Profile metadata
curl -X POST http://localhost:8080/api/assets/github-graphql \
  -H "Content-Type: application/json" \
  -d '{"target_type": "profile", "identifier": "torvalds"}'


```

---

## Contract with GitHub GraphQL API

GitHub GraphQL (v4) exposes a single endpoint. The client sends a declarative query indicating exactly what data it needs, and the server responds with a JSON with that same structure.

Official documentation: [https://docs.github.com/en/graphql](https://docs.github.com/en/graphql).

### Endpoint and Authentication

```http
POST [https://api.github.com/graphql](https://api.github.com/graphql)
Authorization: bearer <GITHUB_TOKEN>
Content-Type: application/json


```

**Quota:** GraphQL calculates the cost by resolved nodes. The limit is 5,000 points per hour.

---

### Scenario A: Profile Request (User)

When the microservice receives the order to process a profile, it sends the following GraphQL Query to GitHub:

```graphql
query {
  user(login: "<IDENTIFIER>") {
    login name bio avatarUrl company location createdAt
    followers { totalCount } following { totalCount }
    repositories(privacy: PUBLIC) { totalCount }
    gists { totalCount } starredRepositories { totalCount }
  }
}


```

**Response**

The C microservice parses the GitHub response, flattens the structures, and generates this JSON that is saved in the database:

```json
{
  "login": "torvalds",
  "name": "Linus Torvalds",
  "bio": "Creator of Linux",
  "avatar_url": "[https://avatars.githubusercontent.com/u/1024025?v=4](https://avatars.githubusercontent.com/u/1024025?v=4)",
  "location": "Portland, OR",
  "stats": {
    "followers": 204551,
    "public_repos": 7,
    "gists": 0
  },
  "created_at": "2011-09-01T15:00:00Z"
}


```

---

### Scenario B: Repository Request (Repository)

When the microservice processes a repository, the following query is executed, which includes statistics and the breakdown of used languages:

```graphql
query {
  repository(owner: "<OWNER>", name: "<NAME>") {
    name owner { login } description primaryLanguage { name }
    languages(first: 10, orderBy: {field: SIZE, direction: DESC}) {
      edges { size node { name } }
    }
    homepageUrl visibility diskUsage
    isArchived isFork
    stargazerCount forkCount watchers { totalCount }
    issues(states: OPEN) { totalCount } pullRequests(states: OPEN) { totalCount }
    releases { totalCount } repositoryTopics(first: 10) { nodes { topic { name } } }
    licenseInfo { spdxId }
    createdAt updatedAt pushedAt
  }
}


```

**Response**

Just like with the profile, the microservice extracts the nested nodes (such as `languages.edges` or `repositoryTopics.nodes`) to build a flat JSON:

```json
{
  "name": "linux",
  "owner": "torvalds",
  "description": "Linux kernel source tree",
  "primary_language": "C",
  "languages": [
    { "name": "C", "bytes": 9482713 },
    { "name": "Assembly", "bytes": 245102 }
  ],
  "urls": {
    "homepage": null
  },
  "visibility": "PUBLIC",
  "disk_usage_kb": 4350212,
  "flags": [], 
  "topics": ["linux", "kernel", "operating-system"],
  "license_spdx": "GPL-2.0",
  "stats": {
    "stargazers": 170940,
    "forks": 52187,
    "watchers": 8200,
    "open_issues": 340,
    "open_prs": 112,
    "releases": 5
  },
  "timestamps": {
    "created_at": "2011-09-04T22:48:12Z",
    "updated_at": "2024-05-18T14:32:00Z"
  }
}


```

---

## Database Mapping

The JSON processed by the microservice is saved in the `assets` table:

| Column | Origin |
| --- | --- |
| `asset_uri` | `github:profile:<login>` or `github:repo:<owner>/<name>` (`UNIQUE`) |
| `title` | `name` (profile) or `name` (repository) |
| `entity` | `"profile"` or `"repository"` |
| `provider` | `"github"` |
| `meta_payload` | Structured JSON by the C microservice |
| `created_at` | Managed by DB |
| `updated_at` | Managed by DB |

---

## Dependencies

This microservice is written in C and relies on the following dynamically linked libraries:

* `libmicrohttpd`: For the HTTP server.
* `libcurl`: For HTTP requests to the GitHub API.
* `cJSON`: For parsing and building JSON payloads.
* `sqlite3`: For the local database.

To install these dependencies on a Debian-based Linux distribution, run:

```bash
sudo apt update
sudo apt install libmicrohttpd-dev libcurl4-openssl-dev libcjson-dev libsqlite3-dev build-essential


```

---

## Configuration

The application reads configuration from a `.env` file located in the root directory. You must create this file before starting the server.

Create a file named `.env` and configure the following variables:

```ini
# Required: Your GitHub Personal Access Token
GITHUB_TOKEN=ghp_your_token_here

# Optional: The port the HTTP server will bind to (Default: 8080)
PORT=8080

# Optional: Path to the SQLite database file (Default: ./db.sqlite3)
DB_PATH=./db.sqlite3


```

---

## Build and Execution

This project uses a `Makefile` to simplify the compilation process. To build the microservice, simply run the following command in the root directory:

```bash
make


```

This will compile the source files located in the `src/` directory (`src/main.c`, `src/db.c`, `src/api.c`, `src/github.c`) and link the necessary libraries (`-lmicrohttpd -lcurl -lsqlite3 -lcjson`), generating an executable named `github_worker`.

Once compiled, start the server by executing the binary:

```bash
./github_worker


```

You should see an output confirming the initialization of the database and the server listening on your configured port. The application handles `SIGINT` and `SIGTERM` signals, so you can safely stop the server gracefully at any time by pressing `Ctrl+C`.