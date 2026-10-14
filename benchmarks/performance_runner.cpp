#include "../src/core/history/history_repository.h"
#include "../src/core/history/history_service.h"
#include "../src/core/history/history_task_queue.h"
#include "../src/core/history/image_store.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSettings>
#include <QStorageInfo>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QtEndian>
#include <QTimer>
#include <QThread>
#include <QRandomGenerator>
#include <QUuid>

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <tuple>

using ClipboardHistory::Core::HistoryRepository;
using ClipboardHistory::Core::ImageStore;

namespace {

constexpr quint32 Seed = 0xC11F2026;
constexpr int Samples = 30;
const std::array<int, 3> DatasetSizes{10'000, 100'000, 500'000};

struct Summary {
    double p50 = 0;
    double p95 = 0;
    double maximum = 0;
};

Summary summarize(QVector<double> values)
{
    std::sort(values.begin(), values.end());
    const auto percentile = [&values](double p) {
        const int index = std::clamp(static_cast<int>(std::ceil(p * values.size())) - 1,
                                     0, static_cast<int>(values.size()) - 1);
        return values.at(index);
    };
    return {percentile(0.50), percentile(0.95), values.constLast()};
}

qint64 residentBytes()
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    return GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))
        ? static_cast<qint64>(counters.WorkingSetSize) : -1;
#else
    return -1;
#endif
}

bool seedDatabase(const QString &databasePath, const QString &imagesPath, int count,
                  qint64 &elapsedMs, QString &error)
{
    {
        HistoryRepository schema(databasePath);
        if (!schema.initialize()) {
            error = QStringLiteral("repository schema initialization failed");
            return false;
        }
    }

    ImageStore imageStore(imagesPath);
    QImage image1080(1920, 1080, QImage::Format_RGB32);
    image1080.fill(QColor(74, 126, 210));
    const auto imageRef1080 = imageStore.store(image1080);
    QImage image4k(3840, 2160, QImage::Format_RGB32);
    image4k.fill(QColor(38, 153, 123));
    const auto imageRef4k = imageStore.store(image4k);
    if (!imageRef1080 || !imageRef4k) {
        error = QStringLiteral("could not create synthetic image fixtures");
        return false;
    }

    const QString connectionName = QStringLiteral("PerfSeed_%1").arg(QUuid::createUuid().toString());
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(databasePath);
        if (!db.open()) {
            error = db.lastError().text();
            return false;
        }
        QSqlQuery setup(db);
        if (!setup.exec(QStringLiteral("PRAGMA journal_mode=OFF"))
            || !setup.exec(QStringLiteral("PRAGMA synchronous=OFF")) || !db.transaction()) {
            error = setup.lastError().text();
            db.close();
            db = QSqlDatabase();
            QSqlDatabase::removeDatabase(connectionName);
            return false;
        }

        QSqlQuery insert(db);
        insert.prepare(QStringLiteral(
            "INSERT INTO history_entries(kind, content, image_ref, created_at_ms_utc, usage_count, content_hash, favorite) "
            "VALUES(:kind, :content, :image_ref, :created_at, 1, :hash, 0)"));
        QElapsedTimer timer;
        QRandomGenerator random(Seed);
        timer.start();
        for (int i = 0; i < count; ++i) {
            QString kind;
            QString content = QStringLiteral("");
            QString imageRef = QStringLiteral("");
            const int bucket = i % 10;
            if (bucket < 7) {
                kind = QStringLiteral("text");
                const int textBucket = i % 100;
                const int length = textBucket == 0 ? 16'384 : (textBucket < 10 ? 1'024 : 32);
                content = QStringLiteral("benchmark-%1 common-token ").arg(i);
                if (i % 997 == 0)
                    content += QStringLiteral("benchmark-needle ");
                if (content.size() < length)
                    content += QString(length - content.size(), QLatin1Char('x'));
            } else if (bucket < 9) {
                kind = QStringLiteral("image");
                imageRef = random.bounded(2) == 0 ? imageRef1080.reference : imageRef4k.reference;
            } else {
                kind = QStringLiteral("path");
                content = QStringLiteral("E:/Synthetic/Folder/item-%1.txt").arg(i, 6, 10, QLatin1Char('0'));
            }
            QByteArray hashInput;
            for (const QString &field : {kind, content, imageRef}) {
                const QByteArray bytes = field.toUtf8();
                const quint64 length = qToBigEndian(quint64(bytes.size()));
                hashInput.append(reinterpret_cast<const char *>(&length), sizeof(length));
                hashInput.append(bytes);
            }
            insert.bindValue(QStringLiteral(":kind"), kind);
            insert.bindValue(QStringLiteral(":content"), content);
            insert.bindValue(QStringLiteral(":image_ref"), imageRef);
            insert.bindValue(QStringLiteral(":created_at"), i);
            insert.bindValue(QStringLiteral(":hash"), QString::fromLatin1(
                QCryptographicHash::hash(hashInput, QCryptographicHash::Sha256).toHex()));
            if (!insert.exec()) {
                error = insert.lastError().text();
                db.rollback();
                db.close();
                db = QSqlDatabase();
                QSqlDatabase::removeDatabase(connectionName);
                return false;
            }
        }
        if (!db.commit()) {
            error = db.lastError().text();
            db.close();
            db = QSqlDatabase();
            QSqlDatabase::removeDatabase(connectionName);
            return false;
        }
        elapsedMs = timer.elapsed();
        db.close();
        db = QSqlDatabase();
    }
    QSqlDatabase::removeDatabase(connectionName);
    HistoryRepository verified(databasePath);
    ClipboardHistory::Core::HistoryEntry firstEntry;
    qint64 duplicateId = 0;
    bool incremented = false;
    if (!verified.initialize() || !verified.get(1, firstEntry)
        || !verified.recordOrIncrement(firstEntry, duplicateId, incremented)
        || !incremented || duplicateId != firstEntry.id) {
        error = QStringLiteral("seed hashes do not match repository duplicate detection");
        return false;
    }
    return true;
}

bool measure(HistoryRepository &repository, int offset, const QString &search,
             const QString &kind, QVector<double> &samples, QString &error)
{
    const auto warmup = repository.previewPage(offset, 50, search, kind);
    if (!warmup.status || (search.isEmpty() && warmup.entries.size() != 50)
        || (search == QStringLiteral("benchmark-needle") && warmup.entries.isEmpty())
        || (search == QStringLiteral("no-such-synthetic-token") && !warmup.entries.isEmpty())) {
        error = QStringLiteral("preview query failed its status or result-count self-check");
        return false;
    }
    for (int i = 0; i < Samples; ++i) {
        QElapsedTimer timer;
        timer.start();
        const auto page = repository.previewPage(offset, 50, search, kind);
        const qint64 elapsedNs = timer.nsecsElapsed();
        if (!page.status) {
            error = QStringLiteral("preview query failed");
            return false;
        }
        samples.append(elapsedNs / 1'000'000.0);
    }
    return true;
}

void addMetric(QJsonArray &jsonMetrics, QStringList &csvLines, int count,
               const QString &operation, const QVector<double> &values, qint64 rssBytes)
{
    const Summary stats = summarize(values);
    jsonMetrics.append(QJsonObject{
        {QStringLiteral("dataset_rows"), count},
        {QStringLiteral("operation"), operation},
        {QStringLiteral("samples"), values.size()},
        {QStringLiteral("p50_ms"), stats.p50},
        {QStringLiteral("p95_ms"), stats.p95},
        {QStringLiteral("max_ms"), stats.maximum},
        {QStringLiteral("process_rss_bytes"), rssBytes}
    });
    csvLines.append(QStringLiteral("%1,%2,%3,%4,%5,%6,%7")
        .arg(count).arg(operation).arg(values.size()).arg(stats.p50, 0, 'f', 3)
        .arg(stats.p95, 0, 'f', 3).arg(stats.maximum, 0, 'f', 3).arg(rssBytes));
}

bool waitUntilReady(ClipboardHistory::Core::HistoryTaskQueue &queue, QString &error)
{
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    bool received = false;
    ClipboardHistory::Core::HistoryServiceStatus status;
    const auto readyConnection = QObject::connect(
        &queue, &ClipboardHistory::Core::HistoryTaskQueue::initialized, &loop,
        [&](const ClipboardHistory::Core::HistoryServiceStatus &result) {
            received = true;
            status = result;
            loop.quit();
        });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(30'000);
    loop.exec();
    QObject::disconnect(readyConnection);
    if (!received) {
        error = QStringLiteral("history queue initialization timed out");
        return false;
    }
    if (!status) {
        error = QStringLiteral("history queue initialization failed");
        return false;
    }
    return true;
}

bool recordOne(ClipboardHistory::Core::HistoryTaskQueue &queue,
               const ClipboardHistory::Core::HistoryCapture &capture,
               double &elapsedMs, QString &error)
{
    using namespace ClipboardHistory::Core;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    bool received = false;
    HistoryRecordResult result;
    const auto recordConnection = QObject::connect(
        &queue, &HistoryTaskQueue::recordCompleted, &loop,
        [&](const HistoryRecordResult &completed) {
            received = true;
            result = completed;
            loop.quit();
        });

    QElapsedTimer timer;
    timer.start();
    if (queue.submit(capture) != HistoryTaskQueue::SubmitResult::Accepted) {
        QObject::disconnect(recordConnection);
        error = QStringLiteral("history queue rejected a benchmark capture");
        return false;
    }
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(30'000);
    loop.exec();
    QObject::disconnect(recordConnection);
    if (!received) {
        error = QStringLiteral("history record timed out");
        return false;
    }
    if (!result.status) {
        error = QStringLiteral("history record failed");
        return false;
    }
    elapsedMs = timer.nsecsElapsed() / 1'000'000.0;
    return true;
}

bool measureIngestion(const QString &buildRoot, const QString &operation,
                      const std::function<ClipboardHistory::Core::HistoryCapture(int)> &makeCapture,
                      qint64 expectedRows, QJsonArray &jsonMetrics,
                      QStringList &csvLines, QString &error)
{
    using namespace ClipboardHistory::Core;
    QTemporaryDir tempRoot(QDir(buildRoot).filePath(QStringLiteral("perf-ingest-XXXXXX")));
    const QString normalizedBuild = QDir::fromNativeSeparators(buildRoot);
    const QString normalizedTemp = QDir::fromNativeSeparators(QFileInfo(tempRoot.path()).canonicalFilePath());
    if (!tempRoot.isValid() || normalizedTemp.isEmpty()
        || !normalizedTemp.startsWith(normalizedBuild + QLatin1Char('/'))) {
        error = QStringLiteral("ingestion dataset escaped the benchmark build directory");
        return false;
    }

    const QString databasePath = QDir(tempRoot.path()).filePath(QStringLiteral("history.sqlite3"));
    const QString imagesPath = QDir(tempRoot.path()).filePath(QStringLiteral("images"));
    QVector<double> samples;
    samples.reserve(Samples);
    {
        HistoryTaskQueue queue(databasePath, imagesPath);
        if (!waitUntilReady(queue, error))
            return false;
        for (int i = 0; i < Samples; ++i) {
            const HistoryCapture capture = makeCapture(i);
            double elapsedMs = 0;
            if (!recordOne(queue, capture, elapsedMs, error))
                return false;
            samples.append(elapsedMs);
        }
    }

    HistoryRepository verification(databasePath);
    if (!verification.initialize()) {
        error = QStringLiteral("could not reopen the ingestion benchmark database");
        return false;
    }
    const HistoryPreviewPage page = verification.previewPage(0, 50, {}, {}, true);
    if (!page.status) {
        error = QStringLiteral("ingestion benchmark count query failed for %1").arg(operation);
        return false;
    }
    if (page.totalCount != expectedRows) {
        error = QStringLiteral("ingestion benchmark count mismatch for %1: expected %2, got %3")
                    .arg(operation).arg(expectedRows).arg(page.totalCount);
        return false;
    }
    addMetric(jsonMetrics, csvLines, static_cast<int>(expectedRows), operation,
              samples, residentBytes());
    return true;
}

bool measureClear(const QString &buildRoot, int count, QJsonArray &jsonMetrics,
                  QStringList &csvLines, QString &error)
{
    using namespace ClipboardHistory::Core;
    QTemporaryDir tempRoot(QDir(buildRoot).filePath(QStringLiteral("perf-clear-XXXXXX")));
    const QString normalizedBuild = QDir::fromNativeSeparators(buildRoot);
    const QString normalizedTemp = QDir::fromNativeSeparators(QFileInfo(tempRoot.path()).canonicalFilePath());
    if (!tempRoot.isValid() || normalizedTemp.isEmpty()
        || !normalizedTemp.startsWith(normalizedBuild + QLatin1Char('/'))) {
        error = QStringLiteral("cleanup dataset escaped the benchmark build directory");
        return false;
    }

    const QString databasePath = QDir(tempRoot.path()).filePath(QStringLiteral("history.sqlite3"));
    const QString imagesPath = QDir(tempRoot.path()).filePath(QStringLiteral("images"));
    qint64 seedMs = 0;
    if (!seedDatabase(databasePath, imagesPath, count, seedMs, error))
        return false;

    double elapsedMs = 0;
    {
        HistoryService service(databasePath, imagesPath);
        if (!service.initialize()) {
            error = QStringLiteral("could not open the cleanup benchmark database");
            return false;
        }
        QElapsedTimer timer;
        timer.start();
        const HistoryMutationResult cleared = service.clear();
        elapsedMs = timer.nsecsElapsed() / 1'000'000.0;
        if (!cleared.status || cleared.cleanupPending) {
            error = QStringLiteral("history cleanup failed or left image cleanup pending");
            return false;
        }
        const HistoryPreviewPage remaining = service.previewPage(0, 50, {}, {}, true);
        if (!remaining.status || remaining.totalCount != 0
            || !QDir(imagesPath).entryList(QDir::Files | QDir::NoDotAndDotDot).isEmpty()) {
            error = QStringLiteral("cleanup benchmark consistency self-check failed");
            return false;
        }
    }
    addMetric(jsonMetrics, csvLines, count, QStringLiteral("clear_history_and_images"),
              QVector<double>{elapsedMs}, residentBytes());
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("clipboard_history_perf"));
    const bool keepData = app.arguments().contains(QStringLiteral("--keep-data"));
    const QString buildRoot = QDir(QCoreApplication::applicationDirPath()).canonicalPath();
    if (buildRoot.isEmpty()) {
        std::cerr << "Cannot resolve benchmark build directory.\n";
        return EXIT_FAILURE;
    }
    const QString outputDir = QDir(buildRoot).filePath(QStringLiteral("performance-results"));
    if (!QDir().mkpath(outputDir)) {
        std::cerr << "Cannot create isolated benchmark output directory.\n";
        return EXIT_FAILURE;
    }
    QTemporaryDir tempRoot(QDir(buildRoot).filePath(QStringLiteral("perf-data-XXXXXX")));
    const QString normalizedRoot = QDir::fromNativeSeparators(buildRoot);
    const QString normalizedTemp = QDir::fromNativeSeparators(QFileInfo(tempRoot.path()).canonicalFilePath());
    if (!tempRoot.isValid() || normalizedTemp.isEmpty()
        || !normalizedTemp.startsWith(normalizedRoot + QLatin1Char('/'))) {
        std::cerr << "Temporary dataset escaped the benchmark build directory.\n";
        return EXIT_FAILURE;
    }

    QJsonArray metrics;
    QStringList csvLines{QStringLiteral("dataset_rows,operation,samples,p50_ms,p95_ms,max_ms,process_rss_bytes")};
    QJsonArray datasetInfo;
    QString error;
    for (const int count : DatasetSizes) {
        const QString datasetDir = QDir(tempRoot.path()).filePath(QString::number(count));
        if (!QDir().mkpath(datasetDir)) {
            std::cerr << "Cannot create temporary dataset directory.\n";
            return EXIT_FAILURE;
        }
        const QString dbPath = QDir(datasetDir).filePath(QStringLiteral("history.sqlite3"));
        const QString imagePath = QDir(datasetDir).filePath(QStringLiteral("images"));
        qint64 seedMs = 0;
        if (!seedDatabase(dbPath, imagePath, count, seedMs, error)) {
            std::cerr << "Dataset generation failed: " << error.toStdString() << '\n';
            return EXIT_FAILURE;
        }
        QFileInfo dbInfo(dbPath);
        datasetInfo.append(QJsonObject{
            {QStringLiteral("rows"), count},
            {QStringLiteral("seed"), static_cast<qint64>(Seed)},
            {QStringLiteral("generator_version"), 2},
            {QStringLiteral("seed_time_ms_excluded_from_metrics"), seedMs},
            {QStringLiteral("database_bytes"), dbInfo.size()}
        });

        {
            HistoryRepository repository(dbPath);
            if (!repository.initialize()) {
                std::cerr << "Cannot open generated dataset through HistoryRepository.\n";
                return EXIT_FAILURE;
            }
            const std::array<std::tuple<QString, int, QString, QString>, 4> operations{{
                {QStringLiteral("first_page_50"), 0, {}, {}},
                {QStringLiteral("deep_page_50"), count - 50, {}, {}},
                {QStringLiteral("search_hit"), 0, QStringLiteral("benchmark-needle"), {}},
                {QStringLiteral("search_miss"), 0, QStringLiteral("no-such-synthetic-token"), {}}
            }};
            for (const auto &[name, offset, search, kind] : operations) {
                QVector<double> samples;
                if (!measure(repository, offset, search, kind, samples, error)) {
                    std::cerr << "Benchmark failed: " << error.toStdString() << '\n';
                    return EXIT_FAILURE;
                }
                addMetric(metrics, csvLines, count, name, samples, residentBytes());
            }
        }
        if (!measureClear(buildRoot, count, metrics, csvLines, error)) {
            std::cerr << "Cleanup benchmark failed: " << error.toStdString() << '\n';
            return EXIT_FAILURE;
        }
    }

    const auto makeText = [](int length, bool duplicate) {
        return [length, duplicate](int index) {
            ClipboardHistory::Core::HistoryCapture capture;
            capture.kind = QStringLiteral("text");
            capture.content = duplicate ? QStringLiteral("same synthetic clipboard text")
                                        : QStringLiteral("unique-%1-").arg(index);
            if (!duplicate && capture.content.size() < length)
                capture.content += QString(length - capture.content.size(), QLatin1Char('x'));
            return capture;
        };
    };
    const auto makeImage = [](int width, int height) {
        return [width, height](int index) {
            ClipboardHistory::Core::HistoryCapture capture;
            capture.kind = QStringLiteral("image");
            capture.image = QImage(width, height, QImage::Format_RGB32);
            capture.image.fill(QColor(74, 126, 210));
            capture.image.setPixelColor(index % width, index % height,
                                        QColor((index + 1) % 255, (index * 7 + 1) % 255,
                                               (index * 11 + 1) % 255));
            return capture;
        };
    };
    const std::array<std::tuple<QString, std::function<ClipboardHistory::Core::HistoryCapture(int)>, qint64>, 5>
        ingestionOperations{{
            {QStringLiteral("record_text_unique_32b"), makeText(32, false), Samples},
            {QStringLiteral("record_text_unique_16kb"), makeText(16'384, false), Samples},
            {QStringLiteral("record_text_duplicate"), makeText(32, true), 1},
            {QStringLiteral("record_image_unique_1080p"), makeImage(1920, 1080), Samples},
            {QStringLiteral("record_image_unique_4k"), makeImage(3840, 2160), Samples}
        }};
    for (const auto &[name, makeCapture, expectedRows] : ingestionOperations) {
        if (!measureIngestion(buildRoot, name, makeCapture, expectedRows, metrics, csvLines, error)) {
            std::cerr << "Ingestion benchmark failed: " << error.toStdString() << '\n';
            return EXIT_FAILURE;
        }
    }

    QSettings cpuRegistry(QStringLiteral("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0"),
                           QSettings::NativeFormat);
    const QStorageInfo storage(buildRoot);
    quint64 physicalMemoryBytes = 0;
#ifdef Q_OS_WIN
    MEMORYSTATUSEX memoryStatus{};
    memoryStatus.dwLength = sizeof(memoryStatus);
    if (GlobalMemoryStatusEx(&memoryStatus))
        physicalMemoryBytes = memoryStatus.ullTotalPhys;
#endif
    QJsonObject environment{
        {QStringLiteral("os"), QSysInfo::prettyProductName()},
        {QStringLiteral("os_build"), QSysInfo::kernelVersion()},
        {QStringLiteral("architecture"), QSysInfo::currentCpuArchitecture()},
        {QStringLiteral("logical_processors"), QThread::idealThreadCount()},
        {QStringLiteral("qt_version"), qVersion()},
        {QStringLiteral("compiler"), QString::fromLatin1(__VERSION__)},
        {QStringLiteral("processor_identifier"), qEnvironmentVariable("PROCESSOR_IDENTIFIER")},
        {QStringLiteral("processor_model"), cpuRegistry.value(QStringLiteral("ProcessorNameString")).toString()},
        {QStringLiteral("physical_memory_bytes"), static_cast<qint64>(physicalMemoryBytes)},
        {QStringLiteral("benchmark_volume_filesystem"), QString::fromLatin1(storage.fileSystemType())},
        {QStringLiteral("benchmark_volume_total_bytes"), static_cast<qint64>(storage.bytesTotal())},
        {QStringLiteral("seed"), static_cast<qint64>(Seed)},
        {QStringLiteral("samples_per_operation"), Samples},
        {QStringLiteral("build_type"), QStringLiteral(CLIPBOARDHISTORY_BUILD_TYPE)},
        {QStringLiteral("clipboard_accessed"), false},
        {QStringLiteral("dataset_root_removed_on_exit"), !keepData},
        {QStringLiteral("gpu_model"), QStringLiteral("not collected")},
        {QStringLiteral("disk_media_type"), QStringLiteral("not collected")}
    };
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QString timestamp = now.toString(QStringLiteral("yyyy-MM-dd-HHmmss"));
    QJsonObject report{
        {QStringLiteral("created_utc"), now.toString(Qt::ISODate)},
        {QStringLiteral("environment"), environment},
        {QStringLiteral("datasets"), datasetInfo},
        {QStringLiteral("metrics"), metrics}
    };
    if (keepData) {
        tempRoot.setAutoRemove(false);
        report.insert(QStringLiteral("preserved_dataset_root"), tempRoot.path());
    }
    QFile jsonFile(QDir(outputDir).filePath(QStringLiteral("benchmark-%1.json").arg(timestamp)));
    if (!jsonFile.open(QIODevice::WriteOnly) || jsonFile.write(QJsonDocument(report).toJson(QJsonDocument::Indented)) < 0) {
        std::cerr << "Cannot write benchmark JSON report.\n";
        return EXIT_FAILURE;
    }
    QFile csvFile(QDir(outputDir).filePath(QStringLiteral("benchmark-%1.csv").arg(timestamp)));
    if (!csvFile.open(QIODevice::WriteOnly)) {
        std::cerr << "Cannot write benchmark CSV report.\n";
        return EXIT_FAILURE;
    }
    for (const auto &line : csvLines)
        csvFile.write(line.toUtf8() + '\n');
    std::cout << QJsonDocument(report).toJson(QJsonDocument::Indented).constData();
    return EXIT_SUCCESS;
}
