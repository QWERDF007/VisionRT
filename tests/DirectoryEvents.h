#pragma once

#include <QStringList>
#include <array>
#include <filesystem>
#include <stdexcept>

#ifdef _WIN32
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <Windows.h>

namespace visionrt::testing {
class DirectoryEvents final
{
public:
    explicit DirectoryEvents(const std::filesystem::path &root)
    {
        directory_
            = CreateFileW(root.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        if (directory_ == INVALID_HANDLE_VALUE)
            throw std::runtime_error("cannot observe directory events");
        operation_.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!operation_.hEvent)
        {
            CloseHandle(directory_);
            throw std::runtime_error("cannot create directory event");
        }
        arm();
    }

    ~DirectoryEvents()
    {
        CancelIoEx(directory_, &operation_);
        DWORD ignored;
        GetOverlappedResult(directory_, &operation_, &ignored, TRUE);
        CloseHandle(operation_.hEvent);
        CloseHandle(directory_);
    }

    DirectoryEvents(const DirectoryEvents &)            = delete;
    DirectoryEvents &operator=(const DirectoryEvents &) = delete;

    QStringList take()
    {
        QStringList result;
        while (WaitForSingleObject(operation_.hEvent, 0) == WAIT_OBJECT_0)
        {
            DWORD bytes = 0;
            if (!GetOverlappedResult(directory_, &operation_, &bytes, FALSE) || bytes == 0)
                throw std::runtime_error("directory event observation failed or overflowed");
            DWORD offset = 0;
            for (;;)
            {
                const auto *event = reinterpret_cast<const FILE_NOTIFY_INFORMATION *>(buffer_.data() + offset);
                result.append(QString::fromWCharArray(event->FileName, event->FileNameLength / sizeof(wchar_t)));
                if (!event->NextEntryOffset)
                    break;
                offset += event->NextEntryOffset;
            }
            arm();
        }
        return result;
    }

private:
    void arm()
    {
        ResetEvent(operation_.hEvent);
        if (!ReadDirectoryChangesW(directory_, buffer_.data(), static_cast<DWORD>(buffer_.size()), TRUE,
                                   FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE
                                       | FILE_NOTIFY_CHANGE_LAST_WRITE,
                                   nullptr, &operation_, nullptr))
            throw std::runtime_error("cannot start directory event observation");
    }

    HANDLE     directory_ = INVALID_HANDLE_VALUE;
    OVERLAPPED operation_{};
    alignas(DWORD) std::array<std::byte, 65536> buffer_{};
};
} // namespace visionrt::testing
#endif
