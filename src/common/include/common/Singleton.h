#pragma once
#include <QObject>
#include <QJSEngine>
#include <QQmlEngine>

#define VISIONRT_QML_SINGLETON(Class) \
    QML_SINGLETON \
public: \
    static Class *getInstance(); \
    static Class *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);
