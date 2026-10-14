# Phase 5: Legacy ClipboardHistory import

## Scope and source format

The old repository is [proacane/ClipboardHistory](https://github.com/proacane/ClipboardHistory). Its README, `databasehandler.cpp`, `clipboardmanager.cpp`, and `const.h` were inspected through the connected GitHub source reader. A local `git clone` was unavailable because this machine has no GitHub credentials configured for Schannel; the source files were still readable through the GitHub connector.

The old application stores `clipboard_history.db` with `clipboard_history(id, type, content, timestamp)`. Type values are 1=text, 2=image, and 3=file. Image rows store an absolute image path in `content`; the old writer places images under `{DataPath}/images/` with names `image_<sha256>.png`. Timestamps are SQLite `CURRENT_TIMESTAMP` UTC strings. The configured data directory is user-changeable in the old app, so the importer must be given the exact old `images/` directory rather than assume `D:/ClipboardHistory/config/images`.

## Implemented import flow

`src/core/import/legacy_importer.{h,cpp}` performs a read-only scan. It opens SQLite with `QSQLITE_OPEN_READONLY` and `PRAGMA query_only`, checks database integrity and required columns, counts recognized text/image/path rows, reports unsupported row IDs and reason codes, verifies image paths resolve to regular files inside the selected legacy images directory, decodes images with a pixel limit, and hashes the source database before and after scanning. It returns `SourceChanged` when the database changes during scanning. It never moves, deletes, or writes to source files. The report contains no clipboard body text or full paths. Its callback receives one `LegacyImportEntry` at a time, including source row ID, mapped kind/content, decoded image, and original UTC millisecond timestamp. The settings page now selects the legacy database and starts the asynchronous import; imported results refresh the history model.

`HistoryService::importLegacy()` stores decoded images through `ImageStore`, then calls `HistoryRepository::importLegacyEntry()` for each valid row. `HistoryTaskQueue::importLegacy()` submits that work to the existing storage worker and emits `legacyImportCompleted`; only one import request can be pending at a time. These APIs are available to the future settings/import screen; UI and `main.cpp` integration are intentionally outside this module.

Repository schema v5 adds `legacy_import_sources(source_path, source_sha256)` and `legacy_import_rows(source_sha256, source_row_id, history_id)`. Each row and its source marker are inserted in one transaction. A repeat call with the same source hash and row ID is a no-op and does not alter the saved record or `usage_count`. The original UTC timestamp is inserted unchanged. The canonical source path is bound to the first fingerprint; if the same path later hashes differently, import returns `LegacySourceChanged` before writing more rows. This prevents silently duplicating entries after the old database changes. Previously committed rows remain marked after an interrupted run, so retrying the unchanged source skips completed rows and continues.

Image files are written before the repository transaction. If the transaction fails, the service checks whether the image reference is still used and removes it only when unreferenced. Missing/corrupt/out-of-root old images are reported as skipped rows; other valid rows can still import.

This avoids `HistoryService::record()` and its duplicate-increment behavior. Source database contents are only read; the new schema stores the old database's canonical path and fingerprint in the app's own database for change detection.

## Validation

The importer is integrated in the root CMake build and settings UI. Debug and Release builds pass. The importer QtTest target passes 11 cases, and the Release importer CTest was rerun successfully as an isolated process. Direct batched CTest runs on this Windows environment can fail to start later executables with `0xc0000135`; each target passes when run separately. The deployed Release application remained alive through startup and forwarded a second-instance activation successfully.

The tests cover repeated import without duplicate rows/count changes, old timestamp retention, partial import followed by retry after a missing image becomes available, changed source rejection, transaction failure image cleanup, v4-to-v5 migration, image path containment, unknown schema, source hash stability, and async worker-queue integration.
