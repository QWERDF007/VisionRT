#pragma once
#include "ui/UiPreferences.h"
#include <QFile>

namespace visionrt::testing {
class SettingsFileGuard final
{
public:
    SettingsFileGuard() : path_(UiPreferences::path())
    {
        QFile file(path_);
        existed_ = file.exists();
        if (file.open(QIODevice::ReadOnly)) previous_ = file.readAll();
        file.close();
        QFile::remove(path_);
    }
    ~SettingsFileGuard()
    {
        QFile::remove(path_);
        if (existed_) {
            QFile file(path_);
            if (file.open(QIODevice::WriteOnly)) file.write(previous_);
        }
    }
    SettingsFileGuard(const SettingsFileGuard &) = delete;
    SettingsFileGuard &operator=(const SettingsFileGuard &) = delete;
private:
    QString path_;
    QByteArray previous_;
    bool existed_ = false;
};
}
