// Text contract for the log service: one finished line per record, the same line in the window and
// in the daily file, UTF-8 boundary-aware truncation and the allocation budget of the bounding
// helper. Aggregation and lifecycle behaviour live in their own files.
#include "log_test_support.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

#include <cstdlib>
#include <iostream>
#include <new>

// Thread-local allocation probe: observes the heap traffic of boundedSingleLine in isolation.
static thread_local bool        g_trackAllocations = false;
static thread_local std::size_t g_allocatedCount   = 0;
static thread_local std::size_t g_allocatedBytes   = 0;

void *operator new(std::size_t size)
{
    if (g_trackAllocations)
    {
        ++g_allocatedCount;
        g_allocatedBytes += size;
    }
    void *ptr = std::malloc(size);
    if (!ptr)
        throw std::bad_alloc();
    return ptr;
}

void operator delete(void *ptr) noexcept
{
    std::free(ptr);
}

void operator delete(void *ptr, std::size_t) noexcept
{
    std::free(ptr);
}

using namespace log_test;
using visionrt::boundedSingleLine;
using visionrt::LogLevel;
using visionrt::LogSource;
using visionrt::LogModel;
using visionrt::LogService;

namespace
{
void boundedTextMatrix()
{
    struct MatrixCase
    {
        std::size_t      cap;
        std::string_view text;
        std::string_view expected;
        bool             truncated;
    };

    const MatrixCase cases[] = {
        {0, "", "", false},
        {0, "a", "", true},
        {1, "a", "a", false},
        {1, "ab", "a", true},
        {1, "中文", "", true},
        {2, "ab", "ab", false},
        {2, "abc", "ab", true},
        {2, "中文", "", true},
        {3, "abc", "abc", false},
        {3, "abcd", "…", true},
        {3, "中文", "…", true},
        {4, "中abc", "…", true},
        {5, "中abc", "…", true},
        {6, "中abcd", "中…", true},
        {6, "中abc", "中abc", false},
        {7, "中文abc", "中…", true},
        {6, "中文", "中文", false},
        {7, "中文", "中文", false},
        // 4-byte UTF-8 (Emoji)
        {3, "\xF0\x9F\x98\x80", "…", true},
        {4, "\xF0\x9F\x98\x80", "\xF0\x9F\x98\x80", false},
        {5, "\xF0\x9F\x98\x80" "ab", "…", true},
        {5, "\xF0\x9F\x98\x80" "a", "\xF0\x9F\x98\x80" "a", false},
        {7, "\xF0\x9F\x98\x80" "abcd", "\xF0\x9F\x98\x80…", true},
        {7, "\xF0\x9F\x98\x80" "abc", "\xF0\x9F\x98\x80" "abc", false},
        {8, "\xF0\x9F\x98\x80" "abcde", "\xF0\x9F\x98\x80" "a…", true},
        // Escape sequences
        {2, "\n", "\\n", false},
        {3, "\r\n", "…", true},
        {4, "\r\n", "\\r\\n", false},
        {2, "\v", "\\v", false},
        {2, "\f", "\\f", false},
        {2, std::string_view("\0", 1), "\\0", false},
        // TAB and 0x1F stay readable
        {16, "a\tb", "a\tb", false},
        {16, "a\x1F" "b", "a\x1F" "b", false},
        // Malformed UTF-8 must not leak a raw newline
        {16, "\xE2\x0A\x78", "?\\nx", false},
        // Capacity larger than the message
        {4096, "abc", "abc", false},
    };

    for (std::size_t idx = 0; idx < sizeof(cases) / sizeof(cases[0]); ++idx)
    {
        const auto &item = cases[idx];
        const auto  result = boundedSingleLine(item.text, item.cap);
        require(result.text == item.expected && result.truncated == item.truncated,
                QString("boundedSingleLine case %1 failed: cap=%2 text='%3' got='%4' (trunc=%5)")
                    .arg(idx)
                    .arg(item.cap)
                    .arg(QString::fromUtf8(item.text.data(), static_cast<int>(item.text.size())))
                    .arg(QString::fromUtf8(result.text.data(), static_cast<int>(result.text.size())))
                    .arg(result.truncated)
                    .toUtf8()
                    .constData());
    }

    // Exact boundary: a message of exactly the cap is kept untouched.
    const std::string large(4096, 'x');
    const auto        exact = boundedSingleLine(large, 4096);
    require(exact.text.size() == 4096 && !exact.truncated, "output sizing: a full-size message must stay intact");
    const auto cut = boundedSingleLine(large, 100);
    require(cut.text.size() == 100 && cut.truncated, "output sizing: an oversized message must end at the cap");

    // The returned string never reserves the caller's cap when the payload is smaller.
    const auto shortText = boundedSingleLine("abc", 4096);
    require(shortText.text == "abc" && shortText.text.capacity() < 4096,
            "return capacity: short text with a large cap pre-allocated the cap");
    require(cut.text.capacity() < 4096, "return capacity: long text with a small cap allocated the full input");

    // Allocation probe: neither path may allocate the discarded tail.
    g_allocatedCount    = 0;
    g_allocatedBytes    = 0;
    g_trackAllocations  = true;
    const auto probeShort = boundedSingleLine("abc", 4096);
    g_trackAllocations  = false;
    std::cout << "[Allocation Probe] Short input (cap=4096): " << g_allocatedCount << " allocs, "
              << g_allocatedBytes << " bytes"
#if defined(_MSC_VER)
              << " [MSVC " << _MSC_VER << "]"
#elif defined(__clang__)
              << " [Clang " << __clang_major__ << "." << __clang_minor__ << "]"
#elif defined(__GNUC__)
              << " [GCC " << __GNUC__ << "." << __GNUC_MINOR__ << "]"
#else
              << " [Compiler unknown]"
#endif
              << " (thread-local operator new/std::string)\n";
    require(probeShort.text == "abc" && !probeShort.truncated, "allocation probe: short text content check");
    require(g_allocatedBytes < 4096, "allocation probe: short input with a large cap allocated the cap");

    g_allocatedCount   = 0;
    g_allocatedBytes   = 0;
    g_trackAllocations = true;
    const auto probeCut = boundedSingleLine(large, 100);
    g_trackAllocations = false;
    std::cout << "[Allocation Probe] Long input (cap=100): " << g_allocatedCount << " allocs, " << g_allocatedBytes
              << " bytes (thread-local operator new/std::string)\n";
    require(probeCut.text.size() == 100 && probeCut.truncated, "allocation probe: truncated text content check");
    require(g_allocatedBytes < 4096, "allocation probe: a truncated message allocated the full input size");
}
} // namespace

int main(int argc, char **argv)
try
{
    QCoreApplication application(argc, argv);
    QTemporaryDir    directory;
    require(directory.isValid(), "log fixture directory is invalid");

    ServiceSession session;
    session.start(directory.filePath(QStringLiteral("visionrt.log")));

    LogModel model;
    model.setCapacity(session.config().uiCapacity);
    session.service().setViewActive(true);

    // 1. Every row is one finished line: time, level, thread, business source and text, with no
    //    logger name and no ANSI escape. The file receives the identical line.
    LogService::write(LogLevel::Info, LogSource::System, QStringLiteral("单行文本"));
    require(QTest::qWaitFor([&] { return anyRow(model, [](const QString &line) { return line.endsWith(QStringLiteral("单行文本")); }); },
                            2000),
            "the text record did not reach the window");
    const QRegularExpression linePattern(
        QStringLiteral("^\\[\\d{4}/\\d{2}/\\d{2} \\d{2}:\\d{2}:\\d{2}\\.\\d{3}\\] \\[[A-Z]\\] \\[\\d+\\] "
                       "\\[[^\\]]+\\] .*$"));
    require(anyRow(model, [&](const QString &line) { return linePattern.match(line).hasMatch(); }),
            "a log row is not the expected single-line pattern");
    require(!anyRow(model, [](const QString &line) { return line.contains(QStringLiteral("[visionrt]")); }),
            "a log row still contains the logger name");
    require(!anyRow(model, [](const QString &line) { return line.contains(QChar(0x1b)); }),
            "a log row contains ANSI escapes");
    const auto visibleLine = rowLine(model, model.rowCount() - 1);
    session.service().flush();
    QTest::qWait(120);
    require(fileText(datedLog(directory.path())).contains(visibleLine),
            "the file does not contain the identical line the window displays");

    // 2. One record is one line: CR/LF and other line breaks are escaped, never written through.
    LogService::write(LogLevel::Info, LogSource::System, QStringLiteral("第一行\r\n第二行\n第三行"));
    require(QTest::qWaitFor([&] { return anyRow(model, [](const QString &line) { return line.contains(QStringLiteral("第一行")); }); },
                            2000),
            "the multi-line record did not reach the window");
    require(anyRow(model, [](const QString &line) { return line.contains(QStringLiteral("第一行\\r\\n第二行\\n第三行")); }),
            "line breaks were not escaped into visible sequences");
    for (int row = 0; row < model.rowCount(); ++row)
    {
        const auto line = rowLine(model, row);
        require(!line.contains(QLatin1Char('\n')) && !line.contains(QLatin1Char('\r')),
                "a log row contains a raw line break");
    }

    // 3. Long messages are capped identically for the window and the file.
    const auto oversized = QString(200, QLatin1Char('x'));
    LogService::write(LogLevel::Info, LogSource::System, oversized);
    require(QTest::qWaitFor([&] { return anyRow(model, [](const QString &line) { return line.endsWith(QStringLiteral("…")); }); },
                            2000),
            "an oversized log message was not truncated");
    for (int row = 0; row < model.rowCount(); ++row)
        require(rowLine(model, row).size() < 200, "a truncated row kept the full payload");

    // 4. A byte cap never splits a UTF-8 character.
    session.restart([]
                    {
                        auto narrow            = testConfig();
                        narrow.maxMessageBytes = 7;
                        return narrow;
                    }(),
                    directory.filePath(QStringLiteral("visionrt.log")));
    session.service().setViewActive(true);
    LogService::write(LogLevel::Info, LogSource::System, QStringLiteral("中文中文"));
    require(QTest::qWaitFor(
                [&]
                {
                    for (const auto &record : session.service().take(64))
                        if (record.line.contains(QStringLiteral("中…")))
                            return true;
                    return false;
                },
                2000),
            "a multi-byte message was not capped on a character boundary");
    session.service().flush();
    QTest::qWait(100);
    require(!fileText(datedLog(directory.path())).contains(QChar(0xFFFD)),
            "the daily file contains a truncated UTF-8 sequence");

    // 5. Malformed UTF-8 keeps its embedded newline escaped and produces valid text.
    session.restart(testConfig(), directory.filePath(QStringLiteral("visionrt.log")));
    session.service().setViewActive(true);
    const std::string badSequence = "\xE2\x0A\x78";
    LogService::write(LogLevel::Info, LogSource::System, std::string_view(badSequence));
    require(QTest::qWaitFor(
                [&]
                {
                    for (const auto &record : session.service().take(64))
                        if (record.line.contains(QStringLiteral("\\n")))
                            return true;
                    return false;
                },
                2000),
            "embedded newline in malformed UTF-8 was not escaped");

    boundedTextMatrix();

    std::cout << "Log text contracts passed: one finished line, identical file content, control-character "
                 "escaping, UTF-8 boundary truncation and bounded allocation\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
