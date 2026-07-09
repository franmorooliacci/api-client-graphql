## Microservicio para consumir GitHub GraphQL API

---

## Descripción

Microservicio HTTP escrito en C que consulta **GitHub GraphQL API**, obtiene la metadata de perfiles o repositorios. 
Ejemplo de caso de uso mediante curl:

```bash
# Metadata de un repositorio
curl -X POST http://localhost:8080/api/assets/github-graphql \
  -H "Content-Type: application/json" \
  -d '{"target_type": "repository", "identifier": "torvalds/linux"}'

# Metadata de un perfil
curl -X POST http://localhost:8080/api/assets/github-graphql \
  -H "Content-Type: application/json" \
  -d '{"target_type": "profile", "identifier": "torvalds"}'

```

---

## Contrato con GitHub GraphQL API

GitHub GraphQL (v4) expone un único endpoint. El cliente envía una consulta declarativa (Query) indicando exactamente qué datos necesita, y el servidor responde con un JSON con esa misma estructura.

Documentación oficial: [https://docs.github.com/en/graphql](https://docs.github.com/en/graphql).

### Endpoint y Autenticación

```http
POST [https://api.github.com/graphql](https://api.github.com/graphql)
Authorization: bearer <GITHUB_TOKEN>
Content-Type: application/json

```

**Cuota:** GraphQL calcula el costo por nodos resueltos. El límite es de 5.000 puntos por hora.

---

### Escenario A: Request de un Perfil (User)

Cuando el microservicio recibe la orden de procesar un perfil, envia la siguiente GraphQL Query a GitHub:

```graphql
query {
  user(login: "<IDENTIFICADOR>") {
    login name bio avatarUrl company location createdAt
    followers { totalCount } following { totalCount }
    repositories(privacy: PUBLIC) { totalCount }
    gists { totalCount } starredRepositories { totalCount }
  }
}

```

**Response**
El microservicio en C parsea la respuesta de GitHub, aplana las estructuras y genera este JSON que se guarda en la base de datos:

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

### Escenario B: Request de un Repositorio (Repository)

Cuando el microservicio procesa un repositorio, se realiza la siguiente consulta, que incluye estadísticas y el desglose de lenguajes usados:

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
Al igual que con el perfil, el microservicio extrae los nodos anidados (como `languages.edges` o `repositoryTopics.nodes`) para armar un JSON plano:

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

## Mapeo a la base de datos

El JSON procesado por el microservicio se guarda en la tabla `assets`:

| Columna | Origen |
| --- | --- |
| `asset_uri` | `github:profile:<login>` o `github:repo:<owner>/<name>` (`UNIQUE`) |
| `title` | `name` (perfil) o `name` (repositorio) |
| `entity` | `"profile"` o `"repository"` |
| `provider` | `"github"` |
| `meta_payload` | JSON estructurado por el microservicio en C |
| `created_at` | Gestionado por DB |
| `updated_at` | Gestionado por DB |

