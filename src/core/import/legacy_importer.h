#pragma once

#include <QHash>
#include <QImage>
#include <QString>
#include <QVector>

#include <functional>

namespace ClipboardHistory::Core {

enum class LegacyImportError {
    None,
    InvalidSource,
    DatabaseOpenFailed,
    UnsupportedSchema,
    CorruptDatabase,
    SourceChanged,
    HashFailed,
    HandlerRejected
};

struct LegacyImportEntry {
    qint64 sourceId = 0;
    QString kind;
    QString content;
    QImage image;
    qint64 createdAtMsUtc = 0;
};

struct LegacyImportIssue {
    qint64 rowId = 0;
    QString code;
};

struct LegacyImportReport {
    LegacyImportError error = LegacyImportError::None;
    QString sourceSha256;
    qint64 scannedRows = 0;
    QHash<QString, qint64> supportedRows;
    qint64 unsupportedRows = 0;
    bool issuesTruncated = false;
    QVector<LegacyImportIssue> issues;

    explicit operator bool() const { return error == LegacyImportError::None; }
};

// Inspects the original ClipboardHistory database without changing it or importing records.
class LegacyImporter final {
public:
    using EntryHandler = std::function<bool(const QString &sourceSha256,
                                            const LegacyImportEntry &entry)>;

    static LegacyImportReport inspect(const QString &databaseFilePath,
                                      const QString &imagesDirectory,
                                      const EntryHandler &onEntry = {});
};

} // namespace ClipboardHistory::Core
