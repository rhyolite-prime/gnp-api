# Video service

`gnp::services::VideoService` provides asynchronous database CRUD for the existing
`videos` table. It does not upload/delete physical media or add HTTP routes.

- `getAll(pageNo, pageSize, query)`: one-based pagination (size 1–100), SQL LIKE
  search over title/description, newest first with ID as a stable tie-breaker.
  Returns `data`, `totalCount`, `totalPages`, `pageNo`, `pageSize`, `lowerBound`,
  and `upperBound`; empty pages have zero bounds.
- `getVideoDetails(id)`: fetch one video.
- `create(VideoDto)`: generate UUID, timestamps, and zero engagement counters.
- `update(VideoDto, id)`: full replacement of editable fields; preserves ID,
  creation audit fields and engagement counters. Omitted optional fields reset
  to DTO defaults, except omitted `modifiedBy` preserves the existing value.
- `deleteVideo(id)`: hard-delete the database record only.

All methods return `BaseApiResponse`. Missing records return
`ERR_RESOURCE_NOT_FOUND`; invalid input returns `ERR_VALIDATION`; database
failures return `ERR_DB_QUERY` without exposing database details to clients.
Video results use the generated model's snake_case JSON representation (including
its string representations of PostgreSQL tags and metadata).

## Input

Include `dto/VideoDto.h`. `fromJson` accepts camelCase fields:

```json
{
  "title": "News bulletin",
  "description": "Evening news",
  "fileName": "bulletin.mp4",
  "filePath": "/videos/bulletin.mp4",
  "fileSize": 1024,
  "durationInMins": 5,
  "format": "mp4",
  "thumbnailUrl": "/images/bulletin.jpg",
  "status": 0,
  "tags": ["news"],
  "metadata": {"source": "studio"},
  "checksum": ""
}
```

Title, fileName and filePath are required. Sizes/duration/status cannot be
negative; strings are checked against model length limits. Tags must be strings
and metadata an object. Malformed JSON field types throw `std::invalid_argument`
when constructing/parsing the DTO, before calling the service. Optional
`createdBy` and `modifiedBy` are UUIDs and should be populated by a trusted caller
from the authenticated identity, not blindly accepted from HTTP input.

DTO tests are included in the existing Drogon test target. Database CRUD needs a
configured PostgreSQL database with the existing `videos` schema.
